# Vendored riscv-tests

Source: https://github.com/riscv-software-src/riscv-tests
Commit: 3df703cd52d5f0d8224e3c50bf03c6cb70efff98 (2026-10-19)
License: BSD-3-Clause, see [LICENSE](LICENSE).

Only the files needed for the RV32UI and RV32UM user-level suites are kept,
unmodified:

- `isa/rv32ui/*.S` and the `isa/rv64ui/*.S` files they include
- `isa/rv32um/*.S`
- `isa/macros/scalar/test_macros.h`

Not vendored, because the simulator does not implement what they test:

| Test | Reason |
|---|---|
| `rv32ui/fence_i` | Self-modifying code; the instruction and data ports are split and `fence.i` is not implemented |
| `rv32ui/ma_data` | Misaligned loads and stores, which raise a fault here instead of being emulated; its handler also needs CSRs |

The upstream `env/` (a submodule) is replaced by
[`tests/riscv-tests/env/riscv_test.h`](../../tests/riscv-tests/env/riscv_test.h),
which needs no CSRs and reports results through the exit syscall.
