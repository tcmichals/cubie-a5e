/*
 * startup.S - XuanTie E907 (RV32IMAFDC) Unified Startup & Exception Vector Table
 */

    .section .vectors, "ax", @progbits
    .align 6
    .global _vectors
    .type _vectors, @function

_vectors:
    j _start                /* Vector 0: Reset Entry */
    .align 2
    .rept 31
    j default_trap_entry    /* Vectors 1..31: Hardware Traps & IRQs */
    .endr

    .global default_trap_entry
    .type default_trap_entry, @function

default_trap_entry:
    /* Allocate 144 bytes on stack for CrashFrame */
    addi sp, sp, -144

    /* Save GPRs x1..x31 */
    sw ra,   0(sp)
    /* Save original sp before trap */
    addi t0, sp, 144
    sw t0,   4(sp)
    sw gp,   8(sp)
    sw tp,  12(sp)
    sw t0,  16(sp)
    sw t1,  20(sp)
    sw t2,  24(sp)
    sw s0,  28(sp)
    sw s1,  32(sp)
    sw a0,  36(sp)
    sw a1,  40(sp)
    sw a2,  44(sp)
    sw a3,  48(sp)
    sw a4,  52(sp)
    sw a5,  56(sp)
    sw a6,  60(sp)
    sw a7,  64(sp)
    sw s2,  68(sp)
    sw s3,  72(sp)
    sw s4,  76(sp)
    sw s5,  80(sp)
    sw s6,  84(sp)
    sw s7,  88(sp)
    sw s8,  92(sp)
    sw s9,  96(sp)
    sw s10, 100(sp)
    sw s11, 104(sp)
    sw t3,  108(sp)
    sw t4,  112(sp)
    sw t5,  116(sp)
    sw t6,  120(sp)

    /* Save CSRs */
    csrr t0, mepc
    sw t0,  124(sp)
    csrr t0, mcause
    sw t0,  128(sp)
    csrr t0, mtval
    sw t0,  132(sp)
    csrr t0, mstatus
    sw t0,  136(sp)

    /* Pass pointer to CrashFrame in a0 to C++ crash dispatcher */
    mv a0, sp
    call hal_crash_dispatcher

.Ltrap_hang:
    wfi
    j .Ltrap_hang

    .size default_trap_entry, . - default_trap_entry

    /* ============================================================= */
    /* Core Reset Entry Point                                        */
    /* ============================================================= */
    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* 1. Disable all interrupts */
    csrw mie, zero
    csrw mip, zero

    /* 2. Setup Hardware Stack Pointer in SRAM */
    la sp, _stack_top

    /* 3. Setup Global Pointer */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 4. Set mtvec CSR to vector table in SRAM */
    la t0, _vectors
    csrw mtvec, t0

    /* 5. Enable FPU (mstatus.FS = 0b11) for hardware float/double */
    li t0, (3 << 13)
    csrs mstatus, t0

    /* 6. Copy .data from LMA to VMA if separate */
    la a0, _sdata
    la a1, _edata
    la a2, _sidata
    beq a0, a2, .Lcopy_data_done
.Lcopy_data:
    bgeu a0, a1, .Lcopy_data_done
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j .Lcopy_data
.Lcopy_data_done:

    /* 7. Zero .bss in SRAM */
    la a0, _sbss
    la a1, _ebss
.Lzero_bss:
    bgeu a0, a1, .Lzero_bss_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lzero_bss
.Lzero_bss_done:

    /* 8. Call global static C++ constructors (if any) */
    la t0, __libc_init_array
    beqz t0, .Lcall_main
    jalr ra, t0, 0

.Lcall_main:
    /* 9. Jump to application main */
    call main

.Lhang:
    wfi
    j .Lhang

    .size _start, . - _start