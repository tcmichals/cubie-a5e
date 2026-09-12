/*
 * startup.S - XuanTie E907 (RV32IMAFDC) Unified Startup & Exception Vector Table
 */

    .section .vectors, "ax", @progbits
    .align 6
    .global _vectors
    .type _vectors, @function
    .global default_trap_entry
    .type default_trap_entry, @function

/*
 * =====================================================================
 * Vector 0 (Reset Vector): Branch immediately to _start
 * =====================================================================
 */
_vectors:
    j _start
    .align 4

/*
 * =====================================================================
 * Direct Mode Trap Entry Point (mtvec)
 * =====================================================================
 */
default_trap_entry:
    /* Allocate 144 bytes on stack for CrashFrame */
    addi sp, sp, -144

    /* Save caller-saved registers and critical GPRs */
    sw ra,   0(sp)
    
    /* -----------------------------------------------------------------
     * FIX 1: Save original t0 BEFORE using it for scratch calculations.
     * In the previous version, 'addi t0, sp, 144' overwrote t0, so
     * saving it to 16(sp) dumped the computed SP instead of original t0.
     * ----------------------------------------------------------------- */
    sw t0,  16(sp)

    /* Calculate and store the original sp prior to this trap entry */
    addi t0, sp, 144
    sw t0,   4(sp)

    /* Save remaining GPRs x3..x31 */
    sw gp,   8(sp)
    sw tp,  12(sp)
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

    /* Save RISC-V Machine Control and Status Registers (CSRs) */
    csrr t0, mepc
    sw t0,  124(sp)
    csrr t1, mcause
    sw t1,  128(sp)
    csrr t0, mtval
    sw t0,  132(sp)
    csrr t0, mstatus
    sw t0,  136(sp)

    /* Check if this trap is an asynchronous interrupt (mcause bit 31). */
    bltz t1, .Lhandle_irq

    /* Pass pointer to CrashFrame (sp) in a0 to C++ crash dispatcher */
    mv a0, sp
    call hal_crash_dispatcher

.Ltrap_hang:
    wfi
    j .Ltrap_hang

.Lhandle_irq:
    /* Call C++ interrupt dispatcher: void riscv_trap_dispatcher(mcause, mepc) */
    lw a0, 128(sp) /* mcause */
    lw a1, 124(sp) /* mepc */
    call riscv_trap_dispatcher

    /* Restore CSRs */
    lw t0, 136(sp)
    csrw mstatus, t0
    lw t0, 124(sp)
    csrw mepc, t0

    /* Restore all GPRs */
    lw ra,   0(sp)
    lw gp,   8(sp)
    lw tp,  12(sp)
    lw t0,  16(sp)
    lw t1,  20(sp)
    lw t2,  24(sp)
    lw s0,  28(sp)
    lw s1,  32(sp)
    lw a0,  36(sp)
    lw a1,  40(sp)
    lw a2,  44(sp)
    lw a3,  48(sp)
    lw a4,  52(sp)
    lw a5,  56(sp)
    lw a6,  60(sp)
    lw a7,  64(sp)
    lw s2,  68(sp)
    lw s3,  72(sp)
    lw s4,  76(sp)
    lw s5,  80(sp)
    lw s6,  84(sp)
    lw s7,  88(sp)
    lw s8,  92(sp)
    lw s9,  96(sp)
    lw s10, 100(sp)
    lw s11, 104(sp)
    lw t3,  108(sp)
    lw t4,  112(sp)
    lw t5,  116(sp)
    lw t6,  120(sp)

    /* Deallocate stack frame and return from trap */
    addi sp, sp, 144
    mret

    .size default_trap_entry, . - default_trap_entry


    /* ============================================================= */
    /* Core Reset Entry Point                                        */
    /* ============================================================= */
    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* 1. Set mtvec FIRST before any other instruction can fault */
    la t0, default_trap_entry
    csrw mtvec, t0

    /* 2. Disable interrupt generation (mie only, mip is read-only status) */
    csrw mie, zero

    /* 3. Write diagnostic signature (0xDEADBEEF) directly from ASM */
    /* This proves the core is executing instructions at reset */
    la t0, __sram_c_start
    li t1, 0xdeadbeef
    sw t1, 0(t0)

    /* 4. Setup Hardware Stack Pointer in SRAM */
    la sp, _stack_top

    /* 5. Setup Global Pointer */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 5b. Enable FPU in mstatus (mstatus.FS = 0b01 Initial) */
    li t0, (1 << 13) | (1 << 14)
    csrs mstatus, t0
    csrw fcsr, zero

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