# Benchmark results

Machine: x86_64, 4 cores, Linux. Wall-clock time including start-up, best of 7 (Python: 1 run).

| Benchmark | Tests | Flaux | Rust | C++ | C# |
|---|---|---|---|---|---|
| fib | recursive function calls | 0.22s | 0.37s | 0.19s | 0.61s |
| sieve | loops + list reads/writes | 0.34s | 0.38s | 0.33s | 0.41s |
| collatz | whole-number maths | 0.70s | 0.62s | 0.88s | 1.05s |
| mandelbrot | decimal maths | 0.28s | 0.27s | 0.28s | 0.31s |
| lists | growing a big list | 0.18s | 0.20s | 0.35s | 0.46s |

Flaux vs others (how many times faster Flaux is; below 1.0 means Flaux is slower):

| Benchmark | vs Rust | vs C++ | vs C# |
|---|---|---|---|
| fib | 1.67x | 0.89x | 2.77x |
| sieve | 1.14x | 0.97x | 1.21x |
| collatz | 0.88x | 1.26x | 1.50x |
| mandelbrot | 0.97x | 1.02x | 1.13x |
| lists | 1.16x | 1.96x | 2.60x |
