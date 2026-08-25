/*
 * Startup & CRT0 Bootstrap for XuanTie E907 RISC-V Co-Processor
 * Features:
 *   - Vector table at ITCM base (0x00000000)
 *   - Fastcode copying from SRAM to ITCM
 *   - Data copying from SRAM to DTCM
 *   - BSS zeroing in DTCM
 *   - Hardware Machine Software Interrupt (MSIP) dispatcher
 *   - Crash Dump register snapshot on fatal trap
 */

    .section .vectors, "ax"
    .align 6
    .globl _vectors
    .globl _start

_vectors:
    j _start                /* Reset Handler */
    .align 2
    j _trap_entry           /* Trap / Exception Handler */
    .align 2
    j _trap_entry
    .align 2
    j _software_irq_entry   /* Machine Software Interrupt (MSIP) */
    .align 2
    j _trap_entry
    .align 2
    j _trap_entry
    .align 2
    j _trap_entry
    .align 2
    j _timer_irq_entry      /* Machine Timer Interrupt */
    .align 2
    j _trap_entry
    .align 2
    j _trap_entry
    .align 2
    j _trap_entry
    .align 2
    j _external_irq_entry   /* Machine External Interrupt (PLIC) */

    .section .text.startup, "ax"
    .align 2
_start:
    /* Disable interrupts during bootstrap */
    csrw mie, zero
    csrw mip, zero

    /* Set global pointer and stack pointer in DTCM */
.option push
.option norelax
    la gp, __global_pointer$
.option pop
    la sp, _estack

    /* Set trap vector base address */
    la t0, _trap_entry
    csrw mtvec, t0

    /* Copy .fastcode section from SRAM C to ITCM */
    la a0, _fastcode_start
    la a1, _fastcode_end
    la a2, _fastcode_load
1:
    bgeu a0, a1, 2f
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 1b
2:

    /* Copy .data section from SRAM C to DTCM */
    la a0, _sdata
    la a1, _edata
    la a2, _sidata
3:
    bgeu a0, a1, 4f
    lw t0, 0(a2)
    sw t0, 0(a0)
    addi a0, a0, 4
    addi a2, a2, 4
    j 3b
4:

    /* Zero-out .bss section in DTCM */
    la a0, _sbss
    la a1, _ebss
5:
    bgeu a0, a1, 6f
    sw zero, 0(a0)
    addi a0, a0, 4
    j 5b
6:

    /* Enable Machine Software Interrupts (MSIE) and External Interrupts (MEIE) */
    li t0, (1 << 3) | (1 << 11)  /* MSIE | MEIE */
    csrs mie, t0

    /* Enable global interrupts (MIE) in mstatus */
    li t0, (1 << 3)
    csrs mstatus, t0

    /* Jump to C++ main */
    call main

    /* If main returns, loop safely in low-power WFI */
_halt_loop:
    wfi
    j _halt_loop

/*
 * Machine Software Interrupt (MSIP) Entry
 */
    .section .text.fastcode, "ax"
    .align 2
_software_irq_entry:
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

    /* Call C++ MSIP handler to dispatch AbstractX wakeup queue */
    call msip_dispatch_events

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
    mret

/*
 * External Interrupt (PLIC) Entry
 */
    .section .text.fastcode, "ax"
    .align 2
_external_irq_entry:
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

    call plic_dispatch_interrupt

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
    mret

/*
 * Timer Interrupt Entry
 */
    .section .text.fastcode, "ax"
    .align 2
_timer_irq_entry:
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

    call timer_dispatch_interrupt

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
    mret

/*
 * Fatal Trap / Hard Fault Entry
 * Saves all registers into Crash Dump Area at 0x07130000 in SRAM C
 */
    .section .text.fastcode, "ax"
    .align 2
_trap_entry:
    /* Check if it is an interrupt (bit 31 of mcause == 1) */
    csrr t0, mcause
    bgez t0, _fatal_exception

    /* If software interrupt handled in direct mtvec mode */
    li t1, 0x80000003
    beq t0, t1, _software_irq_entry

    /* If external interrupt */
    li t1, 0x8000000B
    beq t0, t1, _external_irq_entry

    mret

_fatal_exception:
    /* Write Magic 'CRSH' and Register State to 0x07130000 */
    li t0, 0x07130000
    li t1, 0x48535243       /* Magic: 'CRSH' */
    sw t1, 0(t0)

    csrr t1, mepc
    sw t1, 4(t0)
    csrr t1, mcause
    sw t1, 8(t0)
    csrr t1, mtval
    sw t1, 12(t0)
    sw ra, 16(t0)
    sw sp, 20(t0)
    sw gp, 24(t0)
    sw a0, 28(t0)
    sw a1, 32(t0)

_fatal_hang:
    wfi
    j _fatal_hang
