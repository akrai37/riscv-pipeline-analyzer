// Quicksort over random keys. The partition loops branch on data
// comparisons, which no static or history-based predictor gets right much
// better than chance.

#include "rt.h"

#define N 4000

static int32_t v[N];

static void swap(int32_t* x, int32_t* y) {
  const int32_t t = *x;
  *x = *y;
  *y = t;
}

// Hoare partitioning; recurses into the smaller half and loops on the
// larger, so stack depth stays O(log N).
static void quicksort(int32_t* arr, int lo, int hi) {
  while (lo < hi) {
    const int32_t pivot = arr[lo + (hi - lo) / 2];
    int i = lo;
    int j = hi;
    while (i <= j) {
      while (arr[i] < pivot) {
        ++i;
      }
      while (arr[j] > pivot) {
        --j;
      }
      if (i <= j) {
        swap(&arr[i], &arr[j]);
        ++i;
        --j;
      }
    }
    if (j - lo < hi - i) {
      quicksort(arr, lo, j);
      lo = i;
    } else {
      quicksort(arr, i, hi);
      hi = j;
    }
  }
}

int main(void) {
  rt_srand(2);
  uint32_t sum_before = 0;
  uint32_t xor_before = 0;
  for (int i = 0; i < N; ++i) {
    v[i] = (int32_t)rt_rand();
    sum_before += (uint32_t)v[i];
    xor_before ^= (uint32_t)v[i];
  }

  quicksort(v, 0, N - 1);

  uint32_t unsorted = 0;
  uint32_t sum_after = 0;
  uint32_t xor_after = 0;
  for (int i = 0; i < N; ++i) {
    if (i > 0 && v[i - 1] > v[i]) {
      ++unsorted;
    }
    sum_after += (uint32_t)v[i];
    xor_after ^= (uint32_t)v[i];
  }
  // Sorted, and still the same multiset of keys.
  return rt_check("qsort out-of-order pairs", unsorted, 0) +
         rt_check("qsort sum", sum_after, sum_before) +
         rt_check("qsort xor", xor_after, xor_before);
}
