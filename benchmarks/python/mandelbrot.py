size, max_iter, total = 1200, 200, 0
for py in range(size):
    y0 = py / size * 2.0 - 1.0
    for px in range(size):
        x0 = px / size * 3.0 - 2.0
        x = y = 0.0
        i = 0
        while i < max_iter and x * x + y * y <= 4.0:
            x, y = x * x - y * y + x0, 2.0 * x * y + y0
            i += 1
        total += i
print(total)
