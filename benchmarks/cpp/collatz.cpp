#include <cstdint>
#include <iostream>
int main() {
    int64_t best = 0, best_start = 0;
    for (int64_t start = 1; start <= 3000000; start++) {
        int64_t n = start, steps = 0;
        while (n != 1) {
            if (n % 2 == 0) n = n / 2; else n = 3 * n + 1;
            steps++;
        }
        if (steps > best) { best = steps; best_start = start; }
    }
    std::cout << best_start << " " << best << "\n";
}
