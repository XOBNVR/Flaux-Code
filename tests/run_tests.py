#!/usr/bin/env python3
"""Flaux test suite.

    python3 tests/run_tests.py            run everything
    python3 tests/run_tests.py lists      only tests whose name contains "lists"

tests/programs/NAME.flx   is run, and its output must match NAME.out exactly.
                          If NAME.in exists it is used as the typed input.
examples/**/*.flx         must compile (they're used in the README).
tests/graphics/NAME.flx   graphics programs, run without a real screen. Header
                          lines: `# frames: N` (stop after N frames), `# hold: keys`
                          (pretend these keys are held), `# mouse: x,y` (pretend
                          the mouse is there in the real window), `# mouse_move: dx,dy`
                          (pretend the mouse moves that far every frame), `# pixel: x y #rrggbb`
                          (that pixel of the last frame must be this colour), or
                          `# expect: ...` for programs that must fail. If NAME.out
                          exists, what the program says must match it.
                          Skipped if SDL2 isn't installed.
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
        # Export: a zip with the built program and a file that starts it
        import zipfile
        r = studio.export('say "hi"\n', None, show=False)
        names = zipfile.ZipFile(r["file"]).namelist() if r.get("ok") else []
        launcher = "game/Play game.bat" if os.name == "nt" else "game/play.sh"
        if f"game/game{EXE}" not in names or launcher not in names:
            problems.append(f"export went wrong: {r} {names}")
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

        graphics = sorted(glob.glob(os.path.join(HERE, "graphics", "*.flx")))
        graphics = [p for p in graphics if pattern in "graphics/" + os.path.basename(p)]
        if graphics and not sdl_installed():
            print(f"(skipping {len(graphics)} graphics tests: SDL2 isn't installed)")
            graphics = []
        for path in graphics:
            name = "graphics/" + os.path.basename(path)
            lines = open(path, encoding="utf-8").read().splitlines()
            header = {}
            for l in lines:
                if l.startswith("# ") and ":" in l:
                    key, _, value = l[2:].partition(":")
                    header.setdefault(key.strip(), []).append(value.strip())
            shot = os.path.join(tmp, os.path.basename(path)[:-4] + ".bmp")
            env = {"SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "FLAUX_FAST": "1",
                   "FLAUX_FRAMES": header.get("frames", ["1"])[0], "FLAUX_SCREENSHOT": shot,
                   "FLAUX_HOLD_KEYS": ",".join(header.get("hold", [])),
                   "FLAUX_MOUSE": header.get("mouse", [""])[0],
                   "FLAUX_MOUSE_MOVE": header.get("mouse_move", [""])[0]}
            code, out, err = build_and_run(path, "", tmp, env)
            if "expect" in header:
                missing = [e for e in header["expect"] if e not in err]
                ok = code != 0 and not missing
                detail = f"exit {code}, missing {missing}\n{err}"
            else:
                problems = [] if code == 0 else [f"exit {code}: {err}"]
                if os.path.exists(path[:-4] + ".out"):
                    expected = open(path[:-4] + ".out", encoding="utf-8").read()
                    if out != expected:
                        problems.append(f"--- expected\n{expected}--- got\n{out}")
                for spec in header.get("pixel", []):
                    x, y, want = spec.split()
                    got = bmp_pixel(shot, int(x), int(y)) if code == 0 else "?"
                    if got != want.lower():
                        problems.append(f"pixel {x},{y} is {got}, expected {want}")
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
