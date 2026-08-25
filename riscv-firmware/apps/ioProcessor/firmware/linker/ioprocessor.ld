/*
 * Linker Script for XuanTie E907 Flight Controller Firmware
 * Memory Layout:
 *   - ITCM (64 KB @ 0x00000000): Zero-wait-state instruction memory (Vectors, CRT0, fast ISRs)
 *   - DTCM (64 KB @ 0x00080000): Zero-wait-state data memory (Stacks, AbstractX coroutine pool)
 *   - SRAM (256 KB @ 0x07140000): Main firmware body & code pools (Shared SRAM C)
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    ITCM (rx)   : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx)  : ORIGIN = 0x00080000, LENGTH = 64K
    SRAM (rwx)  : ORIGIN = 0x07140000, LENGTH = 256K
}

STACK_SIZE = 4K;

SECTIONS
{
    /* Linux RemoteProc Resource Table (required for kernel ELF loader) */
    .resource_table : {
        KEEP(*(.resource_table))
    } > SRAM

    /* Interrupt Vector Table and CRT0 Bootstrap pinned at ITCM origin */
    .vectors : {
        . = ALIGN(64);
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > ITCM

    /* Performance-critical fast ISRs and inner loops pinned to zero-wait-state ITCM */
    .fastcode : {
        . = ALIGN(4);
        *(.text.fastcode)
        *(.text.fastcode.*)
    } > ITCM AT > SRAM
    _fastcode_load = LOADADDR(.fastcode);
    _fastcode_start = ADDR(.fastcode);
    _fastcode_end = _fastcode_start + SIZEOF(.fastcode);

    /* Main Application Body in SRAM C */
    .text : {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
    } > SRAM

    /* Read-Only Data */
    .rodata : {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
    } > SRAM

    .eh_frame : {
        KEEP (*(.eh_frame))
    } > SRAM

    /* Initialized Data in DTCM, loaded from SRAM */
    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(4);
        _edata = .;
    } > DTCM AT > SRAM
    _sidata = LOADADDR(.data);

    /* AbstractX Zero-Allocation Static Coroutine Pool in DTCM */
    .dtcm (NOLOAD) : {
        . = ALIGN(4);
        *(.dtcm)
        *(.dtcm.*)
    } > DTCM

    /* Zero-Initialized BSS in DTCM */
    .bss (NOLOAD) : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DTCM

    /* Stack at top of DTCM */
    .stack (NOLOAD) : {
        . = ALIGN(8);
        _sstack = .;
        . = . + STACK_SIZE;
        . = ALIGN(8);
        _estack = .;
    } > DTCM

    /* Global pointer symbol for RISC-V gp-relative relaxation */
    PROVIDE(__global_pointer$ = _sdata + 0x800);
}
