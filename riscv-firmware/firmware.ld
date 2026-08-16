/* Linker script for XuanTie E906/E907 bare-metal co-processor */

OUTPUT_ARCH("riscv")
ENTRY(_enter)

MEMORY
{
    ITCM (rx)   : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx)  : ORIGIN = 0x00080000, LENGTH = 64K
}

SECTIONS
{
    /* 1. Vector table and entry point bootstrap placed at the start of ITCM */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > ITCM

    /* 2. Fast code, main program code placed in ITCM for zero-wait execution */
    .text :
    {
        . = ALIGN(4);
        *(.text.fastcode)
        *(.text.isr)
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

    /* 3. Initialized data segment executed in DTCM, loaded from ITCM */
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
    } > DTCM AT> ITCM

    _sram_data_start = LOADADDR(.data);

    /* 4. Zero-initialized BSS segment in DTCM */
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
    } > DTCM

    /* 5. Stack pointer baseline set to the top of DTCM (grows downwards) */
    _stack_top = ORIGIN(DTCM) + LENGTH(DTCM);
}

/* 6. Memory Bounds Assertions */
ASSERT( (LOADADDR(.data) + SIZEOF(.data)) <= LENGTH(ITCM), "ERROR: ITCM limit exceeded! (Max 64KB)" )
ASSERT( (_bss_end - ORIGIN(DTCM)) <= LENGTH(DTCM), "ERROR: DTCM limit exceeded! Data/BSS too large (Max 64KB)" )

