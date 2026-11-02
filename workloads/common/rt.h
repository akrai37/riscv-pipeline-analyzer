// Minimal runtime for the workloads. The same sources also build natively
// (see `make host` in workloads/Makefile), which is how the expected values
// the workloads check against were obtained.

#ifndef RT_H
#define RT_H

#include <stddef.h>
#include <stdint.h>

void rt_write(const char* buf, size_t len);
void rt_puts(const char* s);
void rt_put_u32(uint32_t value);
void rt_put_hex(uint32_t value);

// Deterministic xorshift32 generator, so every run sees the same inputs.
void rt_srand(uint32_t seed);
uint32_t rt_rand(void);

// Uniform value in [0, n). Uses a multiply-high instead of `%`, so input
// generation does not fill the stall statistics with divider stalls.
uint32_t rt_rand_below(uint32_t n);

// Prints "<what>: <got> OK" or "<what>: <got> FAIL (expected <expected>)".
// Returns 0 on a match and 1 otherwise, so results can be summed into the
// exit code.
int rt_check(const char* what, uint32_t got, uint32_t expected);

#endif
