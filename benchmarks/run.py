#!/usr/bin/env python3
"""Race Flaux against C++, C# and Python on the same programs.

Usage:  python3 benchmarks/run.py [--runs 3] [--no-python]

Every program is built with normal release settings:
    Flaux   flaux build              (C backend, gcc -O2)
    C++     g++ -O2
    C#      dotnet build -c Release  (.NET 8)
    Python  python3

Times are wall-clock for the whole process (start-up included), best of
--runs. Results are printed and written to benchmarks/RESULTS.md.
"""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BENCHMARKS = ["fib", "sieve", "collatz", "mandelbrot", "lists"]
DESCRIPTIONS = {
    "fib": "recursive function calls",
    "sieve": "loops + list reads/writes",
    "collatz": "whole-number maths",
    "mandelbrot": "decimal maths",
    "lists": "growing a big list",
}


def sh(cmd, **kw):
    return subprocess.run(cmd, check=True, capture_output=True, text=True, **kw)


def timed(cmd, runs, timeout):
    best, out = None, None
    for _ in range(runs):
        start = time.perf_counter()
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        except subprocess.TimeoutExpired:
            return None, "timeout"
        elapsed = time.perf_counter() - start
        if result.returncode != 0:
            return None, "error: " + result.stderr.strip()[:200]
        out = result.stdout.strip()
        best = elapsed if best is None else min(best, elapsed)
    return best, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--no-python", action="store_true")
    args = ap.parse_args()

    build = tempfile.mkdtemp(prefix="flaux-bench-")
    langs = {}

    print("Building...", flush=True)
    langs["Flaux"] = {}
    for b in BENCHMARKS:
        exe = os.path.join(build, f"flaux_{b}")
        sh([sys.executable, os.path.join(ROOT, "flaux"), "build",
            os.path.join(HERE, "flaux", f"{b}.flx"), "-o", exe])
        langs["Flaux"][b] = [exe]

    langs["C++"] = {}
    for b in BENCHMARKS:
        exe = os.path.join(build, f"cpp_{b}")
        sh(["g++", "-O2", "-o", exe, os.path.join(HERE, "cpp", f"{b}.cpp")])
        langs["C++"][b] = [exe]

    if shutil.which("dotnet"):
        out_dir = os.path.join(build, "cs")
        sh(["dotnet", "build", os.path.join(HERE, "csharp", "Bench.csproj"), "-c", "Release",
            "-o", out_dir, "--nologo", "-v", "q"])
        langs["C#"] = {b: [os.path.join(out_dir, "Bench"), b] for b in BENCHMARKS}
    else:
        print("  (dotnet not found, skipping C#)")

    if not args.no_python:
        langs["Python"] = {b: [sys.executable, os.path.join(HERE, "python", f"{b}.py")]
                           for b in BENCHMARKS}

    results = {}
    for b in BENCHMARKS:
        for lang, cmds in langs.items():
            runs = 1 if lang == "Python" else args.runs
            t, out = timed(cmds[b], runs, timeout=300)
            results[(lang, b)] = (t, out)
            shown = f"{t:.2f}s" if t is not None else out
            print(f"  {b:<11} {lang:<7} {shown}", flush=True)

    # check everyone got the same answer
    for b in BENCHMARKS:
        answers = {out for (lang, bb), (t, out) in results.items() if bb == b and t is not None}
        if len(answers) > 1:
            print(f"WARNING: different answers for {b}: {answers}")

    names = list(langs)
    lines = [
        "# Benchmark results",
        "",
        f"Machine: {platform.machine()}, {os.cpu_count()} cores, {platform.system()}. "
        f"Wall-clock time including start-up, best of {args.runs} (Python: 1 run).",
        "",
        "| Benchmark | Tests | " + " | ".join(names) + " |",
        "|---|---|" + "---|" * len(names),
    ]
    for b in BENCHMARKS:
        cells = []
        for lang in names:
            t, out = results[(lang, b)]
            cells.append(f"{t:.2f}s" if t is not None else out)
        lines.append(f"| {b} | {DESCRIPTIONS[b]} | " + " | ".join(cells) + " |")
    lines.append("")
    lines.append("Flaux vs others (how many times faster Flaux is; below 1.0 means Flaux is slower):")
    lines.append("")
    lines.append("| Benchmark | " + " | ".join(f"vs {n}" for n in names if n != "Flaux") + " |")
    lines.append("|---|" + "---|" * (len(names) - 1))
    for b in BENCHMARKS:
        ft = results[("Flaux", b)][0]
        cells = []
        for lang in names:
            if lang == "Flaux":
                continue
            t = results[(lang, b)][0]
            cells.append(f"{t / ft:.2f}x" if (t and ft) else "n/a")
        lines.append(f"| {b} | " + " | ".join(cells) + " |")
    report = "\n".join(lines) + "\n"
    with open(os.path.join(HERE, "RESULTS.md"), "w") as fh:
        fh.write(report)
    print()
    print(report)
    shutil.rmtree(build, ignore_errors=True)


if __name__ == "__main__":
    main()
