/*
 * firmware_e902_sram.ld - Linker script for Allwinner A733 XuanTie E902
 * Running in Shared SRAM A2 (0x00040000 - 0x00067FFF, 160 KB)
 * E902 hardware reset vector is hardwired to 0x00040000.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x00040000, LENGTH = 160K
}

SECTIONS
{
    /* 1. Vector table and CRT0 startup code at SRAM A2 base (0x00040000). */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM

    /* 2. Program Code */
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

    /* 3. RemoteProc Resource Table */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > SRAM

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

    /* Execution Stack (8 KB) */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > SRAM

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
