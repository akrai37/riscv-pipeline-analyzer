#include "rt.h"

static uint32_t rand_state = 0x12345678u;

void rt_puts(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  rt_write(s, n);
}

void rt_put_u32(uint32_t value) {
  char buf[10];
  size_t i = sizeof buf;
  do {
    buf[--i] = (char)('0' + value % 10);
    value /= 10;
  } while (value != 0);
  rt_write(buf + i, sizeof buf - i);
}

void rt_put_hex(uint32_t value) {
  static const char digits[] = "0123456789abcdef";
  char buf[10] = {'0', 'x'};
  for (int i = 0; i < 8; ++i) {
    buf[2 + i] = digits[(value >> (28 - 4 * i)) & 0xf];
  }
  rt_write(buf, sizeof buf);
}

void rt_srand(uint32_t seed) {
  rand_state = seed != 0 ? seed : 1;
}

uint32_t rt_rand(void) {
  uint32_t x = rand_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  rand_state = x;
  return x;
}

uint32_t rt_rand_below(uint32_t n) {
  return (uint32_t)(((uint64_t)rt_rand() * n) >> 32);
}

int rt_check(const char* what, uint32_t got, uint32_t expected) {
  rt_puts(what);
  rt_puts(": ");
  rt_put_hex(got);
  if (got == expected) {
    rt_puts(" OK\n");
    return 0;
  }
  rt_puts(" FAIL (expected ");
  rt_put_hex(expected);
  rt_puts(")\n");
  return 1;
}
