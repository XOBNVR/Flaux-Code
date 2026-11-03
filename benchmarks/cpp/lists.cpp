#include <cstdint>
#include <iostream>
#include <vector>
int main() {
    std::vector<int64_t> nums;
    for (int64_t i = 1; i <= 30000000; i++) nums.push_back(i % 1000);
    int64_t total = 0;
    for (int64_t n : nums) total += n;
    std::cout << total << "\n";
}
