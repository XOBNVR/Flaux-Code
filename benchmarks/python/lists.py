nums = []
for i in range(1, 30_000_001):
    nums.append(i % 1000)
total = 0
for n in nums:
    total += n
print(total)
