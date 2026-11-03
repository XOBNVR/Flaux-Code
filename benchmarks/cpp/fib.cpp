#include <cstdint>
#include <iostream>
int64_t fib(int64_t n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int main() { std::cout << fib(40) << "\n"; }
