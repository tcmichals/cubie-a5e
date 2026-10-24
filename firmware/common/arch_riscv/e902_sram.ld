/*
 * e902_sram.ld - System SRAM A2 Linker Script for Allwinner A733 XuanTie E902
 *
 * Target: Allwinner XuanTie E902 RISC-V Co-Processor (RV32EMC)
 * Memory: Shared System SRAM A2 (Device Address 0x00040000 - 0x00073FFF, 208 KB)
 *
 * HARDWARE ARCHITECTURE (A733 Mode 2 RemoteProc):
 * - E902 has NO TCMs (no ITCM, no DTCM).
 * - Executes directly out of System SRAM A2 (0x00040000, 208 KB) or DRAM (0x4E000000).
 * - Entry address written to 0x07032204 (E902_STA_ADD_REG) by sunxi_rproc.
 */

OUTPUT_ARCH("riscv")
ENTRY(_vectors)

MEMORY
{
    SRAM_A2 (rwx) : ORIGIN = 0x00040000, LENGTH = 208K
}

SECTIONS
{
    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors :
    {
        . = ALIGN(64);
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
    } > SRAM_A2

    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text.*)
        . = ALIGN(4);
    } > SRAM_A2

    .rodata :
    {
        . = ALIGN(4);
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(4);
    } > SRAM_A2

    /* Linux RemoteProc Resource Table (Parsed by Kernel on ELF Load) */
    .resource_table :
    {
        . = ALIGN(4);
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        . = ALIGN(4);
    } > SRAM_A2

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM_A2

    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM_A2

    /* Stack allocation at the end of SRAM A2 */
    .stack (NOLOAD) :
    {
        . = ALIGN(16);
        _sstack = .;
        . = . + 4K;
        . = ALIGN(16);
        _estack = .;
        PROVIDE(_sp = .);
    } > SRAM_A2

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
    }
}
