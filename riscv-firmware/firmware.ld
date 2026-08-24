/*
 * firmware.ld - Linker script for Allwinner XuanTie E907 Co-Processor
 * Option A: Direct execution from 256 KB Dedicated RISC-V Local SRAM (0x00000000)
 */

OUTPUT_ARCH("riscv")
ENTRY(_enter)

MEMORY
{
    /* 256 KB Dedicated Zero-Wait-State RISC-V SRAM (Physical 0x07110000 / Core 0x00000000) */
    R_SRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 256K
}

SECTIONS
{
    /* 1. Vector table and eGON bootstrap placed at start of RISC-V SRAM (0x00000000) */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > R_SRAM

    /* 2. Program Code */
    .text :
    {
        . = ALIGN(4);
        *(.text.fastcode)
        *(.text.isr)
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > R_SRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        . = ALIGN(4);
    } > R_SRAM

    /* 3. Initialized Data Segment */
    .data :
    {
        . = ALIGN(4);
        _data_start = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _data_end = .;
    } > R_SRAM

    _sram_data_start = LOADADDR(.data);

    /* 4. Zero-initialized BSS Segment */
    .bss :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > R_SRAM

    /* 5. Stack and Heap */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + 8K;
        _heap_end = .;
    } > R_SRAM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + 16K;
        _stack_top = .;
    } > R_SRAM

    /* 6. Linux RemoteProc Resource Table (placed at fixed offset 0x00030000) */
    .resource_table 0x00030000 :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > R_SRAM
}
