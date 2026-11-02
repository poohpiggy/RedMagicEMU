#!/bin/sh
# Сборка camera HAL эмулятора: ARMv7, только libc, классическая хэш-таблица для линкера bionic 2.3
NDK=${NDK:-$ANDROID_NDK_HOME}
BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
[ -x "$BIN/clang" ] || BIN="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
OUT=${1:-../../app/src/main/assets/engines/common/camera.default.so}
mkdir -p "$(dirname "$OUT")"
"$BIN/clang" --target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,camera.default.so \
  -o "$OUT" camera_hal.c -ldl -lc
"$BIN/llvm-strip" "$OUT" 2>/dev/null || true
# 2.3: a HAL with no cameras for firmwares whose own one needs the modem (see camera_gb_none.c)
"$BIN/clang" --target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,libcamera.so \
  -o "$(dirname "$OUT")/libcamera.none.so" camera_gb_none.c
"$BIN/llvm-strip" "$(dirname "$OUT")/libcamera.none.so" 2>/dev/null || true
# native arm64 engine, Android 8.x (kernel 27): arm64, camera module API 2.4 for the HIDL camera provider
"$BIN/clang" --target=aarch64-linux-android26 -DCAM_MODULE_24 -Os -fPIC -shared \
  -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector \
  -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,camera.default.so \
  -o "$(dirname "$OUT")/../n64-${TAG:-27}/system#lib64#hw#camera.default.so" camera_hal.c -ldl -lc
"$BIN/llvm-strip" "$(dirname "$OUT")/../n64-${TAG:-27}/system#lib64#hw#camera.default.so" 2>/dev/null || true
# native arm64 engine, Android 7.1 (kernel 25): the 32-bit camera service of a vendor firmware, camera module API 2.4
# for Camera.openLegacy
"$BIN/clang" --target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -DCAM_MODULE_24 -Os -fPIC -shared   -nostdlib -ffreestanding -fno-builtin -fvisibility=hidden -fno-stack-protector   -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,camera.default.so   -o "$(dirname "$OUT")/../n64-25/system#lib#hw#camera.default.so" camera_hal.c -ldl -lc
"$BIN/llvm-strip" "$(dirname "$OUT")/../n64-25/system#lib#hw#camera.default.so" 2>/dev/null || true
# 2.3 with the HAL linked into CameraService (HTC): CameraHardwareInterface over aemu_cam (camera_gb.cpp). The
# firmware's C++ classes of 2.3 come from its own libraries: link against stubs with their sonames, holding the
# symbols the object needs, each taken from the firmware library that exports it (FW=<a 2.3 system/lib copy>;
# lld rejects the prelinked originals)
FW=${FW:-E:/aemu_work/htccam}
if [ -f "$FW/libcamera_client.so" ]; then
T=$(mktemp -d)
CXXF="--target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -ffreestanding -fno-builtin -fno-exceptions \
  -fno-rtti -fno-threadsafe-statics -fvisibility=hidden -fvisibility-inlines-hidden -fno-stack-protector -fno-use-cxa-atexit"
"$BIN/clang++" $CXXF -c -o "$T/cam.o" camera_gb.cpp
LIBS=""
for l in libcamera_client libbinder libutils liblog libstdc++ libc; do
  "$BIN/llvm-readelf" --dyn-syms "$FW/$l.so" | awk '$7!="UND" && $5!="LOCAL"{print $8}' | sed 's/@.*//' | sort -u > "$T/$l.exp"
done
"$BIN/llvm-nm" -u "$T/cam.o" | awk '{print $2}' | sort -u > "$T/need"
for l in libcamera_client libbinder libutils liblog libstdc++ libc; do
  comm -12 "$T/need" "$T/$l.exp" > "$T/$l.use"; comm -23 "$T/need" "$T/$l.use" > "$T/rest"; mv "$T/rest" "$T/need"
  awk '{print "void " $1 "(void){}"}' "$T/$l.use" > "$T/$l.c"
  "$BIN/clang" --target=armv7a-linux-androideabi21 -shared -nostdlib -fPIC -w -Wl,-soname,$l.so -o "$T/$l.so" "$T/$l.c"
  LIBS="$LIBS $T/$l.so"
done
if [ -s "$T/need" ]; then echo "camera_gb: not in the firmware: $(cat "$T/need")"; else
"$BIN/clang++" --target=armv7a-linux-androideabi21 -shared -nostdlib -nostdlib++ \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,libcamera.so \
  -o "$(dirname "$OUT")/../gb/libcamera.so" "$T/cam.o" $LIBS
