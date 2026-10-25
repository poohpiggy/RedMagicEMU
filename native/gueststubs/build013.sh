#!/bin/sh
# aemu-run013.jar: app.aemu.stub.Run for pre-1.0 builds (dex 013). Needs a JDK and the M5 SDK zip (its tools/lib/dx.jar).
set -e
SDKZIP=${1:-E:/aemu_work/early/m5rc14/dl/m5rc14.zip}
OUT=${2:-../../app/src/main/assets/engines/kk/aemu-run013.jar}
rm -rf build013 && mkdir -p build013/c
python -c "import zipfile,sys;z=zipfile.ZipFile(sys.argv[1]);open('build013/dx.jar','wb').write(z.read('android-sdk_m5-rc14_windows/tools/lib/dx.jar'))" "$SDKZIP"
javac --release 8 -g:none -d build013/c old/app/aemu/stub/Run.java
# dx of 2008 reads class files up to version 49; the class uses nothing newer
python -c "p='build013/c/app/aemu/stub/Run.class';d=bytearray(open(p,'rb').read());d[6:8]=b'\x00\x31';open(p,'wb').write(d)"
(cd build013/c && java -jar ../dx.jar --dex --output=../run013.jar app/aemu/stub/Run.class)
cp build013/run013.jar "$OUT"
echo "done: $OUT"
