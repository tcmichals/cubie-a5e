/* Linker script for XuanTie E906/E907 bare-metal co-processor */

OUTPUT_ARCH("riscv")
ENTRY(_enter)

MEMORY
{
    ITCM (rx)   : ORIGIN = 0x00000000, LENGTH = 64K
    SRAM (rx)   : ORIGIN = 0x00028000, LENGTH = 320K
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

    /* 2. Fast critical ISR code is placed in ITCM for execution */
    .fastcode :
    {
        . = ALIGN(4);
        *(.text.fastcode)
        *(.text.isr)
        . = ALIGN(4);
    } > ITCM

    /* 3. Main program code and read-only data are placed in System SRAM C */
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
        . = ALIGN(4);
    } > SRAM

    /* Load address of initialized data is in SRAM, but execution address is DTCM */
    _sram_data_start = LOADADDR(.data);

    /* 4. Initialized data segment is executed in DTCM but stored in SRAM C */
    .data : AT(_sram_data_start)
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
    } > DTCM

    /* 5. Zero-initialized BSS segment in DTCM */
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

    /* 6. Stack pointer baseline set to the top of DTCM (grows downwards) */
    _stack_top = ORIGIN(DTCM) + LENGTH(DTCM);
}

/* 7. Memory Bounds Assertions (Fail the build on overflow) */
ASSERT( (SIZEOF(.vectors) + SIZEOF(.fastcode)) <= LENGTH(ITCM), "ERROR: ITCM limit exceeded! (Max 64KB)" )
ASSERT( (_bss_end - ORIGIN(DTCM)) <= LENGTH(DTCM), "ERROR: DTCM limit exceeded! Data/BSS too large (Max 64KB)" )
