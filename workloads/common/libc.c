// Target-only pieces of the runtime: the write syscall, and the few libc
// functions the compiler may emit calls to even with -ffreestanding -nostdlib
// (struct copies, zero-initialised locals, recognised copy loops).
//
// This file is built with -fno-builtin; otherwise the compiler could turn
// these loops back into calls to themselves.

#include <stddef.h>
#include <stdint.h>

#include "rt.h"

void rt_write(const char* buf, size_t len) {
  register uintptr_t a0 __asm__("a0") = 1;  // fd: stdout
  register uintptr_t a1 __asm__("a1") = (uintptr_t)buf;
  register uintptr_t a2 __asm__("a2") = len;
  register uintptr_t a7 __asm__("a7") = 64;  // write
  __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
}

void* memcpy(void* dst, const void* src, size_t n) {
  unsigned char* d = dst;
  const unsigned char* s = src;
  while (n-- != 0) {
    *d++ = *s++;
  }
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  unsigned char* d = dst;
  const unsigned char* s = src;
  if (d < s) {
    while (n-- != 0) {
      *d++ = *s++;
    }
  } else {
    while (n-- != 0) {
      d[n] = s[n];
    }
  }
  return dst;
}

void* memset(void* dst, int c, size_t n) {
  unsigned char* d = dst;
  while (n-- != 0) {
    *d++ = (unsigned char)c;
  }
  return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
  const unsigned char* x = a;
  const unsigned char* y = b;
  for (size_t i = 0; i < n; ++i) {
    if (x[i] != y[i]) {
      return x[i] < y[i] ? -1 : 1;
    }
  }
  return 0;
}

size_t strlen(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}
