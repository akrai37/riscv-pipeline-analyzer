# RISC-V Pipeline Performance Analyzer

A cycle-accurate 5-stage RV32IM pipeline simulator in C++20, with data forwarding,
hazard detection, branch prediction and flushing. It is verified in lockstep against a
sequential reference model and reports CPI and stall cycles per workload.

> Work in progress. Results, design notes and usage will be documented here as the
> milestones land.

## Build

```sh
cmake -S . -B build
cmake --build build -j
```

Options:

| Option | Default | Effect |
|---|---|---|
| `RVSIM_WERROR` | `OFF` | Treat warnings as errors (on in CI) |
| `RVSIM_SANITIZE` | `OFF` | Build with ASan and UBSan |
| `RVSIM_BUILD_TESTS` | `ON` | Build the test suite (fetches GoogleTest) |

## Run

```sh
./build/rvsim workloads/bin/matmul.elf
ctest --test-dir build        # unit tests, riscv-tests, workloads
```

## Programs

All programs are static RV32IM ELF executables loaded at `0x80000000`. Prebuilt
binaries are committed, so no RISC-V toolchain is needed to run anything. To
rebuild them (clang with the RISC-V backend and `ld.lld`):

```sh
make -C workloads          # workloads/bin/ and tests/riscv-tests/bin/
make -C workloads host     # build the workloads natively and run them
```

**Workloads** ([workloads/](workloads/)). Each one checks its own result and exits
with the number of failed checks. Expected values are either independent of the
code (the CRC-32 check value, closed-form sums) or come from a native build of the
same source (`make host`).

| Workload | Instructions | What it stresses |
|---|---:|---|
| `matmul` | 0.92M | 48x48 integer matrix multiply: load-use chains into `mul` |
| `qsort` | 0.66M | Quicksort of 4000 random keys: data-dependent branches |
| `linked_list` | 0.73M | Pointer chasing through a shuffled list: back-to-back load-use |
| `bsearch` | 1.56M | 10,000 binary searches: hard-to-predict branches |
| `fib` | 0.62M | Recursive Fibonacci: calls, returns, stack traffic |
| `crc32` | 0.90M | Bitwise CRC-32 over 16 KiB: long ALU dependency chains |
| `memops` | 1.06M | Transform-copy, `memset`, `memcmp`, `strlen` over large buffers |

**riscv-tests** ([third_party/riscv-tests/](third_party/riscv-tests/)). The official
RV32UI and RV32UM suites (40 + 8 tests), run through a custom environment,
[tests/riscv-tests/env/riscv_test.h](tests/riscv-tests/env/riscv_test.h), that needs
no CSRs and reports through the exit syscall. `fence_i` and `ma_data` are not run;
[VENDORED.md](third_party/riscv-tests/VENDORED.md) explains why.

**Runtime** ([workloads/common/](workloads/common/)): startup code that sets the stack,
clears `.bss` and calls `main`; the `write` and `exit` syscalls; and the `memcpy`,
`memset`, `memmove`, `memcmp` and `strlen` that clang may emit calls to even with
`-ffreestanding -nostdlib`.

## License

MIT, see [LICENSE](LICENSE).
