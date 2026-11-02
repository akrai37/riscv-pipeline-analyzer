// Replacement for the riscv-tests "p" environment, for a simulator with no
// CSRs, traps or privilege modes.
//
// The upstream environment boots through machine-mode CSR writes (trap
// vector, hart ID, mstatus) and reports through a memory-mapped `tohost`
// word. Here a test starts directly at _start with every register zero, and
// reports through the exit syscall (a7 = 93):
//
//   pass: exit(0)
//   fail: exit((TESTNUM << 1) | 1), so the failing test case is code >> 1
//
// The fail code is always odd, so a failure can never look like a pass even
// if TESTNUM were still zero.

#ifndef RVSIM_RISCV_TEST_H
#define RVSIM_RISCV_TEST_H

#define RVTEST_RV32U \
  .macro init;       \
  .endm

#define RVTEST_RV64U RVTEST_RV32U

#define TESTNUM gp

#define RVTEST_CODE_BEGIN      \
  .section .text.init, "ax";   \
  .align 6;                    \
  .globl _start;               \
  _start:                      \
  init;

#define RVTEST_CODE_END unimp

#define RVTEST_PASS \
  li a0, 0;         \
  li a7, 93;        \
  ecall

#define RVTEST_FAIL      \
  slli a0, TESTNUM, 1;   \
  ori a0, a0, 1;         \
  li a7, 93;             \
  ecall

#define RVTEST_DATA_BEGIN    \
  .data;                     \
  .align 4;                  \
  .global begin_signature;   \
  begin_signature:

#define RVTEST_DATA_END    \
  .align 4;                \
  .global end_signature;   \
  end_signature:

#endif
