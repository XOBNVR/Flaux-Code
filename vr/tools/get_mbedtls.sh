#!/bin/sh
# Fetches Mbed TLS (for wss:// room servers) and builds it as a library, once.
#   tools/get_mbedtls.sh pc        -> build/pc-mbedtls.a       (for test.sh)
#   tools/get_mbedtls.sh android   -> build/android-mbedtls.a  (for build.sh; needs ANDROID_NDK)
set -e
cd "$(dirname "$0")/.."
VERSION=v3.6.2
SRC=third_party/mbedtls
if [ ! -f "$SRC/include/mbedtls/ssl.h" ]; then
    mkdir -p third_party
    git clone -q --depth 1 --branch "$VERSION" https://github.com/Mbed-TLS/mbedtls "$SRC"
fi
KIND="${1:-pc}"
OUT="build/$KIND-mbedtls.a"
[ -f "$OUT" ] && exit 0
if [ "$KIND" = android ]; then
    TC="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64"
    CC="$TC/bin/aarch64-linux-android29-clang -fPIC -ffunction-sections -fdata-sections"; AR="$TC/bin/llvm-ar"
else
    CC="cc"; AR="ar"
fi
OBJ="build/$KIND-mbedtls-obj"
rm -rf "$OBJ"; mkdir -p "$OBJ"
for f in "$SRC"/library/*.c; do echo "$f"; done |
    xargs -P 4 -I{} sh -c "$CC -O2 -w -I$SRC/include -I$SRC/library -c {} -o $OBJ/\$(basename {} .c).o"
$AR rcs "$OUT" "$OBJ"/*.o
rm -rf "$OBJ"
