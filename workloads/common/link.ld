/* Memory map shared by the workloads and riscv-tests. ORIGIN and LENGTH must
 * match kRamBase and kRamSize in include/rvsim/platform.hpp. */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
  RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 4M
}

SECTIONS
{
  .text : {
    *(.text.init)
    *(.text .text.*)
  } > RAM

  .rodata : ALIGN(16) {
    *(.rodata .rodata.* .srodata .srodata.*)
  } > RAM

  .data : ALIGN(16) {
    *(.data .data.* .sdata .sdata.*)
  } > RAM

  .bss (NOLOAD) : ALIGN(16) {
    __bss_start = .;
    *(.sbss .sbss.* .bss .bss.* COMMON)
    . = ALIGN(16);
    __bss_end = .;
  } > RAM

  /* The stack grows down from the top of RAM. */
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
