# Builds the RV32IM workloads and the riscv-tests suites into prebuilt ELF
# files, which are committed so the simulator can be used without a RISC-V
# toolchain. Needs a clang with the RISC-V backend and ld.lld:
#
#   macOS:  brew install llvm lld
#   Ubuntu: apt install clang lld
#
#   make            build workloads and riscv-tests into bin/ and ../tests/riscv-tests/bin/
#   make host       build the workloads natively and run them (checks expected values)
#   make OUT=dir    build both sets into dir/ instead (used by CI)

LLVM_PREFIX := $(shell brew --prefix llvm 2>/dev/null)
CLANG ?= $(if $(wildcard $(LLVM_PREFIX)/bin/clang),$(LLVM_PREFIX)/bin/clang,clang)
HOST_CC ?= cc

ROOT := ..
COMMON := common
RVTESTS := $(ROOT)/third_party/riscv-tests/isa
RVTEST_ENV := $(ROOT)/tests/riscv-tests/env

OUT ?=
WL_BIN := $(if $(OUT),$(OUT)/workloads,bin)
RT_BIN := $(if $(OUT),$(OUT)/riscv-tests,$(ROOT)/tests/riscv-tests/bin)
OBJ := $(if $(OUT),$(OUT)/obj,obj)

# Linker relaxation stays on for the workloads so calls become `jal`, as in
# real toolchain output. The linker script defines no __global_pointer$, so
# no gp-relative addressing is introduced. riscv-tests use gp as TESTNUM and
# are built with relaxation off.
ARCH := --target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -mcmodel=medany
LINK := -nostdlib -static -fuse-ld=lld -Wl,-T,$(COMMON)/link.ld -Wl,--build-id=none
CFLAGS := $(ARCH) -O2 -ffreestanding -fno-pic -fno-stack-protector -fno-asynchronous-unwind-tables \
          -std=c11 -Wall -Wextra -Werror -I$(COMMON)
ASFLAGS := $(ARCH)
RVTEST_FLAGS := $(ARCH) -mno-relax

WORKLOADS := matmul qsort linked_list bsearch fib crc32 memops
RV32UI := $(basename $(notdir $(wildcard $(RVTESTS)/rv32ui/*.S)))
RV32UM := $(basename $(notdir $(wildcard $(RVTESTS)/rv32um/*.S)))

WL_ELFS := $(WORKLOADS:%=$(WL_BIN)/%.elf)
RT_ELFS := $(RV32UI:%=$(RT_BIN)/rv32ui-%.elf) $(RV32UM:%=$(RT_BIN)/rv32um-%.elf)
RUNTIME := $(OBJ)/crt0.o $(OBJ)/rt.o $(OBJ)/libc.o

.PHONY: all workloads riscv-tests host clean
all: workloads riscv-tests
workloads: $(WL_ELFS)
riscv-tests: $(RT_ELFS)

$(OBJ) $(WL_BIN) $(RT_BIN) $(OBJ)/host:
	mkdir -p $@

$(OBJ)/crt0.o: $(COMMON)/crt0.S | $(OBJ)
	$(CLANG) $(ASFLAGS) -c $< -o $@
$(OBJ)/rt.o: $(COMMON)/rt.c $(COMMON)/rt.h | $(OBJ)
	$(CLANG) $(CFLAGS) -c $< -o $@
# -fno-builtin stops the copy loops from being turned into calls to themselves.
$(OBJ)/libc.o: $(COMMON)/libc.c $(COMMON)/rt.h | $(OBJ)
	$(CLANG) $(CFLAGS) -fno-builtin -c $< -o $@

$(WL_BIN)/%.elf: %.c $(RUNTIME) $(COMMON)/rt.h $(COMMON)/link.ld | $(WL_BIN)
	$(CLANG) $(CFLAGS) $(LINK) $(RUNTIME) $< -o $@

$(RT_BIN)/rv32ui-%.elf: $(RVTESTS)/rv32ui/%.S $(RVTEST_ENV)/riscv_test.h $(COMMON)/link.ld | $(RT_BIN)
	$(CLANG) $(RVTEST_FLAGS) $(LINK) -I$(RVTEST_ENV) -I$(RVTESTS)/macros/scalar $< -o $@
$(RT_BIN)/rv32um-%.elf: $(RVTESTS)/rv32um/%.S $(RVTEST_ENV)/riscv_test.h $(COMMON)/link.ld | $(RT_BIN)
	$(CLANG) $(RVTEST_FLAGS) $(LINK) -I$(RVTEST_ENV) -I$(RVTESTS)/macros/scalar $< -o $@

host: | $(OBJ)/host
	@set -e; for w in $(WORKLOADS); do \
	  $(HOST_CC) -O2 -std=c11 -Wall -Wextra -Werror -I$(COMMON) $(COMMON)/rt.c $(COMMON)/host.c $$w.c -o $(OBJ)/host/$$w; \
	  $(OBJ)/host/$$w; \
	done

clean:
	rm -rf obj
