# Benchmark results

Machine: x86_64, 4 cores, Linux. Wall-clock time including start-up, best of 5 (Python: 1 run).

| Benchmark | Tests | Flaux | Rust | C++ | C# | Python |
|---|---|---|---|---|---|---|
| fib | recursive function calls | 0.16s | 0.31s | 0.16s | 0.46s | 10.73s |
| sieve | loops + list reads/writes | 0.33s | 0.38s | 0.37s | 0.40s | 11.48s |
| collatz | whole-number maths | 0.58s | 0.45s | 0.71s | 0.90s | 44.42s |
| mandelbrot | decimal maths | 0.20s | 0.21s | 0.22s | 0.25s | 18.78s |
| lists | growing a big list | 0.15s | 0.17s | 0.31s | 0.48s | 5.55s |

Flaux vs others (how many times faster Flaux is; below 1.0 means Flaux is slower):

| Benchmark | vs Rust | vs C++ | vs C# | vs Python |
|---|---|---|---|---|
| fib | 1.91x | 1.01x | 2.79x | 65.62x |
| sieve | 1.13x | 1.11x | 1.21x | 34.31x |
| collatz | 0.78x | 1.23x | 1.56x | 77.25x |
| mandelbrot | 1.03x | 1.09x | 1.25x | 94.22x |
| lists | 1.10x | 2.03x | 3.09x | 35.82x |
