#!/bin/sh
# Offline internal dependency build; never replaces the EMU GLES drivers.
set -eu
[ "$#" -eq 4 ] || { echo "usage: $0 NDK STREAM_OBJECTS GLES_OBJECTS GUEST_LIBS" >&2; exit 2; }
ndk=$1
s=$2
gles=$3
guest=$4
cxx="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android35-clang++"
"$cxx" -shared -Wl,--no-undefined -Wl,-soname,libRmeCodecCommon.so \
  -o "$gles/libRmeCodecCommon.so" "$s/ChecksumCalculator.o" "$s/glUtils.o" \
  "$s/EncoderDebug.o" "$s/GLClientState.o" "$s/GLESTextureUtils.o" "$s/GLSharedGroup.o" \
  "$s/IndexRangeCache.o" "$s/etc.o" "$s/Tracing.o" "$guest/libcutils.so" "$guest/libbase.so" -llog -landroid
for api in gles1 gles2; do
  "$cxx" -shared -Wl,--no-undefined "-Wl,-soname,libRme_${api}_enc.so" \
    -o "$gles/libRme_${api}_enc.so" "$gles/$api"/*.o "$gles/libRmeCodecCommon.so" -llog -landroid
done
