#!/usr/bin/env python3
"""Checks the web version of Flaux: every test program is turned into
JavaScript (flauxc/jsgen.py) and run with Node on the web runtime
(flauxc/web/fx.js). The output must match the native version's exactly.
Graphics tests with a NAME.out file (tests/graphics) run too, in a pretend
window that draws nothing, with the same `# frames:` and `# mouse_move:` headers.

    python3 tests/js_tests.py

Needs Node.js (skipped with a message if it isn't installed).
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

from flauxc.errors import FlauxError  # noqa: E402
from flauxc.jsgen import compile_js  # noqa: E402


def run(path, stdin, tmp, env=None):
    source = open(path, encoding="utf-8").read()
    try:
        js = compile_js(source, os.path.basename(path))
    except FlauxError as e:
        return 1, "", e.render(os.path.basename(path), source)
    js_file = os.path.join(tmp, os.path.basename(path) + ".js")
    with open(js_file, "w", encoding="utf-8") as fh:
        fh.write(js)
    r = subprocess.run(["node", os.path.join(HERE, "js_runner.js"), js_file, path], input=stdin,
                       env={**os.environ, **(env or {})}, capture_output=True, text=True, encoding="utf-8", timeout=120)
    return r.returncode, r.stdout, r.stderr


def main():
    if not shutil.which("node"):
        print("(skipping the web version tests: Node.js isn't installed)")
        return 0
    failures, passed = [], 0
    with tempfile.TemporaryDirectory() as tmp:
        for path in sorted(glob.glob(os.path.join(HERE, "programs", "*.flx"))):
            expected = open(path[:-4] + ".out", encoding="utf-8").read()
            stdin = open(path[:-4] + ".in").read() if os.path.exists(path[:-4] + ".in") else ""
            code, out, err = run(path, stdin, tmp)
            if code != 0 or out != expected:
                failures.append((path, f"exit {code}\n--- expected\n{expected}--- got\n{out}{err}"))
            else:
                passed += 1
        # programs that must stop with a problem while running
        for path in sorted(glob.glob(os.path.join(HERE, "errors", "runtime_*.flx"))):
            lines = open(path, encoding="utf-8").read().splitlines()
            expects = [l[len("# expect:"):].strip() for l in lines if l.startswith("# expect:")]
            stdin = "".join(l[len("# stdin:"):].strip() + "\n" for l in lines if l.startswith("# stdin:"))
            code, out, err = run(path, stdin, tmp)
            missing = [e for e in expects if e not in err]
            if code == 0 or missing:
                failures.append((path, f"exit {code}, missing {missing}\n{err}"))
            else:
                passed += 1
        # graphics programs that say things (a pretend window, see js_runner.js)
        for path in sorted(glob.glob(os.path.join(HERE, "graphics", "*.flx"))):
            if not os.path.exists(path[:-4] + ".out"):
                continue
            expected = open(path[:-4] + ".out", encoding="utf-8").read()
            header = {}
            for l in open(path, encoding="utf-8").read().splitlines():
                if l.startswith("# ") and ":" in l:
                    key, _, value = l[2:].partition(":")
                    header.setdefault(key.strip(), value.strip())
            env = {"FLAUX_FRAMES": header.get("frames", "1"), "FLAUX_MOUSE_MOVE": header.get("mouse_move", ""),
                   "FLAUX_HOLD_MOUSE": header.get("hold_mouse", "")}
            code, out, err = run(path, "", tmp, env)
            if code != 0 or out != expected:
                failures.append((path, f"exit {code}\n--- expected\n{expected}--- got\n{out}{err}"))
            else:
                passed += 1
    for path, detail in failures:
        print(f"FAIL {os.path.relpath(path, ROOT)}\n{detail}\n")
    print(f"web version: {passed} passed, {len(failures)} failed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
