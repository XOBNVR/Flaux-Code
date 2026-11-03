#include <cstdint>
#include <iostream>
#include <vector>
int main() {
    const int64_t limit = 50000000;
    std::vector<char> is_prime(limit + 1, 1);
    is_prime[0] = is_prime[1] = 0;
    int64_t count = 0;
    for (int64_t i = 2; i <= limit; i++) {
        if (is_prime[i]) {
            count++;
            for (int64_t j = i * i; j <= limit; j += i) is_prime[j] = 0;
        }
    }
    std::cout << count << "\n";
}