"$BIN/llvm-strip" "$(dirname "$OUT")/../gb/libcamera.so" 2>/dev/null || true
fi
rm -r "$T"
fi
# 3.x vendor firmwares (HAL linked into CameraService: HAL_/SEC_openCameraHardware): CameraHardwareInterface of
# 3.x over aemu_cam (camera_hc.cpp). Same stub linking, against a 3.x system/lib copy (FW3: Xoom 2 3.2.2)
FW3=${FW3:-E:/aemu_work/hccam}
if [ -f "$FW3/libcamera_client.so" ]; then
T=$(mktemp -d)
CXXF="--target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -ffreestanding -fno-builtin -fno-exceptions \
  -fno-rtti -fno-threadsafe-statics -fvisibility=hidden -fvisibility-inlines-hidden -fno-stack-protector -fno-use-cxa-atexit"
"$BIN/clang++" $CXXF -c -o "$T/cam.o" camera_hc.cpp
LIBS=""
for l in libcamera_client libbinder libutils liblog libstdc++ libdl libc; do
  "$BIN/llvm-readelf" --dyn-syms "$FW3/$l.so" | awk '$7!="UND" && $5!="LOCAL"{print $8}' | sed 's/@.*//' | sort -u > "$T/$l.exp"
done
"$BIN/llvm-nm" -u "$T/cam.o" | awk '{print $2}' | sort -u > "$T/need"
for l in libcamera_client libbinder libutils liblog libstdc++ libdl libc; do
  comm -12 "$T/need" "$T/$l.exp" > "$T/$l.use"; comm -23 "$T/need" "$T/$l.use" > "$T/rest"; mv "$T/rest" "$T/need"
  awk '{print "void " $1 "(void){}"}' "$T/$l.use" > "$T/$l.c"
  "$BIN/clang" --target=armv7a-linux-androideabi21 -shared -nostdlib -fPIC -w -Wl,-soname,$l.so -o "$T/$l.so" "$T/$l.c"
  LIBS="$LIBS $T/$l.so"
done
if [ -s "$T/need" ]; then echo "camera_hc: not in the firmware: $(cat "$T/need")"; else
"$BIN/clang++" --target=armv7a-linux-androideabi21 -shared -nostdlib -nostdlib++ \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,libcamera.so \
  -o "$(dirname "$OUT")/libcamera.hc.so" "$T/cam.o" $LIBS
"$BIN/llvm-strip" "$(dirname "$OUT")/libcamera.hc.so" 2>/dev/null || true
fi
rm -r "$T"
fi
# 1.6 with SEMC's CameraService (X10): CameraHardwareInterface of 1.6 with SEMC's slots over aemu_cam
# (camera_semc16.cpp). Same stub linking as above, against a 1.6 system/lib copy (FW16): binder classes in
# libutils, CameraParameters in libui.
FW16=${FW16:-E:/aemu_work/semc16}
if [ -f "$FW16/libui.so" ]; then
T=$(mktemp -d)
CXXF="--target=armv7a-linux-androideabi21 -march=armv7-a -mthumb -Os -fPIC -ffreestanding -fno-builtin -fno-exceptions \
  -fno-rtti -fno-threadsafe-statics -fvisibility=hidden -fvisibility-inlines-hidden -fno-stack-protector -fno-use-cxa-atexit"
"$BIN/clang++" $CXXF -c -o "$T/cam.o" camera_semc16.cpp
LIBS=""
for l in libui libutils liblog libstdc++ libdl libc; do
  "$BIN/llvm-readelf" --dyn-syms "$FW16/$l.so" | awk '$7!="UND" && $5!="LOCAL"{print $8}' | sed 's/@.*//' | sort -u > "$T/$l.exp"
done
"$BIN/llvm-nm" -u "$T/cam.o" | awk '{print $2}' | sort -u > "$T/need"
for l in libui libutils liblog libstdc++ libdl libc; do
  comm -12 "$T/need" "$T/$l.exp" > "$T/$l.use"; comm -23 "$T/need" "$T/$l.use" > "$T/rest"; mv "$T/rest" "$T/need"
  awk '{print "void " $1 "(void){}"}' "$T/$l.use" > "$T/$l.c"
  "$BIN/clang" --target=armv7a-linux-androideabi21 -shared -nostdlib -fPIC -w -Wl,-soname,$l.so -o "$T/$l.so" "$T/$l.c"
  LIBS="$LIBS $T/$l.so"
done
if [ -s "$T/need" ]; then echo "camera_semc16: not in the firmware: $(cat "$T/need")"; else
"$BIN/clang++" --target=armv7a-linux-androideabi21 -shared -nostdlib -nostdlib++ \
  -Wl,--hash-style=sysv -Wl,-z,norelro -Wl,--build-id=none -Wl,--no-undefined -Wl,-soname,libcamera.so \
  -o "$(dirname "$OUT")/libcamera.semc16.so" "$T/cam.o" $LIBS
"$BIN/llvm-strip" "$(dirname "$OUT")/libcamera.semc16.so" 2>/dev/null || true
fi
rm -r "$T"
fi
