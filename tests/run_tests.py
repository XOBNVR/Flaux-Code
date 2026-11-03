#!/usr/bin/env python3
"""Flaux test suite.

    python3 tests/run_tests.py            run everything
    python3 tests/run_tests.py lists      only tests whose name contains "lists"

tests/programs/NAME.flx   is run, and its output must match NAME.out exactly.
                          If NAME.in exists it is used as the typed input.
examples/**/*.flx         must compile (they're used in the README).
tests/graphics/NAME.flx   graphics programs, run without a real screen. Header
                          lines: `# frames: N` (stop after N frames), `# hold: keys`
                          (pretend these keys are held), `# buttons: a,b` (pretend
                          these controller buttons are held), `# pixel: x y #rrggbb`
                          (that pixel of the last frame must be this colour), or
                          `# expect: ...` for programs that must fail.
                          Skipped if SDL2 isn't installed.
tests/graphics3d/NAME.flx the same, for 3D and shaders (drawn with OpenGL, so they
                          need Mesa's software OpenGL on a computer without a
                          screen). `# near: x y #rrggbb` allows a few shades either
                          way. FLAUX_SKIP_3D=1 skips them.
tests/errors/NAME.flx     must fail. Every `# expect: ...` line at the top
                          must appear in the error message. A `# stdin: ...`
                          line gives the typed input.
"""

import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from flauxc.cli import BUNDLED_SDL, EXE, compile_c  # noqa: E402
from flauxc.compiler import compile_source  # noqa: E402
from flauxc.errors import FlauxError  # noqa: E402


def sdl_installed():
    if os.path.isdir(BUNDLED_SDL) or shutil.which("sdl2-config"):
        return True
    return bool(shutil.which("pkg-config")) and \
        subprocess.run(["pkg-config", "--exists", "sdl2"]).returncode == 0


