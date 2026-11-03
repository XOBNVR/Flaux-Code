#!/usr/bin/env python3
"""Flaux test suite.

    python3 tests/run_tests.py            run everything
    python3 tests/run_tests.py lists      only tests whose name contains "lists"

tests/programs/NAME.flx   is run, and its output must match NAME.out exactly.
                          If NAME.in exists it is used as the typed input.
tests/errors/NAME.flx     must fail. Every `# expect: ...` line at the top
                          must appear in the error message. A `# stdin: ...`
                          line gives the typed input.
"""

import glob
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, ROOT)

from flauxc.cli import compile_c  # noqa: E402
from flauxc.compiler import compile_source  # noqa: E402
from flauxc.errors import FlauxError  # noqa: E402


def build_and_run(path, stdin, tmp):
    """Returns (exit code, stdout, stderr) like running `flaux run`."""
    source = open(path, encoding="utf-8").read()
    name = os.path.basename(path)
    try:
        c_code = compile_source(source, name)
    except FlauxError as e:
        return 1, "", e.render(name, source)
    exe = os.path.join(tmp, name[:-4])
    compile_c(c_code, exe)
    r = subprocess.run([exe], input=stdin, capture_output=True, text=True, timeout=60)
    return r.returncode, r.stdout, r.stderr


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

    for name, detail in failures:
        print(f"FAIL {name}\n{detail}\n")
    print(f"{passed} passed, {len(failures)} failed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
