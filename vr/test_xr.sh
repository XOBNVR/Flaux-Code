#!/bin/sh
# Runs the headset code (src/xr_main.c) on this PC against a real OpenXR runtime (Monado): it
# catches mistakes in how OpenXR is used, without a headset.
#   ./test_xr.sh [frames]
# Needs: libopenxr-dev, libopenxr1-monado, monado-service, openxr-layer-corevalidation, Mesa EGL/GLES.
set -e
cd "$(dirname "$0")"
mkdir -p build
cc -std=gnu11 -O1 -g -DFLAUX_PC_XR -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
   -fsanitize=address,undefined \
   src/gfx.c src/ui.c src/scene.c src/net.c src/editor.c src/screens.c src/copilot.c src/xr_main.c \
   -o build/flauxvr_xr -lopenxr_loader -lEGL -lGLESv2 -lm
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/tmp/flaux-xdg}"
mkdir -p "$XDG_RUNTIME_DIR"; chmod 700 "$XDG_RUNTIME_DIR"
rm -rf build/xr_data
if ! pgrep -x monado-service >/dev/null; then
    rm -f "$XDG_RUNTIME_DIR"/monado_comp_ipc
    (sleep 100000 | XRT_COMPOSITOR_NULL=1 monado-service >build/monado.log 2>&1) &     # it wants a live stdin
    sleep 3
fi
FLAUX_NO_VALIDATION=1 ASAN_OPTIONS=detect_leaks=0 ./build/flauxvr_xr build/xr_data "${1:-200}" build/xr_eye.ppm
