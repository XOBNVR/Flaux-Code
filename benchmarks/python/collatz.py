best = best_start = 0
for start in range(1, 3_000_001):
    n, steps = start, 0
    while n != 1:
        n = n // 2 if n % 2 == 0 else 3 * n + 1
        steps += 1
    if steps > best:
        best, best_start = steps, start
print(best_start, best)
