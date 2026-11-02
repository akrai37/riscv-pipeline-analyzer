#pragma once

#include <cstdint>

#include "rvsim/isa.hpp"

// Instruction semantics shared by the reference model and the pipeline.
// The pipeline's job is timing and operand delivery (forwarding, stalls,
// flushes); keeping the arithmetic in one place means the lockstep checker
// isolates exactly that, while riscv-tests cover the arithmetic itself.
namespace rvsim {

// Value written to rd by any instruction other than a load. `rs1` and `rs2`
// are operand values; immediates are taken from `in`.
uint32_t compute(const Instr& in, uint32_t pc, uint32_t rs1, uint32_t rs2);

bool branch_taken(Op op, uint32_t rs1, uint32_t rs2);

// Destination of a taken branch, jal or jalr.
uint32_t control_target(const Instr& in, uint32_t pc, uint32_t rs1);

uint32_t mem_address(const Instr& in, uint32_t rs1);

// Access width in bytes for loads and stores, 0 for anything else.
unsigned access_size(Op op);

// Sign- or zero-extends the bytes a load read from memory.
uint32_t extend_load(Op op, uint32_t loaded);

}  // namespace rvsim
