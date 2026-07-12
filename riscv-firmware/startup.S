/* startup.S - XuanTie E906/E907 bootstrap assembly */

.section .vectors, "ax"
.global _enter

/* Standard RISC-V vector table at the start of ITCM (address 0x0) */
_enter:
    j _reset_handler      /* Reset vector */
    .align 2
    .rept 31
    j _trap_handler       /* Trap/interrupt vectors */
    .endr

.section .text.startup, "ax"
_reset_handler:
    /* Disable interrupts globally */
    csrc mstatus, 8

    /* 1. Initialize Stack Pointer (sp) */
    la sp, _stack_top

    /* 2. Initialize Global Pointer (gp) */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 3. Copy initialized data (.data) from SRAM to DTCM */
    la a0, _sram_data_start   /* Source address in SRAM */
    la a1, _data_start        /* Destination address in DTCM */
    la a2, _data_end          /* End of data */
    beq a0, a1, _skip_data_copy

_copy_loop:
    beq a1, a2, _skip_data_copy
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j _copy_loop

_skip_data_copy:

    /* 4. Clear BSS segment (.bss) in DTCM */
    la a0, _bss_start
    la a1, _bss_end
    li t0, 0

_bss_loop:
    beq a0, a1, _skip_bss
    sw t0, 0(a0)
    addi a0, a0, 4
    j _bss_loop

_skip_bss:

    /* 5. Jump to main function */
    tail main

_trap_handler:
    /* Loop indefinitely if a trap/exception is hit */
    j _trap_handler
