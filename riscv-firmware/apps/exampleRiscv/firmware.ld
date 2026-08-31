/*
 * firmware.ld - Linker script for XuanTie E907 RISC-V Firmware
 * Running in 256 KB Dedicated RISC-V SRAM (0x00000000 - 0x0003FFFF)
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* 224 KB Dedicated Zero-Wait-State RISC-V SRAM */
    R_SRAM (rwx)  : ORIGIN = 0x00000000, LENGTH = 224K

    /* Top 32 KB for RemoteProc Resource Table and Trace Buffer */
    R_TABLE (rwx) : ORIGIN = 0x00038000, LENGTH = 32K
}

SECTIONS
{
    /* 1. Vector table and CRT0 startup code at SRAM base (0x00000000) */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > R_SRAM

    /* 2. Program Code */
    .text :
    {
        . = ALIGN(4);
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

    /* 3. Linux RemoteProc Resource Table (placed in R_TABLE for host visibility) */
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
    } > R_SRAM

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
    } > R_SRAM

    /* 6. Stack and Heap */
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
}
