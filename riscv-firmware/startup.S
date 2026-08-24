/* startup.S - XuanTie E906/E907 bootstrap assembly for Option A */

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
    .endr

.section .text.startup, "ax"
.global _reset_handler
_reset_handler:
    /* 1. Disable interrupts */
    csrci mstatus, 8

    /* 2. Set MTvec base to vectors at 0x00000000 */
    la t0, _vectors
    csrw mtvec, t0

    /* 3. Initialize Stack Pointer (sp) in SRAM C */
    la sp, _stack_top

    /* 4. Initialize Global Pointer (gp) */
    .option push
    .option norelax
    la gp, __global_pointer$
    .option pop

    /* 5. Copy initialized .data (if LMA != VMA) */
    la a0, _sram_data_start
    la a1, _data_start
    beq a0, a1, _data_copy_done
    la a2, _data_end
_data_copy_loop:
    bgeu a1, a2, _data_copy_done
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j _data_copy_loop
_data_copy_done:

    /* 6. Clear BSS segment */
    la a0, _bss_start
    la a1, _bss_end
    li t0, 0
_bss_loop:
    bgeu a0, a1, _skip_bss
    sw t0, 0(a0)
    addi a0, a0, 4
    j _bss_loop

_skip_bss:
    /* 7. Jump directly to main */
    tail main

.global _trap_handler
_trap_handler:
    /* Store trap counter in fixed telemetry offset 0x00037018 */
    li t0, 0x00037018
    lw t1, 0(t0)
    addi t1, t1, 1
    sw t1, 0(t0)
    wfi
    j _trap_handler
