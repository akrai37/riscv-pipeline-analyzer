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

## License

MIT, see [LICENSE](LICENSE).
