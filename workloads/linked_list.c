// Pointer chasing through a list linked in shuffled order: each step's
// loads depend on the previous step's `p->next` load, a serial
// load-to-load chain. At -O2 clang schedules the value load and the loop
// bookkeeping between the next-pointer load and its use, which hides the
// one-cycle load-use penalty with forwarding; without forwarding the chain
// costs two stall cycles per step.

#include "rt.h"

#define N 8192
#define PASSES 8

struct node {
  struct node* next;
  uint32_t value;
};

static struct node nodes[N];
static uint32_t order[N];

int main(void) {
  rt_srand(3);
  for (uint32_t i = 0; i < N; ++i) {
    order[i] = i;
    nodes[i].value = i;
  }
  // Fisher-Yates shuffle, then link the nodes in shuffled order.
  for (uint32_t i = N - 1; i > 0; --i) {
    const uint32_t j = rt_rand_below(i + 1);
    const uint32_t t = order[i];
    order[i] = order[j];
    order[j] = t;
  }
  for (uint32_t i = 0; i + 1 < N; ++i) {
    nodes[order[i]].next = &nodes[order[i + 1]];
  }
  nodes[order[N - 1]].next = 0;

  uint32_t visited = 0;
  uint32_t sum = 0;
  for (int pass = 0; pass < PASSES; ++pass) {
    for (const struct node* p = &nodes[order[0]]; p != 0; p = p->next) {
      sum += p->value;
      ++visited;
    }
  }
  return rt_check("list nodes visited", visited, PASSES * N) +
         rt_check("list value sum", sum, PASSES * (N * (N - 1) / 2));
}
