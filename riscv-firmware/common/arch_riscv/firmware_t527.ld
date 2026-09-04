/*
 * firmware_t527.ld - Unified Linker Script for Allwinner T527 / A523 XuanTie E907
 *
 * Target: Allwinner XuanTie E907 / E906 RISC-V Co-Processor
 * Memory: Shared System PubSRAM C (128 KB @ 0x00020000)
 *         Directly addressable by both ARM Cortex-A55 and XuanTie E907 without translation.
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x00020000, LENGTH = 128K
}

SECTIONS
{
    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors :
    {
        . = ALIGN(64);
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM

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

    /* Linux RemoteProc Resource Table (Parsed by Kernel on ELF Load) */
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
        _sdata = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM
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
    } > SRAM

    /* Execution Stack (8 KB) */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _stack_bottom = .;
        . += 0x2000;
        _stack_top = .;
    } > SRAM

    /* Scratchpad / DTCM Compatibility Memory */
    .dtcm_scratch (NOLOAD) :
    {
        . = ALIGN(4);
        __dtcm_scratch_start = .;
        *(.dtcm_scratch)
        *(.dtcm_scratch.*)
        . = ALIGN(4);
        __dtcm_scratch_end = .;
    } > SRAM

    /* RemoteProc Trace Buffer in Shared SRAM */
    .trace_buffer (NOLOAD) :
    {
        . = ALIGN(4);
        __trace_start = .;
        KEEP(*(.trace_buffer))
        KEEP(*(.trace_buffer.*))
        . = ALIGN(4);
        __trace_end = .;
    } > SRAM

    /* Shared Application / IPC Memory */
    .sram_c (NOLOAD) :
    {
        . = ALIGN(4);
        __sram_c_start = .;
        *(.sram_c)
        *(.sram_c.*)
        *(.sram_c_loc1)
        *(.sram_c_loc2)
        . = ALIGN(4);
        __sram_c_end = .;
    } > SRAM

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
