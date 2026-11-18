#!/bin/sh
set -eu
[ "$#" = 2 ] || { echo 'usage: verify_public.sh OPENCL_HEADERS OUTPUT' >&2; exit 2; }
here=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)
headers=$(realpath "$1")
mkdir -p "$2"
out=$(realpath "$2")
# Keep the client and host ICD in different linker search paths.
mkdir -p "$out/guest"
cc -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" -shared -fPIC -pthread \
    -DRME_CL_SOCKET_PATH="\"$out/aemucl\"" -Wl,-soname,libOpenCL.so \
    "$here/abi_guest.c" "$here/abi_io.c" -o "$out/guest/libOpenCL.so"
cc -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" \
    "$here/../../tools/diagnostics/opencl-vulkan/probe.c" \
    -L"$out/guest" -Wl,-rpath,"$out/guest" -lOpenCL -o "$out/probe"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror -O2 -I"$headers" "$here/abi_feature_probe.c" \
    -L"$out/guest" -Wl,-rpath,"$out/guest" -lOpenCL -o "$out/features"
cc -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" \
    "$here/abi_host.c" "$here/abi_host_posix.c" "$here/abi_io.c" "$here/abi_test_host.c" -ldl -o "$out/host"
if [ "${RME_CL_TEST_ASYNC_UPLOAD:-}" = 1 ]; then
    icd=$(realpath "${RME_CL_HOST_ICD:?set RME_CL_HOST_ICD to the host ICD file}")
    cc -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" -shared -fPIC \
        "$here/abi_test_delayed_upload.c" -Wl,-rpath,"$(dirname "$icd")" "$icd" -ldl -o "$out/delayed-upload.so"
fi
if [ "${RME_CL_TEST_ASYNC_READ:-}" = 1 ] && [ -z "${RME_CL_WINDOWS_WORKER:-}" ]; then
    icd=$(realpath "${RME_CL_HOST_ICD:?set RME_CL_HOST_ICD to the host ICD file}")
    cc -std=c11 -Wall -Wextra -Werror -O2 -I"$headers" -shared -fPIC \
        "$here/abi_test_delayed_read.c" -Wl,-rpath,"$(dirname "$icd")" "$icd" -ldl -o "$out/delayed-read.so"
fi
python3 "$here/verify_public.py" "$out"
