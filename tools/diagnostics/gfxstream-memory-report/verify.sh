#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail

if [[ ${1:-} != --inside ]]; then
    if [[ $# -ne 7 ]]; then
        echo "usage: bash verify.sh BASELINE_ICD PATCHED_ICD SERVER BACKEND_DIR DEP_LIB_DIR NDK VULKAN_INCLUDE" >&2
        exit 2
    fi
    out=$(mktemp -d /var/tmp/rme-memory-report-check.XXXXXXXX)
    printf 'Research logs: %s\n' "$out"
    here=$(cd -- "$(dirname -- "$0")" && pwd)
    exec python3 "$here/supervise.py" "$0" "$out" "$@"
fi
shift
out=$1
baseline_icd=$2
patched_icd=$3
server=$4
backend_dir=$5
dep_lib_dir=$6
ndk=$7
headers=$8
here=$(cd -- "$(dirname -- "$0")" && pwd)
compute_dir="$here/../vulkan-compute"
shader_tools="$ndk/shader-tools/linux-x86_64"
host_icd=${RME_HOST_ICD:-/usr/share/vulkan/icd.d/lvp_icd.json}

for file in "$baseline_icd" "$patched_icd" "$server" "$headers/vulkan/vulkan.h" \
            "$shader_tools/glslc" "$shader_tools/spirv-val" "$host_icd"; do
    [[ -f $file ]] || { echo "missing: $file" >&2; exit 2; }
done
mkdir "$out/private-tmp"
mount --bind "$out/private-tmp" /tmp

"$shader_tools/glslc" -fshader-stage=compute --target-env=vulkan1.2 \
    "$compute_dir/compute.comp" -o "$out/compute.spv"
"$shader_tools/spirv-val" --target-env vulkan1.2 "$out/compute.spv"
python3 - "$out/compute.spv" "$out/shader_spv.h" <<'PY'
import pathlib
import struct
import sys
data = pathlib.Path(sys.argv[1]).read_bytes()
if len(data) < 20 or len(data) % 4:
    raise SystemExit("invalid SPIR-V")
words = struct.unpack(f"<{len(data) // 4}I", data)
if words[0] != 0x07230203:
    raise SystemExit("invalid SPIR-V magic")
pathlib.Path(sys.argv[2]).write_text(
    "#include <stdint.h>\nstatic const uint32_t shader_spv[] = {\n" +
    ",\n".join("    " + ", ".join(f"0x{x:08x}u" for x in words[i:i+8])
                for i in range(0, len(words), 8)) + "\n};\n", encoding="ascii")
PY
gcc -std=c11 -Wall -Wextra -Werror -I"$headers" "$here/probe.c" \
    -o "$out/probe" -l:libvulkan.so.1
gcc -std=c11 -O2 -I"$headers" -I"$out" -I"$compute_dir" \
    "$compute_dir/probe.c" -o "$out/compute" -l:libvulkan.so.1

export LD_LIBRARY_PATH="$backend_dir:$dep_lib_dir"
server_pid=
server_is_live() {
    jobs -pr | grep -Fxq -- "$1" || jobs -ps | grep -Fxq -- "$1"
}
cleanup() {
    local pid=$server_pid status=0 forced=0 sent=0
    [[ -n $pid ]] || return 0
    if server_is_live "$pid"; then
        if kill -TERM "$pid" 2>/dev/null; then sent=1; fi
    fi
    for _ in {1..20}; do
        server_is_live "$pid" || break
        sleep 0.1
    done
    if server_is_live "$pid"; then
        forced=1
        echo "server $pid ignored SIGTERM; sending SIGKILL" >&2
        kill -KILL "$pid" 2>/dev/null || true
        for _ in {1..20}; do
            server_is_live "$pid" || break
            sleep 0.1
        done
    fi
    if server_is_live "$pid"; then
        echo "server $pid did not exit after SIGKILL; leaving namespace" >&2
        server_pid=
        return 1
    fi
    if wait "$pid"; then status=0; else status=$?; fi
    server_pid=
    printf 'server_exit=%s term_sent=%s forced_kill=%s\n' "$status" "$sent" "$forced"
    [[ $sent -eq 1 && $forced -eq 0 && ( $status -eq 0 || $status -eq 143 ) ]]
}
on_exit() {
    local original=$? cleanup_status=0
    trap - EXIT
    cleanup || cleanup_status=$?
    if [[ $original -eq 0 && $cleanup_status -ne 0 ]]; then
        exit "$cleanup_status"
    fi
    exit "$original"
}
trap on_exit EXIT

for mode in baseline patched; do
    if [[ $mode == baseline ]]; then icd=$baseline_icd; else icd=$patched_icd; fi
    rm -f "$out/private-tmp/kumquat-gpu-0"
    env -u VK_DRIVER_FILES VK_ICD_FILENAMES="$host_icd" "$server" \
        --gpu-socket-path=/tmp/kumquat-gpu-0 \
        --renderer-features=VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled \
        >"$out/$mode-server.log" 2>&1 &
    server_pid=$!
    for _ in $(seq 1 150); do
        [[ -S /tmp/kumquat-gpu-0 ]] && break
        kill -0 "$server_pid" 2>/dev/null || { echo "$mode server exited" >&2; exit 1; }
        sleep 0.1
    done
    [[ -S /tmp/kumquat-gpu-0 ]] || { echo "$mode socket timeout" >&2; exit 1; }
    env -u VK_DRIVER_FILES VIRTGPU_KUMQUAT=1 VK_ICD_FILENAMES="$icd" \
        timeout -k 2 40 "$out/probe" "$mode" >"$out/$mode-probe.log" 2>&1
    env -u VK_DRIVER_FILES VIRTGPU_KUMQUAT=1 VK_ICD_FILENAMES="$icd" \
        timeout -k 2 40 "$out/compute" >"$out/$mode-compute.log" 2>&1
    grep -Fx 'VULKAN_COMPUTE_OK words=64' "$out/$mode-compute.log" >/dev/null
    set +e
    env -u VK_DRIVER_FILES VIRTGPU_KUMQUAT=1 VK_ICD_FILENAMES="$icd" \
        timeout -k 2 40 "$out/compute" --expect-mismatch >"$out/$mode-mismatch.log" 2>&1
    mismatch=$?
    set -e
    if [[ $mismatch -ne 1 ]] ||
       ! grep -F 'FAIL output mismatches=1 (intentional expectation mismatch)' \
            "$out/$mode-mismatch.log" >/dev/null; then
        echo "$mode mismatch negative control failed" >&2
        exit 1
    fi
    kill -0 "$server_pid"
    cleanup
    server_pid=
done

grep -E '^(EXT=|CORE_FEATURES=|QUEUE=|DEVICE=)' "$out/baseline-probe.log" |
    grep -v '^EXT=VK_EXT_device_memory_report:' | sort >"$out/baseline-capabilities.txt"
grep -E '^(EXT=|CORE_FEATURES=|QUEUE=|DEVICE=)' "$out/patched-probe.log" |
    sort >"$out/patched-capabilities.txt"
diff -u "$out/baseline-capabilities.txt" "$out/patched-capabilities.txt"
grep -E '^(MEMORY_REPORT|CREATE_DEVICE_REPORT|PROBE_)' "$out/baseline-probe.log"
grep -E '^(MEMORY_REPORT|CREATE_DEVICE_REPORT|PROBE_)' "$out/patched-probe.log"
echo "Ordinary compute and mismatch negative controls passed for both builds."
