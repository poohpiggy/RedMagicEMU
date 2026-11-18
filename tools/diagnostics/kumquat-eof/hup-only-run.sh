#!/usr/bin/env bash
# Run only inside a new user + mount namespace; see README.
set -u
: "${RME_OUT:?}" "${RME_PARENT_MNT:?}" "${RME_GDB:?}" "${RME_SERVER:?}" "${RME_PROBE:?}"
: "${RME_HOST_ICD:?}" "${RME_CLIENT_ICD:?}" "${RME_LIB_PATH:?}"
if [ "$(readlink /proc/self/ns/mnt)" = "$RME_PARENT_MNT" ]; then
    echo "refusing to bind /tmp outside a new mount namespace" >&2
    exit 70
fi
mkdir -m 700 "$RME_OUT/private-tmp" || exit 70
mount --bind "$RME_OUT/private-tmp" /tmp || exit 70
diag_dir=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$RME_LIB_PATH"
env -u VK_DRIVER_FILES VK_ICD_FILENAMES="$RME_HOST_ICD" "$RME_GDB" --batch \
    -x "$diag_dir/hup-only.gdb" --args "$RME_SERVER" \
    --gpu-socket-path=/tmp/kumquat-gpu-0 --capset-names=gfxstream-vulkan \
    --renderer-features=VulkanExternalMemoryMode:HostAllocation,SystemBlob:enabled >"$RME_OUT/gdb.log" 2>&1 &
gp=$!
sp=
cleanup() {
    if [ -n "$sp" ]; then kill -TERM "$sp" 2>/dev/null || true; fi
    kill -TERM "$gp" 2>/dev/null || true
    wait "$gp" 2>/dev/null || true
}
trap cleanup EXIT
for i in $(seq 1 150); do
    sp=$(ps -o pid= --ppid "$gp" | tr -d " ")
    [ -n "$sp" ] && [ -S /tmp/kumquat-gpu-0 ] && break
    kill -0 "$gp" 2>/dev/null || exit 71
    sleep 0.1
done
[ -n "$sp" ] && [ -S /tmp/kumquat-gpu-0 ] || exit 72
before=$(ls "/proc/$sp/fd" | wc -l)
python3 -c 'import socket; s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET); s.connect("/tmp/kumquat-gpu-0"); s.close()'
for i in $(seq 1 100); do
    grep -q HUP_ONLY_BRANCH "$RME_OUT/gdb.log" && break
    kill -0 "$gp" 2>/dev/null || exit 73
    sleep 0.1
done
grep -q SIMULATED_HUP_ONLY "$RME_OUT/gdb.log" || exit 74
grep -q HUP_ONLY_BRANCH "$RME_OUT/gdb.log" || exit 74
for i in $(seq 1 100); do
    after=$(ls "/proc/$sp/fd" | wc -l)
    [ "$before" -eq "$after" ] && break
    sleep 0.1
done
[ "$before" -eq "$after" ] || { echo "FD count before=$before after=$after" >&2; exit 77; }
env -u VK_DRIVER_FILES VIRTGPU_KUMQUAT=1 VK_ICD_FILENAMES="$RME_CLIENT_ICD" \
    timeout 20 "$RME_PROBE" >"$RME_OUT/probe.log" 2>&1 || exit 75
grep -q "VULKAN_COMPUTE_OK words=64" "$RME_OUT/probe.log" || exit 76
echo "server_pid=$sp simulated_hup_only_branch=yes fd_before=$before fd_after=$after"
echo "subsequent_compute=pass"
