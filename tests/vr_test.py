#!/usr/bin/env python3
"""Tests Flaux in a VR headset (OpenXR), with a pretend headset.

    python3 tests/vr_test.py

Needs Linux with Xvfb, Monado (monado-service, the OpenXR runtime with a
pretend "qwerty" headset), its OpenXR loader, and Mesa's Vulkan and OpenGL
(on Ubuntu: apt install xvfb monado-service libopenxr-loader1
libopenxr1-monado mesa-vulkan-drivers). Skipped if they're missing.

It starts a virtual screen and Monado, runs tests/vr/headset.flx in the
headset, and checks the program knew it was in VR, saw the right hand, and
that the left eye's picture shows the red cube.
"""

import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)

from flauxc.cli import compile_c  # noqa: E402
from flauxc.compiler import compile_source  # noqa: E402
from run_tests import bmp_pixel, near  # noqa: E402


def main():
    if not (shutil.which("Xvfb") and shutil.which("monado-service")):
        print("(skipping the VR test: Xvfb or monado-service isn't installed)")
        return 0
    display = ":91"
    procs = []
    with tempfile.TemporaryDirectory() as tmp:
        try:
            procs.append(subprocess.Popen(["Xvfb", display, "-screen", "0", "1280x1024x24"],
                                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            time.sleep(1)
            env = {**os.environ, "DISPLAY": display, "XRT_COMPOSITOR_FORCE_XCB": "1", "QWERTY_ENABLE": "1",
                   "XDG_RUNTIME_DIR": tmp}
            # Monado listens on stdin for commands, so give it one that stays open
            monado = subprocess.Popen(["monado-service"], env=env, stdin=subprocess.PIPE,
                                      stdout=open(os.path.join(tmp, "monado.log"), "w"), stderr=subprocess.STDOUT)
            procs.append(monado)
            for _ in range(100):
                if any(f.startswith("monado") for f in os.listdir(tmp)) or os.path.exists("/tmp/monado_comp_ipc"):
                    break
                time.sleep(0.1)
            time.sleep(2)
            path = os.path.join(HERE, "vr", "headset.flx")
            exe = os.path.join(tmp, "headset")
            compile_c(compile_source(open(path).read(), "headset.flx", os.path.dirname(path)), exe)
            shot = os.path.join(tmp, "shot.bmp")
            run = subprocess.run([exe], env={**env, "SDL_VIDEODRIVER": "x11", "SDL_AUDIODRIVER": "dummy",
                                             "FLAUX_FRAMES": "30", "FLAUX_SCREENSHOT": shot, "FLAUX_VR_DEBUG": "1"},
                                 capture_output=True, text=True, timeout=120)
            problems = []
            if "in VR: true" not in run.stdout:
                problems.append("the program didn't know it was in VR")
            if "hand is near: true" not in run.stdout:
                problems.append("the right hand wasn't where it should be")
            eye = os.path.join(tmp, "shot-vr.bmp")
            if not os.path.exists(eye):
                problems.append("the left eye's picture wasn't saved")
            else:
                data = open(eye, "rb").read()
                w = int.from_bytes(data[18:22], "little", signed=True)
                h = abs(int.from_bytes(data[22:26], "little", signed=True))
                middle = bmp_pixel(eye, w // 2, h // 2)
                if not near(middle, "#951f25", 40):
                    problems.append(f"the left eye sees {middle} in the middle, not the red cube")
            if problems or run.returncode:
                print("FAIL VR:", "; ".join(problems) or f"exit {run.returncode}")
                print(run.stdout, run.stderr[-3000:])
                print(open(os.path.join(tmp, "monado.log")).read()[-2000:])
                return 1
            print("OK    a 3D program runs in a (pretend) VR headset: both eyes, the hands and in_vr()")
            return 0
        finally:
            for p in reversed(procs):
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()


if __name__ == "__main__":
    sys.exit(main())
