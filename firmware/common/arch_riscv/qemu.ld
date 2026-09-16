/*
 * qemu.ld - QEMU Linker Script for XuanTie E907 / RISC-V Emulation
 *
 * Target: QEMU RISC-V virt machine (-M virt -cpu rv32 -m 128M)
 * RAM: 128 MB @ 0x80000000
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 128M
}

SECTIONS
{
    /* Entry point at base of RAM */
    .text.startup :
    {
        KEEP(*(.text.startup))
    } > RAM

    /* Exception vector table must be 64-byte aligned for RISC-V mtvec */
    .vectors : ALIGN(64)
    {
        KEEP(*(.vectors))
    } > RAM

    .text : ALIGN(4)
    {
        *(.text)
        *(.text.*)
    } > RAM

    .rodata : ALIGN(4)
    {
        *(.rodata)
        *(.rodata.*)
    } > RAM

    /* Linux RemoteProc Resource Table (for compatibility) */
    .resource_table : ALIGN(4)
    {
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
    } > RAM

    .data : ALIGN(4)
    {
        _sdata = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        . = ALIGN(4);
        _edata = .;
    } > RAM
    _sidata = LOADADDR(.data);

    .bss : ALIGN(4)
    {
        _sbss = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM

    /* Execution Stack (64 KB for QEMU) */
    .stack (NOLOAD) : ALIGN(16)
    {
        _stack_bottom = .;
        . += 0x10000;
        _stack_top = .;
    } > RAM

    /* RemoteProc Trace Buffer */
    .trace_buffer (NOLOAD) : ALIGN(4)
    {
        __trace_start = .;
        KEEP(*(.trace_buffer))
        KEEP(*(.trace_buffer.*))
        . = ALIGN(4);
        __trace_end = .;
    } > RAM

    /* Shared SRAM / Telemetry / IPC Buffers */
    .sram_c (NOLOAD) : ALIGN(4)
    {
        __sram_c_start = .;
        *(.sram_c)
        *(.sram_c.*)
        *(.sram_c_loc1)
        *(.sram_c_loc2)
        *(.sram_ipc .sram_ipc.*)
        *(.vring_ctrl)
        . = ALIGN(4);
        __sram_c_end = .;
    } > RAM

    .dram_payload (NOLOAD) : ALIGN(4096)
    {
        *(.dram_payload .dram_payload.*)
        *(.rpmsg_bufs)
    } > RAM

    .dtcm_scratch (NOLOAD) : ALIGN(4)
    {
        __dtcm_scratch_start = .;
        *(.dtcm_scratch)
        *(.dtcm_scratch.*)
        . = ALIGN(4);
        __dtcm_scratch_end = .;
    } > RAM

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
