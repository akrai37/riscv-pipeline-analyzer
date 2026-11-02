// Recursive Fibonacci: dominated by calls, returns and stack traffic.
// Without a return address stack every `ret` (jalr) is a misprediction.
// At -O2 clang keeps one of the two recursive calls and turns the other into
// a loop, so there are about fib(n) calls instead of 2 * fib(n).

#include "rt.h"

__attribute__((noinline)) static uint32_t fib(uint32_t n) {
  return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

int main(void) {
  volatile uint32_t n = 22;  // keep the call from being constant-folded
  return rt_check("fib(22)", fib(n), 17711);
}
