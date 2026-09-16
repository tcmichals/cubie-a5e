/*
 * startup.S - XuanTie E907 RISC-V Direct SRAM Startup & Vector Table
 */

    .section .vectors, "ax", @progbits
    .global _vectors
    .type _vectors, @function

_vectors:
    j _start            /* Vector 0: Reset */
    .align 2
    .rept 31
    j _trap_handler
    .endr

_trap_handler:
    wfi
    j _trap_handler

    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function

_start:
    li t0, 0x00050000      /* SRAM A2 probe address */
    li t1, 0x4E010000      /* DDR Carveout probe address */
    li t2, 0x31313131      /* Test signature */

.Lloop:
    sw t2, 0(t0)
    sw t2, 0(t1)
    addi t2, t2, 1
    j .Lloop

.Lhang:
    wfi
    j .Lhang

    .size _start, . - _start