def bmp_pixel(path, x, y):
    """The colour of one pixel of a .bmp screenshot, as "#rrggbb"."""
    data = open(path, "rb").read()
    offset = int.from_bytes(data[10:14], "little")
    width = int.from_bytes(data[18:22], "little", signed=True)
    height = int.from_bytes(data[22:26], "little", signed=True)
    bpp = int.from_bytes(data[28:30], "little")
    row_size = (bpp * width + 31) // 32 * 4
    row = height - 1 - y if height > 0 else y
    i = offset + row * row_size + x * (bpp // 8)
    return f"#{data[i + 2]:02x}{data[i + 1]:02x}{data[i]:02x}"


def near(got, want, within=24):
    """True if two "#rrggbb" colours are within a few shades of each other."""
    if not got.startswith("#"):
        return False
    a = [int(got[i:i + 2], 16) for i in (1, 3, 5)]
    b = [int(want[i:i + 2], 16) for i in (1, 3, 5)]
    return all(abs(x - y) <= within for x, y in zip(a, b))


def build_and_run(path, stdin, tmp, env=None):
    """Returns (exit code, stdout, stderr) like running `flaux run`."""
    source = open(path, encoding="utf-8").read()
    name = os.path.basename(path)
    try:
        c_code = compile_source(source, name, os.path.dirname(os.path.abspath(path)))
    except FlauxError as e:
        return 1, "", e.render(name, source)
    exe = os.path.join(tmp, name[:-4] + EXE)
    compile_c(c_code, exe)
    r = subprocess.run([exe], input=stdin, capture_output=True, text=True, timeout=60,
                       encoding="utf-8", errors="replace", env={**os.environ, **(env or {})})
    return r.returncode, r.stdout, r.stderr


def studio_checks():
    import time
    from flauxc.studio.server import Studio
    problems = []
    with tempfile.TemporaryDirectory() as home:
        studio = Studio(home=home)
        r = studio.check("set score to 10\nsay scroe\n")
        if r["ok"] or r["error"]["fixed_source"] != "set score to 10\nsay score\n":
            problems.append(f"check/apply fix gave {r}")
        r = studio.run('set name to ask("Name?")\nsay "Hi {name}"\nsay 1 / 0\n')
        if not r.get("ok"):
            problems.append(f"run failed: {r}")
        else:
            studio.send_input(r["run"], "Mia")
            out, deadline = {"done": False}, time.time() + 60
            while not out["done"] and time.time() < deadline:
                time.sleep(0.05)
                out = studio.output(r["run"], 0)
            text = "".join(t for _, t in out["chunks"])
            errs = "".join(t for s, t in out["chunks"] if s == "err")
            if "Hi Mia" not in text or "divide by zero" not in errs or text.index("Hi Mia") > text.index("Problem"):
                problems.append(f"run output wrong: {out}")
        saved = studio.save("say 1\n", name="My Game")
        if not os.path.isfile(os.path.join(home, "My_Game.flx")) or saved["name"] != "My_Game.flx":
            problems.append(f"save went wrong: {saved}")
    return problems


def main():
    pattern = sys.argv[1] if len(sys.argv) > 1 else ""
    failures = []
    passed = 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in sorted(glob.glob(os.path.join(HERE, "programs", "*.flx"))):
            name = "programs/" + os.path.basename(path)
            if pattern not in name:
                continue
            expected = open(path[:-4] + ".out", encoding="utf-8").read()
            stdin = open(path[:-4] + ".in").read() if os.path.exists(path[:-4] + ".in") else ""
            code, out, err = build_and_run(path, stdin, tmp)
            if code != 0 or out != expected:
                failures.append((name, f"exit {code}\n--- expected\n{expected}--- got\n{out}{err}"))
            else:
                passed += 1

        for path in sorted(glob.glob(os.path.join(HERE, "errors", "*.flx"))):
            name = "errors/" + os.path.basename(path)
            if pattern not in name:
                continue
            lines = open(path, encoding="utf-8").read().splitlines()
            expects = [l[len("# expect:"):].strip() for l in lines if l.startswith("# expect:")]
            stdin = "".join(l[len("# stdin:"):].strip() + "\n" for l in lines if l.startswith("# stdin:"))
            code, out, err = build_and_run(path, stdin, tmp)
            missing = [e for e in expects if e not in err]
            if code == 0 or missing:
                failures.append((name, f"exit {code}, missing {missing}\n{err}"))
            else:
                passed += 1

        graphics = sorted(glob.glob(os.path.join(HERE, "graphics", "*.flx")) +
                          glob.glob(os.path.join(HERE, "graphics3d", "*.flx")))
        graphics = [p for p in graphics if pattern in os.path.relpath(p, HERE).replace(os.sep, "/")]
        if graphics and not sdl_installed():
            print(f"(skipping {len(graphics)} graphics tests: SDL2 isn't installed)")
            graphics = []
        three_d = [p for p in graphics if os.path.basename(os.path.dirname(p)) == "graphics3d"]
        if three_d and os.environ.get("FLAUX_SKIP_3D"):
            print(f"(skipping {len(three_d)} 3D tests: FLAUX_SKIP_3D is set)")
            graphics = [p for p in graphics if p not in three_d]
        for path in graphics:
            name = os.path.relpath(path, HERE).replace(os.sep, "/")
            lines = open(path, encoding="utf-8").read().splitlines()
            header = {}
            for l in lines:
                if l.startswith("# ") and ":" in l:
                    key, _, value = l[2:].partition(":")
                    header.setdefault(key.strip(), []).append(value.strip())
            shot = os.path.join(tmp, os.path.basename(path)[:-4] + ".bmp")
            # 3D draws with OpenGL: on a test computer without a screen, SDL's
            # "offscreen" driver and Mesa's software graphics card do that
            # (on Windows, a real window: Flaux falls back to Mesa by itself
            # when there's no graphics card, like on GitHub's test computers)
            driver = ("windows" if os.name == "nt" else "offscreen") if path in three_d else "dummy"
            env = {"SDL_VIDEODRIVER": driver,
                   "SDL_AUDIODRIVER": "dummy", "FLAUX_FAST": "1",
                   "FLAUX_FRAMES": header.get("frames", ["1"])[0], "FLAUX_SCREENSHOT": shot,
                   "FLAUX_HOLD_KEYS": ",".join(header.get("hold", [])),
                   "FLAUX_HOLD_BUTTONS": ",".join(header.get("buttons", []))}
            code, out, err = build_and_run(path, "", tmp, env)
            if "expect" in header:
                missing = [e for e in header["expect"] if e not in err]
                ok = code != 0 and not missing
                detail = f"exit {code}, missing {missing}\n{err}"
            else:
                problems = [] if code == 0 else [f"exit {code}: {err}"]
                for spec in header.get("pixel", []):
                    x, y, want = spec.split()
                    got = bmp_pixel(shot, int(x), int(y)) if code == 0 else "?"
                    if got != want.lower():
                        problems.append(f"pixel {x},{y} is {got}, expected {want}")
                for spec in header.get("near", []):   # lit 3D: within a few shades
                    x, y, want = spec.split()
                    got = bmp_pixel(shot, int(x), int(y)) if code == 0 else "?"
                    if not near(got, want):
                        problems.append(f"pixel {x},{y} is {got}, expected about {want}")
                ok, detail = not problems, "\n".join(problems)
            if ok:
                passed += 1
            else:
                failures.append((name, detail))

    # Flaux Studio's helper: checking, Apply fix, running with typed input, saving.
    if pattern in "studio":
        problems = studio_checks()
        if problems:
            failures.append(("studio", "\n".join(problems)))
        else:
            passed += 1

    # Every example and lesson (the README uses them) must still compile.
    for path in sorted(glob.glob(os.path.join(ROOT, "examples", "**", "*.flx"), recursive=True)):
        name = os.path.relpath(path, ROOT)
        if pattern not in name:
            continue
        try:
            compile_source(open(path, encoding="utf-8").read(), os.path.basename(path))
            passed += 1
        except FlauxError as e:
            failures.append((name, e.render(os.path.basename(path), open(path).read())))

    for name, detail in failures:
        print(f"FAIL {name}\n{detail}\n")
        if os.environ.get("GITHUB_ACTIONS"):  # show it on the GitHub page too
            short = detail[:3000].replace("%", "%25").replace("\r", "").replace("\n", "%0A")
            print(f"::error title=FAIL {name}::{short}")
    print(f"{passed} passed, {len(failures)} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
