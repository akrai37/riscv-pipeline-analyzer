// Bitwise CRC-32 (IEEE 802.3, reflected, polynomial 0xEDB88320): tight
// loops of shifts, masks and XORs with long ALU dependency chains.

#include "rt.h"

#define LEN 16384

static uint8_t buf[LEN];

static uint32_t crc32(const uint8_t* p, size_t n) {
  uint32_t crc = ~0u;
  while (n-- != 0) {
    crc ^= *p++;
    for (int k = 0; k < 8; ++k) {
      crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
  }
  return ~crc;
}

int main(void) {
  // The standard check value for "123456789" is independent of this code.
  static const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  int failures = rt_check("crc32 check value", crc32(check, sizeof check), 0xCBF43926u);

  rt_srand(5);
  for (int i = 0; i < LEN; ++i) {
    buf[i] = (uint8_t)rt_rand();
  }
  failures += rt_check("crc32 buffer", crc32(buf, LEN), 0x000ACE3Eu);
  return failures;
}
