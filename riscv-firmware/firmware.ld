/*
 * firmware.ld - Linker script for Allwinner XuanTie E907 Co-Processor
 * Option A: Direct execution from 256 KB Dedicated RISC-V Local SRAM (0x00000000)
 * with Shared System SRAM C (0x00020000) for RemoteProc tables, IPC, and telemetry.
 */

OUTPUT_ARCH("riscv")
ENTRY(_enter)

MEMORY
{
    /* 256 KB Dedicated Zero-Wait-State RISC-V SRAM (Physical 0x07280000 / Core 0x00000000) */
    R_SRAM (rx)  : ORIGIN = 0x00000000, LENGTH = 256K

    /* 128 KB Shared System SRAM C (Physical 0x00020000 / Core 0x00020000) */
    SRAM_C (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
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

    /* 3. Linux RemoteProc Resource Table (in SRAM C for host visibility) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > SRAM_C

    /* 4. Initialized Data Segment (Loaded in R_SRAM, executed in SRAM_C) */
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
    } > SRAM_C AT > R_SRAM

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
    } > SRAM_C

    /* 6. Stack and Heap in SRAM C */
    .heap (NOLOAD) :
    {
        . = ALIGN(8);
        _heap_start = .;
        . = . + 8K;
        _heap_end = .;
    } > SRAM_C

    .stack (NOLOAD) :
    {
        . = ALIGN(8);
        _stack_bottom = .;
        . = . + 16K;
        _stack_top = .;
    } > SRAM_C

    /* 7. Dedicated 32 KB Shared Ring-Buffer / Telemetry Window at top of SRAM C */
    .shared_memory (NOLOAD) :
    {
        . = ALIGN(8);
        _shared_mem_start = .;
        . = . + 32K;
        _shared_mem_end = .;
    } > SRAM_C
}
