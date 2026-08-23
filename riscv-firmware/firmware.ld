/* Linker script for XuanTie E906/E907 bare-metal co-processor in SRAM C */

OUTPUT_ARCH("riscv")
ENTRY(_enter)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
}

SECTIONS
{
    /* 1. Vector table and entry point bootstrap placed at start of SRAM */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM

    /* RemoteProc Resource Table (Trace Buffer & RPMsg Vrings) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        . = ALIGN(4);
    } > SRAM

    /* 2. Program code */
    .text :
    {
        . = ALIGN(4);
        *(.text.fastcode)
        *(.text.isr)
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

    /* 3. Initialized data segment */
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

    _sram_data_start = LOADADDR(.data);

    /* 4. Zero-initialized BSS segment */
    .bss :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.sbss)
        *(.sbss.*)
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM

    /* 5. Stack allocation in SRAM (4KB) */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x1000;
        _stack_top = .;
    } > SRAM
}

/* 6. Memory Bounds Assertions */
ASSERT( (_stack_top <= (ORIGIN(SRAM) + LENGTH(SRAM))), "ERROR: SRAM limit exceeded!" )
