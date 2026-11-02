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
./build/rvsim --stats workloads/bin/matmul.elf            # pipeline model, CPI breakdown
./build/rvsim --lockstep workloads/bin/qsort.elf          # check against the reference model
./build/rvsim --no-forwarding --predictor not-taken --ras 0 --stats workloads/bin/fib.elf
./build/rvsim --trace workloads/bin/fib.elf 2>&1 | head   # one line per cycle
./build/rvsim --model ref workloads/bin/crc32.elf         # sequential reference model
ctest --test-dir build -j8                                # all tests
```

## Pipeline model

Classic in-order IF / ID / EX / MEM / WB, cycle-accurate for this modeled
microarchitecture ([src/pipeline.cpp](src/pipeline.cpp)):

- Pipeline registers are double-buffered: every stage computes the next latch
  contents from the current ones, so one call to `cycle()` is one clock edge.
- The register file is written in the first half of a cycle and read in the second.
- Forwarding EX/MEM -> EX and MEM/WB -> EX, plus MEM/WB -> MEM for store data, so a
  load followed by a store of the loaded value does not stall. x0 is never forwarded.
- Multiply and divide hold EX for a configurable latency (3 and 32 cycles by default).
- Branches and jumps resolve in EX. Prediction happens in IF: a direct-mapped BTB
  identifies control-flow instructions and their targets before decode, a 2-bit
  pattern table (or BTFN, or always-not-taken) picks the direction, and a return
  address stack, checkpointed per fetched instruction and restored on a
  misprediction, predicts returns.
- Split instruction and data ports onto one memory, so there is no structural hazard
  between IF and MEM.
- `ecall` waits in ID until older instructions have retired, then runs there, so a
  syscall never executes speculatively.
- Faults travel with the instruction and are raised only at writeback, so wrong-path
  instructions cannot fault a correct program.

| Event | Cycles lost |
|---|---:|
| Dependent ALU instruction (forwarding on) | 0 |
| Load followed by a use of its result | 1 |
| Load followed by a store of its result | 0 |
| Dependent instruction, forwarding off (distance 1 / 2 / 3) | 2 / 1 / 0 |
| Multiply / divide | latency − 1 |
| Branch, correctly predicted (not taken, or taken with a BTB hit) | 0 |
| Branch mispredicted, or taken with a BTB miss | 2 |
| `jal` with a BTB hit / miss | 0 / 2 |
| `jalr` or return with a correct / wrong predicted target | 0 / 2 |

Every bubble is tagged with the cause that created it and counted when it reaches
writeback, so `cycles = instructions + lost cycles` holds exactly and the CPI breakdown
from `--stats` sums with nothing left over. Each row of the table is a test in
[tests/pipeline_test.cpp](tests/pipeline_test.cpp).

## Verification

- **Lockstep**: the pipeline is run against the sequential reference model and every
  retired instruction (PC, encoding, register write, store) is compared; at exit, so
  are the exit code, the program output and the register file. Every program runs in
  lockstep under four pipeline configurations on every `ctest` run.
- **riscv-tests**: the official RV32UI and RV32UM suites.
- **Directed timing tests**: exact cycle costs for each hazard, each also run in
  lockstep.
- **Unit tests**: decoder (against `llvm-mc` encodings), instruction semantics, memory,
  ELF loader, predictor.

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
| `linked_list` | 0.73M | Pointer chasing through a shuffled list: a serial load-to-load chain |
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
