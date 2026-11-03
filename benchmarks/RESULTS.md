# Benchmark results

Machine: x86_64, 4 cores, Linux. Wall-clock time including start-up, best of 5 (Python: 1 run).

| Benchmark | Tests | Flaux | C++ | C# | Python |
|---|---|---|---|---|---|
| fib | recursive function calls | 0.17s | 0.17s | 0.57s | 12.15s |
| sieve | loops + list reads/writes | 0.29s | 0.30s | 0.34s | 11.18s |
| collatz | whole-number maths | 0.73s | 0.79s | 0.92s | 54.09s |
| mandelbrot | decimal maths | 0.26s | 0.27s | 0.28s | 16.47s |
| lists | growing a big list | 0.21s | 0.35s | 0.46s | 5.58s |

Flaux vs others (how many times faster Flaux is; below 1.0 means Flaux is slower):

| Benchmark | vs C++ | vs C# | vs Python |
|---|---|---|---|
| fib | 1.04x | 3.40x | 72.34x |
| sieve | 1.02x | 1.15x | 38.34x |
| collatz | 1.08x | 1.26x | 73.91x |
| mandelbrot | 1.03x | 1.06x | 62.55x |
| lists | 1.66x | 2.21x | 26.76x |
