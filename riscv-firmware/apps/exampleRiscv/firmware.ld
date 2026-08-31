/*
 * firmware.ld - Linker script for XuanTie E907 RISC-V Firmware
 * Running in Shared System SRAM C (0x00020000 - 0x0003FFFF)
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* 128 KB Shared System SRAM C */
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
}

SECTIONS
{
    /* 1. Vector table and CRT0 startup code at SRAM C base (0x00020000) */
    .vectors 0x00020000 :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM

    /* 2. Linux RemoteProc Resource Table (at fixed offset 0x00028000 for host) */
    .resource_table 0x00028000 :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > SRAM

    /* 3. Program Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > SRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
        . = ALIGN(4);
    } > SRAM

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
    } > SRAM

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
    } > SRAM

    /* 6. Stack and Heap */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + 4K;
        _heap_end = .;
    } > SRAM

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + 8K;
        _stack_top = .;
    } > SRAM
}
