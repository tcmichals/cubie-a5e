/*
 * startup.S - XuanTie E907 RISC-V Direct SRAM Startup & Vector Table
 */

    .section .vectors, "ax", @progbits
    .global _vectors
    .type _vectors, @function

_vectors:
    j _start            /* Vector 0: Reset */
    .align 2
    .word 0             /* Vector 1  */
    .word 0             /* Vector 2  */
    .word 0             /* Vector 3: Machine software interrupt */
    .word 0             /* Vector 4  */
    .word 0             /* Vector 5  */
    .word 0             /* Vector 6  */
    .word 0             /* Vector 7: Machine timer interrupt */
    .word 0             /* Vector 8  */
    .word 0             /* Vector 9  */
    .word 0             /* Vector 10 */
    .word 0             /* Vector 11: Machine external interrupt */

    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    /* 1. Disable all interrupts */
    csrw mie, zero
    csrw mip, zero

    /* 2. Setup Stack Pointer in SRAM */
    la sp, _stack_top

    /* 3. Setup Global Pointer anchored to .data + 0x800 */
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    /* 4. Enable FPU if hardware present (mstatus.FS = 3) */
    li t0, (3 << 13)
    csrs mstatus, t0

    /* 5. Copy initialized data from ITCM load image to DTCM runtime address */
    la a0, _sram_data_start
    la a1, _data_start
    la a2, _data_end
.Lcopy_data:
    bgeu a1, a2, .Lcopy_data_done
    lw t0, 0(a0)
    sw t0, 0(a1)
    addi a0, a0, 4
    addi a1, a1, 4
    j .Lcopy_data
.Lcopy_data_done:

    /* 6. Clear BSS in DTCM */
    la a0, _bss_start
    la a1, _bss_end
.Lclear_bss:
    bgeu a0, a1, .Lclear_bss_done
    sw zero, 0(a0)
    addi a0, a0, 4
    j .Lclear_bss
.Lclear_bss_done:

    /* 7. Call main() */
    call main

    /* 7. Hang loop if main returns */
.Lhang:
    wfi
    j .Lhang

    .size _start, . - _start
