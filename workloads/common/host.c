// Native stand-in for libc.c, used by `make host` to run the workloads on
// the build machine and confirm their expected values.

#include <unistd.h>

#include "rt.h"

void rt_write(const char* buf, size_t len) {
  while (len > 0) {
    const ssize_t n = write(1, buf, len);
    if (n <= 0) {
      return;
    }
    buf += n;
    len -= (size_t)n;
  }
}
