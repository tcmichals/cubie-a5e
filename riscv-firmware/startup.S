/* startup.S - XuanTie E906/E907 bootstrap assembly */

.section .vectors, "ax"
.global _enter
.global _vectors

_vectors:
_enter:
    /* eGON Header (32 bytes) */
    j _reset_handler
    .ascii "eGON.BT0"
    .word 0x5F0A6C39      /* Checksum */
    .word 0x00002000      /* Size */
    .word 0x00000020      /* Header size */
    .word 0x00000100      /* Version */
    .word 0x00000000      /* Reserved */

    /* Standard RISC-V Trap vectors */
    .align 2
    .rept 24
    j _trap_handler
    .endr   /* Trap/interrupt vectors */

.section .text.startup, "ax"
.global _reset_handler
_reset_handler:
    /* 1. Disable interrupts */
    csrci mstatus, 8

    /* 2. Set MTvec base */
    la t0, _vectors
    csrw mtvec, t0

    /* 3. Initialize Stack Pointer (sp) */
    la sp, _stack_top

    /* 4. Initialize Global Pointer (gp) */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 5. Clear BSS segment */
    la a0, _bss_start
    la a1, _bss_end
    li t0, 0
_bss_loop:
    beq a0, a1, _skip_bss
    sw t0, 0(a0)
    addi a0, a0, 4
    j _bss_loop

_skip_bss:
    /* 6. Jump directly to main */
    tail main

.global _trap_handler
_trap_handler:
    /* Store trap counter in fixed SRAM offset 0x00028020 */
    li t0, 0x00028020
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    wfi
    j _trap_handler
