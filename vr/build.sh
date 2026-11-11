#!/bin/sh
# Builds FlauxVR.apk for Meta Quest headsets.
#
#   ANDROID_NDK=/path/to/android-ndk-r26d ANDROID_SDK=/path/to/sdk ./build.sh
#
# Needs the Android NDK (r26 or newer), and an Android SDK with "build-tools;34.0.0" and
# "platforms;android-34".  The OpenXR headers are fetched from Maven Central the first time; the
# loader itself is prebuilt/arm64-v8a/libopenxr_loader.so (the Khronos loader, known to work on Quest).  No Gradle: one C compiler call, then aapt2, zipalign and apksigner.
set -e
cd "$(dirname "$0")"
: "${ANDROID_NDK:?Set ANDROID_NDK to your Android NDK folder}"
: "${ANDROID_SDK:?Set ANDROID_SDK to your Android SDK folder}"
BT="$ANDROID_SDK/build-tools/34.0.0"
PLATFORM="$ANDROID_SDK/platforms/android-34/android.jar"
TC="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
OXR_VERSION=1.0.34
OXR="third_party/openxr-$OXR_VERSION"
OUT=build/android

if [ ! -f "$OXR/prefab/modules/headers/include/openxr/openxr.h" ]; then      # only the headers are used from here
    mkdir -p "$OXR"
    curl -fsSL -o "$OXR/loader.aar" \
        "https://repo1.maven.org/maven2/org/khronos/openxr/openxr_loader_for_android/$OXR_VERSION/openxr_loader_for_android-$OXR_VERSION.aar"
    unzip -q -o "$OXR/loader.aar" -d "$OXR"
fi

rm -rf "$OUT"
mkdir -p "$OUT/lib/arm64-v8a" "$OUT/res"
ANDROID_NDK="$ANDROID_NDK" ./tools/get_mbedtls.sh android

"$TC/bin/aarch64-linux-android29-clang" -std=gnu11 -O2 -fPIC -shared -Wall -Wextra -Wno-unused-parameter \
    -Wno-missing-field-initializers -fvisibility=hidden -ffunction-sections -fdata-sections \
    -g -I src -I third_party/mbedtls/include -I "$OXR/prefab/modules/headers/include" -I "$ANDROID_NDK/sources/android/native_app_glue" \
    src/gfx.c src/ui.c src/scene.c src/net.c src/editor.c src/screens.c src/copilot.c src/assets.c src/xr_main.c \
    "$ANDROID_NDK/sources/android/native_app_glue/android_native_app_glue.c" \
    build/android-mbedtls.a -L prebuilt/arm64-v8a -lopenxr_loader \
    -landroid -llog -lEGL -lGLESv3 -lOpenSLES -lm \
    -u ANativeActivity_onCreate -Wl,--gc-sections -Wl,--no-undefined -Wl,-z,max-page-size=16384 \
    -o "$OUT/lib/arm64-v8a/libflauxvr.so"
cp "$OUT/lib/arm64-v8a/libflauxvr.so" "$OUT/libflauxvr.unstripped.so"      # to look up a crash address from log.txt
"$TC/bin/llvm-strip" --strip-unneeded "$OUT/lib/arm64-v8a/libflauxvr.so"
cp prebuilt/arm64-v8a/libopenxr_loader.so "$OUT/lib/arm64-v8a/"

"$BT/aapt2" compile --dir res -o "$OUT/res.zip"
"$BT/aapt2" link -o "$OUT/unaligned.apk" -I "$PLATFORM" --manifest AndroidManifest.xml -A assets "$OUT/res.zip"
cp prebuilt/classes.dex "$OUT/classes.dex"      # an (almost) empty one: the app is all native code
(cd "$OUT" && zip -q -r unaligned.apk lib classes.dex)
"$BT/zipalign" -f -p 4 "$OUT/unaligned.apk" "$OUT/aligned.apk"

KEY="${FLAUX_KEYSTORE:-build/debug.keystore}"       # a throwaway debug key unless you bring your own
if [ ! -f "$KEY" ]; then
    keytool -genkeypair -keystore "$KEY" -storepass android -keypass android -alias androiddebugkey \
        -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1
fi
"$BT/apksigner" sign --min-sdk-version 24 --ks "$KEY" --ks-pass "pass:${FLAUX_KEYSTORE_PASS:-android}" --out build/FlauxVR.apk "$OUT/aligned.apk"
"$BT/apksigner" verify build/FlauxVR.apk
ls -l build/FlauxVR.apk
