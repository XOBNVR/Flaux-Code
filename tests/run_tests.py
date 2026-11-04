#!/usr/bin/env python3
"""Flaux test suite.

    python3 tests/run_tests.py            run everything
    python3 tests/run_tests.py lists      only tests whose name contains "lists"

tests/programs/NAME.flx   is run, and its output must match NAME.out exactly.
                          If NAME.in exists it is used as the typed input.
examples/**/*.flx         must compile (they're used in the README).
flauxc/studio/course.json the course: up to date, and its answers right (see "course" below).
tests/graphics/NAME.flx   graphics programs, run without a real screen. Header
                          lines: `# frames: N` (stop after N frames), `# hold: keys`
                          (pretend these keys are held), `# buttons: a,b` (pretend
                          these controller buttons are held), `# mouse: x,y` (pretend
                          the mouse is there in the real window), `# mouse_move: dx,dy`
                          (pretend the mouse moves that far every frame), `# screen: WxH`
                          (pretend the screen is that big, for fullscreen), `# pixel: x y #rrggbb`
                          (that pixel of the last frame must be this colour), `# near: x y #rrggbb`
                          (within a few shades: lit 3D), `# canvas: WxH`
                          and `# full_pixel: x y #rrggbb` (the canvas's real size, and a pixel
                          of it: fullscreen draws at the screen's resolution), `# again_with:
                          NAME=value` (run it again with that set, same checks), or
                          `# expect: ...` for programs that must fail. If NAME.out
                          exists, what the program says must match it.
                          3D tests with `# screen:` run on a pretend X screen (Xvfb) on
                          Linux, since the "offscreen" driver can't make a 3D window
                          bigger; they're skipped where that can't be done (Windows,
                          or no Xvfb: GitHub's Linux computers have it).
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
import time

# C code that newer compilers refuse (GCC 14 and later, like Windows') must fail here too
os.environ["FLAUX_STRICT_C"] = "1"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)
sys.path.insert(0, HERE)   # for blocks_roundtrip (Windows' embeddable Python leaves the script's folder out)

from flauxc.cli import BUNDLED_MESA, BUNDLED_SDL, EXE, compile_c  # noqa: E402
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


def start_xvfb():
    """A pretend X screen (Xvfb) for 3D tests that change the window's size, or False."""
    if os.name == "nt" or sys.platform == "darwin" or not shutil.which("Xvfb"):
        return False
    for n in range(91, 120):
        if os.path.exists(f"/tmp/.X{n}-lock"):
            continue
        proc = subprocess.Popen(["Xvfb", f":{n}", "-screen", "0", "1920x1080x24", "-nolisten", "tcp"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(50):
            if os.path.exists(f"/tmp/.X11-unix/X{n}") or proc.poll() is not None:
                break
            time.sleep(0.1)
        if proc.poll() is None:
            proc.display = f":{n}"
            return proc
    return False


def bmp_size(path):
    """The width and height of a .bmp picture."""
    data = open(path, "rb").read()
    return int.from_bytes(data[18:22], "little", signed=True), abs(int.from_bytes(data[22:26], "little", signed=True))


def graphics_checks(path, header, code, out, err, shot, full_shot):
    """What's wrong with a graphics test's run (see the top of this file)."""
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
    for spec in header.get("near", []):   # lit 3D: within a few shades
        x, y, want = spec.split()
        got = bmp_pixel(shot, int(x), int(y)) if code == 0 else "?"
        if not near(got, want):
            problems.append(f"pixel {x},{y} is {got}, expected about {want}")
    for spec in header.get("full_near", []):
        x, y, want = spec.split()
        got = bmp_pixel(full_shot, int(x), int(y)) if code == 0 else "?"
        if not near(got, want):
            problems.append(f"canvas pixel {x},{y} is {got}, expected about {want}")
    for spec in header.get("canvas", []):
        got = "x".join(map(str, bmp_size(full_shot))) if code == 0 else "?"
        if got != spec:
            problems.append(f"the canvas is {got}, expected {spec}")
    for spec in header.get("full_pixel", []):
        x, y, want = spec.split()
        got = bmp_pixel(full_shot, int(x), int(y)) if code == 0 else "?"
        if got != want.lower():
            problems.append(f"canvas pixel {x},{y} is {got}, expected {want}")
    return problems


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
        # (on Windows the .exe is all you double-click; elsewhere play.sh starts it)
        launcher = f"game/game{EXE}" if os.name == "nt" else "game/play.sh"
        if f"game/game{EXE}" not in names or launcher not in names or "game/Play game.bat" in names:
            problems.append(f"export went wrong: {r} {names}")
        else:   # and the program in it runs on its own
            unzipped = os.path.join(home, "unzipped")
            zipfile.ZipFile(r["file"]).extractall(unzipped)
            exe = os.path.join(unzipped, "game", f"game{EXE}")
            os.chmod(exe, 0o755)
            ran = subprocess.run([exe], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
            if ran.returncode != 0 or ran.stdout != "hi\n":
                problems.append(f"the exported program didn't run: exit {ran.returncode}, said {ran.stdout!r} {ran.stderr!r}")
        # Mac export (not a .dmg): the otool reader keeps SDL2 and drops the system libraries,
        from flauxc.cli import _parse_otool
        otool = ("game:\n\t/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib (compatibility version 16.0.0)\n"
                 "\t/System/Library/Frameworks/OpenGL.framework/OpenGL (compatibility version 1.0.0)\n"
                 "\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0)\n"
                 "\t@rpath/other.dylib (compatibility version 1.0.0)\n")
        if _parse_otool(otool) != ["/opt/homebrew/opt/sdl2/lib/libSDL2-2.0.0.dylib"]:
            problems.append(f"the Mac library reader (otool) is wrong: {_parse_otool(otool)}")
        # and on a Mac the zip gets a double-clickable play.command, with SDL2 bundled next to the
        # program (install_name_tool); here we pretend to be a Mac and stand in for the Mac tools.
        # (Not on Windows: pretending there would leave cli.WINDOWS/.exe set, a state no real Mac is in.)
        if sdl_installed() and os.name != "nt":
            from flauxc.studio import server as _server
            from flauxc import cli as _cli
            real_platform, real_fn = _server.sys.platform, _cli.macos_selfcontain

            def fake_selfcontain(exe, out_dir):      # what macos_selfcontain does, without the Mac tools
                prog = os.path.join(out_dir, os.path.basename(exe))
                shutil.copyfile(exe, prog)
                lib = os.path.join(out_dir, "libSDL2-2.0.0.dylib")
                open(lib, "wb").write(b"fake dylib")
                return prog, [lib]
            try:
                _server.sys.platform = "darwin"
                _cli.macos_selfcontain = fake_selfcontain
                rm = studio.export('window(200, 100)\nclear("red")\nshow()\n',
                                   os.path.join(home, "mac_game.flx"), show=False)
                mac_names = zipfile.ZipFile(rm["file"]).namelist() if rm.get("ok") else []
                readme = zipfile.ZipFile(rm["file"]).read("mac_game/README.txt").decode() if rm.get("ok") else ""
                want_mac = ["mac_game/mac_game", "mac_game/libSDL2-2.0.0.dylib", "mac_game/play.command"]
                if any(n not in mac_names for n in want_mac) or "double-click play.command" not in readme:
                    problems.append(f"Mac export went wrong: {rm} {mac_names}")
            finally:
                _server.sys.platform, _cli.macos_selfcontain = real_platform, real_fn
        # Windows: a game that never says anything is a window-only program (no console window
        # behind it), and it still runs
        if os.name == "nt" and sdl_installed():
            r = studio.export('window(200, 100)\nclear("red")\nshow()\n', os.path.join(home, "quiet.flx"), show=False)
            if not r.get("ok"):
                problems.append(f"exporting a quiet game went wrong: {r}")
            else:
                zipfile.ZipFile(r["file"]).extractall(os.path.join(home, "quiet-unzipped"))
                exe = os.path.join(home, "quiet-unzipped", "quiet", f"quiet{EXE}")
                data = open(exe, "rb").read()
                pe = int.from_bytes(data[0x3C:0x40], "little")
                subsystem = int.from_bytes(data[pe + 0x5C:pe + 0x5E], "little")   # 2: a window, 3: a console
                shot = os.path.join(home, "quiet.bmp")
                ran = subprocess.run([exe], capture_output=True, timeout=60, env={
                    **os.environ, "SDL_VIDEODRIVER": "dummy", "SDL_AUDIODRIVER": "dummy", "FLAUX_FAST": "1",
                    "FLAUX_FRAMES": "2", "FLAUX_SCREENSHOT": shot})
                if subsystem != 2 or ran.returncode != 0 or not os.path.isfile(shot):
                    problems.append(f"the exported quiet game: subsystem {subsystem} (2 wanted), exit {ran.returncode}, "
                                    f"screenshot {os.path.isfile(shot)}")
        # a 3D game takes its model, the model's .mtl and the picture that names along
        # (and on Windows Mesa, for computers without a good graphics card)
        if sdl_installed():
            for f in ("house.obj", "house.mtl", "checker.png"):
                shutil.copyfile(os.path.join(HERE, "graphics3d", f), os.path.join(home, f))
            r = studio.export('window(300, 200)\nmodel("house.obj", 0, 0, 0, 2)\nshow()\n',
                              os.path.join(home, "house_game.flx"), show=False)
            names = zipfile.ZipFile(r["file"]).namelist() if r.get("ok") else []
            want = [f"house_game/house_game{EXE}", "house_game/house.obj", "house_game/house.mtl"]
            if "map_Kd" in open(os.path.join(home, "house.mtl")).read():
                want.append("house_game/checker.png")
            if os.name == "nt" and os.path.isdir(BUNDLED_MESA):
                want.append("house_game/mesa/opengl32.dll")
            if any(n not in names for n in want):
                problems.append(f"3D export went wrong: {r} {names}, wanted {want}")
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
        xvfb = None   # the pretend X screen, started when a test needs it
        for path in graphics:
            name = os.path.relpath(path, HERE).replace(os.sep, "/")
            lines = open(path, encoding="utf-8").read().splitlines()
            header = {}
            for l in lines:
                if l.startswith("# ") and ":" in l:
                    key, _, value = l[2:].partition(":")
                    header.setdefault(key.strip(), []).append(value.strip())
            shot = os.path.join(tmp, os.path.basename(path)[:-4] + ".bmp")
            on_x = path in three_d and "screen" in header
            if on_x and xvfb is None:
                xvfb = start_xvfb()
            if on_x and not xvfb:
                print(f"(skipping {name}: 3D in fullscreen is tested on Linux with Xvfb)")
                continue
            # 3D draws with OpenGL: on a test computer without a screen, SDL's
            # "offscreen" driver and Mesa's software graphics card do that
            # (on Windows, a real window: Flaux falls back to Mesa by itself
            # when there's no graphics card, like on GitHub's test computers)
            driver = ("windows" if os.name == "nt" else "offscreen") if path in three_d else "dummy"
            full_shot = os.path.join(tmp, os.path.basename(path)[:-4] + "-full.bmp")
            env = {"SDL_VIDEODRIVER": driver,
                   "SDL_AUDIODRIVER": "dummy", "FLAUX_FAST": "1",
                   "FLAUX_FRAMES": header.get("frames", ["1"])[0], "FLAUX_SCREENSHOT": shot,
                   "FLAUX_HOLD_KEYS": ",".join(header.get("hold", [])),
                   "FLAUX_HOLD_BUTTONS": ",".join(header.get("buttons", [])),
                   "FLAUX_HOLD_MOUSE": header.get("hold_mouse", [""])[0],
                   "FLAUX_MOUSE": header.get("mouse", [""])[0],
                   "FLAUX_MOUSE_MOVE": header.get("mouse_move", [""])[0],
                   "FLAUX_SCREEN": header.get("screen", [""])[0], "FLAUX_SCREENSHOT_FULL": full_shot}
            if on_x:
                env.update({"SDL_VIDEODRIVER": "x11", "DISPLAY": xvfb.display})
            code, out, err = build_and_run(path, "", tmp, env)
            if "expect" in header:
                missing = [e for e in header["expect"] if e not in err]
                ok = code != 0 and not missing
                detail = f"exit {code}, missing {missing}\n{err}"
            else:
                problems = graphics_checks(path, header, code, out, err, shot, full_shot)
                # `# again_with: NAME=value`: the same checks again with that set (another way of drawing)
                for extra in header.get("again_with", []):
                    key, _, value = extra.partition("=")
                    code, out, err = build_and_run(path, "", tmp, {**env, key.strip(): value.strip()})
                    problems += [f"({extra}) {p}" for p in graphics_checks(path, header, code, out, err, shot, full_shot)]
                ok, detail = not problems, "\n".join(problems)
            if ok:
                passed += 1
            else:
                failures.append((name, detail))
        if xvfb:
            xvfb.terminate()
            xvfb.wait()

    # Flaux Studio's helper: checking, Apply fix, running with typed input, saving.
    if pattern in "studio":
        problems = studio_checks()
        if problems:
            failures.append(("studio", "\n".join(problems)))
        else:
            passed += 1

    # The course (flauxc/studio/course.json): it's up to date with course_units.py and
    # course_questions.py, every answer that needs no running is right, the challenges'
    # solutions do them, and a sample of the "what does it show?" programs say their answer.
    # (All of them run in `python3 -m flauxc.studio.make_course --verify`, on Linux in CI.)
    if pattern in "course":
        from flauxc.studio import make_course
        found = [] if make_course.up_to_date() else ["course.json is out of date: run python3 -m flauxc.studio.make_course"]
        questions = make_course.collect()
        ran = [q for q in questions if q["check"] in ("run", "ends")]
        sample = [q for q in questions if q["check"] not in ("run", "ends", None)] + ran[::max(1, len(ran) // 25)]
        found += make_course.check_all(sample, workers=4)
        if len(questions) < 1000:
            found.append(f"only {len(questions)} questions")
        if found:
            failures.append(("course", "\n\n".join(found)))
        else:
            passed += 1

    # The desktop app's Blocks mode: every example and test program goes into blocks
    # (blocks.py) and back to code (studio/blockmodel.py), meaning exactly the same.
    if pattern in "blocks":
        from blocks_roundtrip import meaning
        from flauxc.lexer import tokenize
        from flauxc.parser import parse
        from flauxc.studio import blocks as code_to_blocks, blockmodel
        programs = sorted(glob.glob(os.path.join(ROOT, "examples", "**", "*.flx"), recursive=True)
                          + glob.glob(os.path.join(HERE, "programs", "*.flx"))
                          + [p for p in glob.glob(os.path.join(HERE, "graphics", "*.flx")) if os.path.exists(p[:-4] + ".out")])
        wrong = []
        for path in programs:
            original = open(path, encoding="utf-8").read()
            try:
                made, _, problems = blockmodel.generate(blockmodel.from_workspace(code_to_blocks.to_blocks(original)))
                compile_source(made, os.path.basename(path), os.path.dirname(path))
                if problems or meaning(parse(tokenize(original), original)) != meaning(parse(tokenize(made), made)):
                    wrong.append(f"{os.path.relpath(path, ROOT)} means something else as blocks:\n{made}")
            except FlauxError as e:
                wrong.append(f"{os.path.relpath(path, ROOT)}: line {e.line}: {e.message}")
        if wrong:
            failures.append(("blocks (desktop app)", "\n".join(wrong)))
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
