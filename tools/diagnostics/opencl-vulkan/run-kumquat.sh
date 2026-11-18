#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -u
here=$(cd -- "$(dirname -- "$0")" && pwd)
if [[ ${1:-} != --inside ]]; then
    if [[ $# -ne 9 ]]; then
        echo "usage: bash run-kumquat.sh CLVK_LIB_DIR CLSPV OPENCL_HEADERS CLIENT_ICD SERVER BACKEND_DIR DEP_LIB_DIRS STRACE HOST_ICD" >&2
        exit 2
    fi
    out=$(mktemp -d /var/tmp/rme-clvk-kumquat.XXXXXXXX) || exit 1
    echo "artifacts=$out"
    exec python3 "$here/../gfxstream-memory-report/supervise.py" "$0" "$out" "$@"
fi
shift
out=$1
clvk_dir=$2
clspv=$3
headers=$4
client_icd=$5
server_binary=$6
backend_dir=$7
dependency_dirs=$8
tracer=$9
host_icd=${10}
for path in "$clvk_dir/libOpenCL.so" "$clvk_dir/libOpenCL.so.1" "$clspv" "$headers/CL/cl.h" "$client_icd" \
            "$server_binary" "$backend_dir/libgfxstream_backend.so.0" "$tracer" "$host_icd"; do
    [[ $path = /* && -f $path ]] || { echo "invalid absolute path: $path" >&2; exit 2; }
done
client_library=$(python3 - "$client_icd" <<'PY'
import json, pathlib, sys
manifest = pathlib.Path(sys.argv[1])
path = pathlib.Path(json.loads(manifest.read_text())["ICD"]["library_path"])
if not path.is_absolute() or not path.is_file():
    raise SystemExit("client ICD must name an existing absolute library path")
print(path)
PY
) || exit 2
mkdir -m 700 "$out/private-tmp" "$out/cache" || exit 1
timeout -k 2 30 sha256sum "$here/probe.c" "$clvk_dir/libOpenCL.so.1" "$clspv" \
    "$client_icd" "$client_library" "$server_binary" "$backend_dir/libgfxstream_backend.so.0" \
    >"$out/input-sha256.txt" || exit 1
timeout -k 2 30 cc -std=c11 -Wall -Wextra -Werror -O2 -I "$headers" "$here/probe.c" \
    -L "$clvk_dir" -lOpenCL -o "$out/probe-opencl" || exit 1
timeout -k 2 10 mount --bind "$out/private-tmp" /tmp || exit 1
export LD_LIBRARY_PATH="$clvk_dir:$backend_dir:$dependency_dirs"
export CLVK_CLSPV_PATH="$clspv"
export CLVK_CACHE_DIR=
export CLVK_COMPILER_TEMP_DIR="$out"
export TMPDIR="$out"
export MESA_SHADER_CACHE_DISABLE=true
export XDG_CACHE_HOME="$out/cache"
export VIRTGPU_KUMQUAT=1
unset VK_DRIVER_FILES VK_ADD_DRIVER_FILES VK_LAYER_PATH VK_INSTANCE_LAYERS LD_PRELOAD LD_AUDIT

env VK_ICD_FILENAMES="$client_icd" timeout -k 2 30 "$out/probe-opencl" >"$out/no-server.log" 2>&1
status=$?
echo "no_server_status=$status"
if [[ $status -ne 1 ]] || grep -q OPENCL_COMPUTE_OK "$out/no-server.log"; then exit 1; fi

env VK_ICD_FILENAMES="$host_icd" stdbuf -oL -eL "$server_binary" \
    --gpu-socket-path=/tmp/kumquat-gpu-0 \
    --renderer-features=VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled \
    >"$out/server.log" 2>&1 &
server=$!
cleanup() {
    local original=$? forced=0 server_status=0
    trap - EXIT
    kill -TERM "$server" 2>/dev/null || true
    for _ in {1..30}; do
        jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server" || break
        sleep .1
    done
    if jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server"; then
        forced=1
        echo "server ignored SIGTERM; sending SIGKILL" >&2
        kill -KILL "$server" 2>/dev/null || true
    fi
    for _ in {1..30}; do
        jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server" || break
        sleep .1
    done
    if jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server"; then
        echo "server did not exit after SIGKILL" >&2
        [[ $original -ne 0 ]] && exit "$original"
        exit 1
    fi
    if wait "$server"; then server_status=0; else server_status=$?; fi
    echo "server_final_status=$server_status (SIGTERM/SIGKILL is a forced stop)"
    if [[ $original -eq 0 && ( $forced -ne 0 || $server_status -ne 143 ) ]]; then exit 1; fi
    exit "$original"
}
trap cleanup EXIT
for _ in {1..150}; do
    [[ -S /tmp/kumquat-gpu-0 ]] && break
    kill -0 "$server" 2>/dev/null || { echo server_exited_early >&2; exit 1; }
    sleep .1
done
[[ -S /tmp/kumquat-gpu-0 ]] || { echo server_socket_timeout >&2; exit 1; }
for mode in bad-kernel mismatch good workgroup-mismatch workgroup; do
    case "$mode" in
        bad-kernel) args=(--bad-kernel);;
        mismatch) args=(--expect-mismatch);;
        good) args=();;
        workgroup-mismatch) args=(--workgroup --expect-mismatch);;
        workgroup) args=(--workgroup);;
    esac
    env VK_ICD_FILENAMES="$client_icd" timeout -k 2 60 "$tracer" -f -qq \
        -e trace=execve,connect,sendmsg,recvmsg -o "$out/$mode.trace" \
        "$out/probe-opencl" "${args[@]}" >"$out/$mode.log" 2>&1
    status=$?
    echo "${mode}_status=$status"
    kill -0 "$server" 2>/dev/null || { echo "server_exited_after=$mode" >&2; exit 1; }
    case "$mode" in
        bad-kernel) [[ $status -eq 1 ]] && grep -q 'FAIL clBuildProgram: OpenCL error -11' "$out/$mode.log" || exit 1;;
        mismatch) [[ $status -eq 1 ]] && grep -q 'intentional expectation mismatch' "$out/$mode.log" || exit 1;;
        good) [[ $status -eq 0 ]] && grep -q 'OPENCL_COMPUTE_OK words=64' "$out/$mode.log" || exit 1;;
        workgroup-mismatch) [[ $status -eq 1 ]] && grep -q 'FAIL group\[0\]:.*intentional expectation mismatch' "$out/$mode.log" || exit 1;;
        workgroup) [[ $status -eq 0 ]] && grep -q 'OPENCL_WORKGROUP_OK groups=4 local=16 elements=64' "$out/$mode.log" || exit 1;;
    esac
    grep -Fq "execve(\"$clspv\"" "$out/$mode.trace" || exit 1
    grep -q '/tmp/kumquat-gpu-0' "$out/$mode.trace" || exit 1
    grep -q 'sendmsg(' "$out/$mode.trace" || exit 1
    grep -q 'recvmsg(' "$out/$mode.trace" || exit 1
done
grep -F 'SystemBlob: enabled' "$out/server.log" || exit 1
grep -F 'VulkanExternalMemoryMode: HostAllocation' "$out/server.log" || exit 1
grep -F 'Selecting Vulkan device:' "$out/server.log" || exit 1
grep -F 'DEVICE name=' "$out/good.log" || exit 1
echo "OPENCL_KUMQUAT_RESEARCH_OK (not a hardware GPU test)"
