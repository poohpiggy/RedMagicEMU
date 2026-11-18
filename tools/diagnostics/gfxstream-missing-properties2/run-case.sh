#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -u
[[ ${1:-} == --inside && $# -eq 10 ]] || exit 2
mode=$2
out=$3
backend=$4
shim=$5
server_binary=$6
probe=$7
client_icd=$8
host_icd=$9
deps=${10}

mkdir -m 700 "$out/tmp" || exit 70
mount --bind "$out/tmp" /tmp || exit 70
export LD_LIBRARY_PATH="$backend:$deps"
export VIRTGPU_KUMQUAT=1
unset VK_DRIVER_FILES VK_ADD_DRIVER_FILES VK_LAYER_PATH VK_INSTANCE_LAYERS LD_PRELOAD LD_AUDIT
server_env=(env "VK_ICD_FILENAMES=$host_icd" "ANDROID_EMU_VK_LOADER_PATH=$shim")
if [[ "$mode" != normal ]]; then
    server_env+=("RME_HIDE_PROPERTIES2=$mode")
fi
"${server_env[@]}" stdbuf -oL -eL "$server_binary" \
    --gpu-socket-path=/tmp/kumquat-gpu-0 \
    --renderer-features=VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled \
    >"$out/server.log" 2>&1 &
server=$!
cleanup() {
    local original=$? forced=0 status=0
    trap - EXIT
    kill -TERM "$server" 2>/dev/null || true
    for _ in {1..30}; do
        jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server" || break
        sleep .1
    done
    if jobs -pr | grep -Fxq "$server" || jobs -ps | grep -Fxq "$server"; then
        forced=1
        kill -KILL "$server" 2>/dev/null || true
    fi
    if wait "$server"; then status=0; else status=$?; fi
    echo "server_final_status=$status forced_kill=$forced"
    if [[ $original -eq 0 && ( $forced -ne 0 || $status -ne 143 ) ]]; then exit 1; fi
    exit "$original"
}
trap cleanup EXIT
for _ in {1..150}; do
    [[ -S /tmp/kumquat-gpu-0 ]] && break
    if ! kill -0 "$server" 2>/dev/null; then
        if wait "$server"; then status=0; else status=$?; fi
        trap - EXIT
        echo "server_exited_before_socket status=$status"
        exit 3
    fi
    sleep .1
done
[[ -S /tmp/kumquat-gpu-0 ]] || { echo server_socket_timeout >&2; exit 4; }
if env VK_ICD_FILENAMES="$client_icd" timeout -k 2 40 "$probe" >"$out/client.log" 2>&1; then
    client_status=0
else
    client_status=$?
fi
if ! kill -0 "$server" 2>/dev/null; then
    if wait "$server"; then status=0; else status=$?; fi
    trap - EXIT
    echo "server_exited_after_client status=$status client_status=$client_status"
    exit 5
fi
echo "mode=$mode client_status=$client_status server=alive"
[[ $client_status -eq 0 ]] || exit 6
