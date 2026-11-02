// Dense integer matrix multiply. Each inner-loop step loads two operands
// and feeds them straight into a multiply-accumulate, so it exercises
// load-use forwarding and the multiplier latency.

#include "rt.h"

#define N 48

static int32_t a[N][N];
static int32_t b[N][N];
static int32_t c[N][N];

static void matmul(void) {
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      int32_t sum = 0;
      for (int k = 0; k < N; ++k) {
        sum += a[i][k] * b[k][j];
      }
      c[i][j] = sum;
    }
  }
}

int main(void) {
  rt_srand(1);
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      a[i][j] = (int32_t)rt_rand_below(17) - 8;
      b[i][j] = (int32_t)rt_rand_below(17) - 8;
    }
  }
  matmul();
  uint32_t hash = 0;
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      hash = hash * 31u + (uint32_t)c[i][j];
    }
  }
  return rt_check("matmul checksum", hash, 0x7887b0a9u);
}
