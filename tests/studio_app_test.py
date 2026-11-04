"""Tests Flaux Studio, the desktop app (flauxc/studio/app.py), in a real window:
a mistake is found and explained, Apply fix fixes it, Run runs it, Save saves it.

    python tests/studio_app_test.py

Needs Python with Tk. On Linux without a screen it uses a pretend one (Xvfb).
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def main():
    try:
        import tkinter  # noqa: F401
    except ImportError:
        print("(skipping the Studio app test: this Python has no Tk)")
        return 0
    env = dict(os.environ)
    xvfb = None
    if sys.platform.startswith("linux") and not env.get("DISPLAY"):
        if not shutil.which("Xvfb"):
            print("(skipping the Studio app test: no screen, and no Xvfb)")
            return 0
        xvfb = subprocess.Popen(["Xvfb", ":95", "-screen", "0", "1280x900x24"], stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        env["DISPLAY"] = ":95"
    with tempfile.TemporaryDirectory() as tmp:
        home, report = os.path.join(tmp, "programs"), os.path.join(tmp, "report.json")
        os.makedirs(home)
        env.update(FLAUX_STUDIO_TEST=report, XDG_CONFIG_HOME=os.path.join(tmp, "config"),
                   APPDATA=os.path.join(tmp, "config"))
        code = ("import sys; sys.path.insert(0, %r); from flauxc.studio import app; app.main(None, home=%r)"
                % (ROOT, home))
        try:
            r = subprocess.run([sys.executable, "-c", code], env=env, timeout=180, capture_output=True, text=True)
        finally:
            if xvfb:
                xvfb.terminate()
        if not os.path.exists(report):
            print(f"FAIL  the Studio app didn't finish its test (exit {r.returncode})\n{r.stdout}{r.stderr}")
            return 1
        got = json.load(open(report, encoding="utf-8"))
    checks = [
        (got.get("problem") == "I don't know what `scroe` is." and got.get("problem_line") == 5,
         f"a mistake is found while typing ({got.get('problem')!r}, line {got.get('problem_line')})"),
        (got.get("fixed_code", "").endswith("say score\n"), "Apply fix fixes it"),
        (got.get("output", "").startswith("hi hi hi 10") and "Finished" in got.get("output", ""),
         f"Run runs it ({got.get('output')!r})"),
        (got.get("saved"), "Save saves it"),
        (got.get("blocks_mode") == "blocks" and got.get("blocks_code") == got.get("fixed_code"),
         "Blocks mode shows the program as blocks (and the code stays the same)"),
        (got.get("blocks_added", "").endswith('say "made with blocks"\n'),
         f"a block added in Blocks mode is in the code ({got.get('blocks_added', '')[-40:]!r})"),
        (got.get("blocks_dragged", "").endswith('say "made with blocks"\nsay "Hello!"\n'),
         f"a block dragged out of the menus with the mouse snaps under the last one "
         f"({got.get('blocks_dragged', '')[-40:]!r})"),
        (got.get("night") is True, "Night mode"),
    ]
    for ok, what in checks:
        print(("OK    " if ok else "FAIL  ") + what)
    return 0 if all(ok for ok, _ in checks) else 1


if __name__ == "__main__":
    sys.exit(main())
