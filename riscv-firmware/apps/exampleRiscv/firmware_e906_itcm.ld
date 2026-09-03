/*
 * firmware_e906_itcm.ld - Linker script for Allwinner T527 XuanTie E906
 * Running in ITCM (0x00000000) and DTCM (0x00080000).
 * Boot vector is programmed to 0x00000000 via E906_STA_ADD_REG.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx) : ORIGIN = 0x00080000, LENGTH = 64K
}

SECTIONS
{
    .vectors :
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > ITCM

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
        . = ALIGN(4);
    } > ITCM

    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > DTCM

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

    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > DTCM

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
