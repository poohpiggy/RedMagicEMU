#!/bin/sh
# framework-res overlay without task thumbnails (see AndroidManifest.xml): builds build/aemu-noshot.apk;
# the app ships it as assets/engines/kk/aemu-noshot.apk and puts it in /vendor/overlay of an open32 firmware.
set -e
cd "$(dirname "$0")"
SDK=${ANDROID_HOME:-$LOCALAPPDATA/Android/Sdk}
JAR="$SDK/platforms/android-36/android.jar"
BT=$(ls -d "$SDK"/build-tools/*/ | sort -V | tail -1)
SIGN="${BT}apksigner"; [ -x "$SIGN" ] || SIGN="$SIGN.bat"
rm -rf build && mkdir -p build
"${BT}aapt2" compile --dir res -o build/res.zip
"${BT}aapt2" link -o build/base.apk --manifest AndroidManifest.xml -I "$JAR" build/res.zip
"${BT}zipalign" -f 4 build/base.apk build/aligned.apk
"$SIGN" sign --ks "${KEYSTORE:-$(cygpath -m "$HOME" 2>/dev/null || echo "$HOME")/.android/debug.keystore}" --ks-pass pass:android --key-pass pass:android --out build/aemu-noshot.apk build/aligned.apk
cp build/aemu-noshot.apk ../../app/src/main/assets/engines/kk/aemu-noshot.apk
ls -la build/aemu-noshot.apk
