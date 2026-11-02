// Binary search for random keys. Each probe's direction depends on the key,
// so the branch is close to a coin flip for any predictor.

#include "rt.h"

#define N 4096
#define QUERIES 10000

static uint32_t keys[N];

static int search(const uint32_t* arr, int n, uint32_t key) {
  int lo = 0;
  int hi = n - 1;
  while (lo <= hi) {
    const int mid = lo + (hi - lo) / 2;
    if (arr[mid] == key) {
      return mid;
    }
    if (arr[mid] < key) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return -1;
}

int main(void) {
  // keys[i] = 3i + 1, so whether a key is present has a closed form.
  for (uint32_t i = 0; i < N; ++i) {
    keys[i] = 3 * i + 1;
  }
  rt_srand(4);
  uint32_t found = 0;
  uint32_t wrong = 0;
  for (int q = 0; q < QUERIES; ++q) {
    const uint32_t key = rt_rand_below(3 * N + 3);
    const int got = search(keys, N, key);
    const int want = (key % 3 == 1 && key / 3 < N) ? (int)(key / 3) : -1;
    if (got != want) {
      ++wrong;
    }
    if (got >= 0) {
      ++found;
    }
  }
  rt_puts("bsearch hits: ");
  rt_put_u32(found);
  rt_puts("\n");
  return rt_check("bsearch wrong answers", wrong, 0);
}
