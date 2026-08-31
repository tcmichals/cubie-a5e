/*
 * XuanTie E907 / RISC-V Unified Startup & Trap Frame
 * - .text.sram_trampoline : 8-byte reset entry in SRAM (jumps directly to DRAM)
 * - .text.dram_init       : Executes _start, relocations, and C++ init from DRAM
 * - .vectors              : Copied to 0x00000000 (ITCM) for 0-wait trap handling
 */

    /* ============================================================= */
    /* SRAM Trampoline: Reset entry point at SRAM Base (0x07140000)  */
    /* ============================================================= */
    .section .text.sram_trampoline, "ax", @progbits
    .global _sram_entry
    .type _sram_entry, @function

_sram_entry:
    /* Absolute jump from SRAM to DRAM init entry point */
    la t0, _start
    jr t0

    .size _sram_entry, . - _sram_entry


    /* ============================================================= */
    /* DRAM Init: Runs CRT0, copy loops, and C++ init from DRAM      */
    /* ============================================================= */
    .section .text.dram_init, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* 1. Disable all interrupts during early initialization */
    csrw mie, zero
    csrw mip, zero

    /* 2. Setup Hardware Stack Pointer in DTCM (0x00080000) */
    la sp, _estack

    /* 3. Setup Global Pointer anchored to SRAM .data + 0x800 */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 4. Enable FPU (FS = 0b11) if hardware floating point present */
    li t0, (3 << 13)
    csrs mstatus, t0

    /* ============================================================= */
    /* Relocation: Copy from DRAM (LMA) into ITCM / SRAM (VMA)       */
    /* ============================================================= */

    /* Copy .vectors -> ITCM (0x00000000) */
    la a0, _vectors_start
    la a1, _vectors_end
    la a2, _vectors_load
.Lcopy_vectors:
    bgeu a0, a1, .Lcopy_vectors_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_vectors
.Lcopy_vectors_done:

    /* Copy .fastcode -> ITCM */
    la a0, _fastcode_start
    la a1, _fastcode_end
    la a2, _fastcode_load
.Lcopy_fastcode:
    bgeu a0, a1, .Lcopy_fastcode_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_fastcode
.Lcopy_fastcode_done:

    /* Copy .text -> SRAM C */
    la a0, _stext
    la a1, _etext
    la a2, _text_load
.Lcopy_text:
    bgeu a0, a1, .Lcopy_text_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_text
.Lcopy_text_done:

    /* Copy .rodata -> SRAM C */
    la a0, _srodata
    la a1, _erodata
    la a2, _rodata_load
.Lcopy_rodata:
    bgeu a0, a1, .Lcopy_rodata_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_rodata
.Lcopy_rodata_done:

    /* Copy .data -> SRAM C */
    la a0, _sdata
    la a1, _edata
    la a2, _sidata
.Lcopy_data:
    bgeu a0, a1, .Lcopy_data_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_data
.Lcopy_data_done:

    /* ============================================================= */
    /* Zero Initialization: .bss, .dtcm, and .dram_bss               */
    /* ============================================================= */

    /* Zero .bss in SRAM C */
    la a0, _sbss
    la a1, _ebss
.Lzero_bss:
    bgeu a0, a1, .Lzero_bss_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_bss
.Lzero_bss_done:

    /* Zero .dtcm scratchpad in DTCM */
    la a0, _sdtcm
    la a1, _edtcm
.Lzero_dtcm:
    bgeu a0, a1, .Lzero_dtcm_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_dtcm
.Lzero_dtcm_done:

    /* Zero .dram_bss in DRAM */
    la a0, _sdram_bss
    la a1, _edram_bss
.Lzero_dram:
    bgeu a0, a1, .Lzero_dram_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_dram
.Lzero_dram_done:

    /* ============================================================= */
    /* Core Trap & Interrupt Configuration                          */
    /* ============================================================= */

    /* Point mtvec CSR to vector entry in ITCM (0x00000000) */
    la t0, default_trap_entry
    csrw mtvec, t0

    /* Instruction pipeline barrier */
    fence.i

    /* ============================================================= */
    /* C++ Runtime Construction & Execution                          */
    /* ============================================================= */

    /* Execute global static C++ constructors (tables live in DRAM) */
    la t0, __libc_init_array
    jalr ra, t0, 0

    /* Jump to C++ Application Entry in SRAM */
    la t0, main
    jalr ra, t0, 0

    /* Call destructors if main returns */
    la t0, __libc_fini_array
    jalr ra, t0, 0

.Lhang:
    wfi
    j .Lhang

    .size _start, . - _start


/* ================================================================= */
/* Hardware Trap Vector Entry & Context Preservation Frame (ITCM)    */
/* ================================================================= */
    .section .vectors, "ax", @progbits
    .align 6
    .global default_trap_entry
    .type default_trap_entry, @function

default_trap_entry:
    /* Allocate 64 bytes on DTCM stack and save caller-saved registers */
    addi sp, sp, -64
    sw ra,  0(sp)
    sw t0,  4(sp)
    sw t1,  8(sp)
    sw t2, 12(sp)
    sw a0, 16(sp)
    sw a1, 20(sp)
    sw a2, 24(sp)
    sw a3, 28(sp)
    sw a4, 32(sp)
    sw a5, 36(sp)
    sw a6, 40(sp)
    sw a7, 44(sp)
    sw t3, 48(sp)
    sw t4, 52(sp)
    sw t5, 56(sp)
    sw t6, 60(sp)

    /* Pass mcause (a0) and mepc (a1) to C++ dispatcher */
    csrr a0, mcause
    csrr a1, mepc
    call riscv_trap_dispatcher

    /* Restore caller-saved registers */
    lw ra,  0(sp)
    lw t0,  4(sp)
    lw t1,  8(sp)
    lw t2, 12(sp)
    lw a0, 16(sp)
    lw a1, 20(sp)
    lw a2, 24(sp)
    lw a3, 28(sp)
    lw a4, 32(sp)
    lw a5, 36(sp)
    lw a6, 40(sp)
    lw a7, 44(sp)
    lw t3, 48(sp)
    lw t4, 52(sp)
    lw t5, 56(sp)
    lw t6, 60(sp)
    addi sp, sp, 64

    /* Return from Machine Trap / Interrupt */
    mret
    .size default_trap_entry, . - default_trap_entry
    