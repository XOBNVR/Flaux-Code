#include <cstdint>
#include <iostream>
int main() {
    const int64_t size = 1200, max_iter = 200;
    int64_t total = 0;
    for (int64_t py = 0; py < size; py++) {
        double y0 = (double)py / size * 2.0 - 1.0;
        for (int64_t px = 0; px < size; px++) {
            double x0 = (double)px / size * 3.0 - 2.0;
            double x = 0, y = 0;
            int64_t i = 0;
            while (i < max_iter && x * x + y * y <= 4.0) {
                double xt = x * x - y * y + x0;
                y = 2.0 * x * y + y0;
                x = xt;
                i++;
            }
            total += i;
        }
    }
    std::cout << total << "\n";
}
