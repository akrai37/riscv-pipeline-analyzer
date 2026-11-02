// Streaming memory operations: a word-wide transform-copy, memset, memcmp
// and strlen over buffers much larger than any register working set.

#include "rt.h"

#define WORDS 8192
#define TEXT 12000

static uint32_t src[WORDS];
static uint32_t dst[WORDS];
static char text[TEXT];

// There is no <string.h> in the freestanding target build; these come from
// common/libc.c there and from the C library in the host build.
void* memset(void*, int, size_t);
int memcmp(const void*, const void*, size_t);
size_t strlen(const char*);

int main(void) {
  rt_srand(6);
  for (int i = 0; i < WORDS; ++i) {
    src[i] = rt_rand();
  }

  // XOR with a key so the compiler cannot replace the loop with memcpy.
  const uint32_t key = 0x5a5a5a5au;
  for (int i = 0; i < WORDS; ++i) {
    dst[i] = src[i] ^ key;
  }
  uint32_t mismatched = 0;
  for (int i = 0; i < WORDS; ++i) {
    if ((dst[i] ^ key) != src[i]) {
      ++mismatched;
    }
  }
  int failures = rt_check("memops transform-copy mismatches", mismatched, 0);

  memset(dst, 0, sizeof dst);
  memset(src, 0, sizeof src);
  failures += rt_check("memops memset then memcmp", (uint32_t)memcmp(dst, src, sizeof dst), 0);

  for (int i = 0; i < TEXT - 1; ++i) {
    text[i] = (char)('a' + rt_rand_below(26));
  }
  text[TEXT - 1] = '\0';
  failures += rt_check("memops strlen", (uint32_t)strlen(text), TEXT - 1);
  return failures;
}
