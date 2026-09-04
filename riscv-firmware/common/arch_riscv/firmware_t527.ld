/*
 * firmware_t527.ld - Unified Linker Script for Allwinner T527 XuanTie E907
 *
 * Memory Map:
 *   ITCM (rx)   : 0x00000000 (64 KB - Zero-Wait Instruction Memory)
 *   DTCM (rwx)  : 0x00080000 (64 KB - Zero-Wait Data Memory & Stack)
 *   SRAM_C (rw) : 0x07130000 (256 KB - Dedicated MCU SRAM)
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    ITCM (rx)    : ORIGIN = 0x00000000, LENGTH = 64K
    DTCM (rwx)   : ORIGIN = 0x00080000, LENGTH = 64K
    SRAM_C (rwx) : ORIGIN = 0x07130000, LENGTH = 256K
}

SECTIONS
{
    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors :
    {
        . = ALIGN(64);
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

    /* Linux RemoteProc Resource Table (Parsed by Kernel on ELF Load) */
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
        _sdata = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > DTCM AT > ITCM
    _sidata = LOADADDR(.data);

    .bss :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DTCM

    /* DTCM 8KB Stack */
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
