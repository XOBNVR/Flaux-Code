#!/bin/sh
# Builds the app for this computer and runs it off-screen, saving pictures of every screen.
#   ./test.sh [output dir] [script]        (needs clang, Mesa's EGL/GLES and Python's Pillow)
set -e
cd "$(dirname "$0")"
OUT="${1:-build/shots}"
rm -rf "$OUT"/data_*
mkdir -p build "$OUT"
cc -std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -fsanitize=address,undefined \
   src/gfx.c src/ui.c src/scene.c src/net.c src/editor.c src/screens.c src/copilot.c src/desktop_main.c \
   -o build/flauxvr_test -lEGL -lGLESv2 -lm
if [ $# -gt 0 ]; then shift; fi
EGL_PLATFORM=surfaceless ASAN_OPTIONS=detect_leaks=0 ./build/flauxvr_test assets "$OUT" "$@"
python3 - "$OUT" <<'PY'
import glob, os, sys
from PIL import Image
for p in glob.glob(os.path.join(sys.argv[1], "*.ppm")):
    Image.open(p).save(p[:-4] + ".png"); os.remove(p)
PY
