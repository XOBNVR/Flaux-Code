#!/bin/sh
# Runs the headset code (src/xr_main.c) on this PC against a real OpenXR runtime (Monado): it
# catches mistakes in how OpenXR is used, without a headset.
#   ./test_xr.sh [frames]
# Needs: libopenxr-dev, libopenxr1-monado, monado-service, openxr-layer-corevalidation, Mesa EGL/GLES.
set -e
cd "$(dirname "$0")"
mkdir -p build
./tools/get_mbedtls.sh pc
cc -std=gnu11 -O1 -g -DFLAUX_PC_XR -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
   -fsanitize=address,undefined -Ithird_party/mbedtls/include \
   src/gfx.c src/ui.c src/scene.c src/net.c src/editor.c src/screens.c src/copilot.c src/assets.c src/xr_main.c \
   -o build/flauxvr_xr -lopenxr_loader build/pc-mbedtls.a -lEGL -lGLESv2 -lm -lpthread
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/flaux-xdg}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
rm -rf build/xr_data
if ! pgrep -x monado-service >/dev/null; then
    rm -f "$XDG_RUNTIME_DIR"/monado_comp_ipc "$XDG_RUNTIME_DIR"/monado.pid      # left behind if it was killed
    (sleep 100000 2>/dev/null | XRT_COMPOSITOR_NULL=1 monado-service >build/monado.log 2>&1) >/dev/null 2>&1 &     # it wants a live stdin
    sleep 3
fi
FLAUX_NO_VALIDATION=1 ASAN_OPTIONS=detect_leaks=0 ./build/flauxvr_xr build/xr_data "${1:-200}" build/xr_eye.ppm
