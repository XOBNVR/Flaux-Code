limit = 50_000_000
is_prime = [True] * (limit + 1)
is_prime[0] = is_prime[1] = False
count = 0
for i in range(2, limit + 1):
    if is_prime[i]:
        count += 1
        for j in range(i * i, limit + 1, i):
            is_prime[j] = False
print(count)
