/*
 * firmware_e902_dram.ld - Linker script for Allwinner A733 XuanTie E902
 * Running in Dedicated DRAM Carveout (0x40014000 - 0x40053FFF, 256 KB)
 * E902 hardware reset vector is hardwired to 0x40014000 by bootloader/ATF.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    DRAM (rwx) : ORIGIN = 0x40014000, LENGTH = 256K
}

SECTIONS
{
    /* 1. Vector table and CRT0 startup code at DRAM base (0x40014000). */
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > DRAM

    /* 2. Program Code */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > DRAM

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > DRAM

    /* 3. RemoteProc Resource Table */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > DRAM

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
    } > DRAM

    .bss :
    {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        . = ALIGN(4);
        _bss_end = .;
    } > DRAM

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > DRAM

    /DISCARD/ :
    {
        *(.note.gnu.build-id)
        *(.comment)
        *(.riscv.attributes)
    }
}
