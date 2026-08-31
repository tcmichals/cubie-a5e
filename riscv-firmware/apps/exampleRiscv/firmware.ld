/*
 * firmware.ld - Linker script for XuanTie E907 RISC-V Firmware
 * Running in 256 KB Dedicated RISC-V SRAM (0x00000000 - 0x0003FFFF)
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* XuanTie E907 ITCM: 64 KB (0x00000000 - 0x0000FFFF) */
    ITCM (rx)     : ORIGIN = 0x00000000, LENGTH = 64K

    /* XuanTie E907 DTCM: 60 KB for Data, BSS, Heap, Stack (0x00080000 - 0x0008EFFF) */
    DTCM (rwx)    : ORIGIN = 0x00080000, LENGTH = 60K

    /* Top 4 KB of DTCM for RemoteProc Resource Table and Trace Buffer (0x0008F000 - 0x0008FFFF) */
    R_TABLE (rwx) : ORIGIN = 0x0008F000, LENGTH = 4K
}

SECTIONS
{
    /* 1. Vector table and CRT0 startup code at ITCM base (0x00000000) */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > ITCM

    /* 2. Program Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > ITCM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        . = ALIGN(4);
    } > ITCM

    /* 3. Linux RemoteProc Resource Table (placed in R_TABLE in DTCM) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > R_TABLE

    /* 4. Initialized Data Segment */
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
    } > DTCM AT > ITCM

    _sram_data_start = LOADADDR(.data);

    /* 5. Zero-initialized BSS Segment */
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
    } > DTCM

    /* 6. Stack and Heap */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + 8K;
        _heap_end = .;
    } > DTCM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + 16K;
        _stack_top = .;
    } > DTCM
}
