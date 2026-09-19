/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Cadence Tensilica HiFi4 Audio DSP / MCU Linker Script
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Maps firmware to the valid on-chip SRAM Space 0 at 0x3FFC0000 (256 KB)
 */

MEMORY
{
    SRAM (rwx) : ORIGIN = 0x3FFC0000, LENGTH = 256K
}

ENTRY(_start)

SECTIONS
{
    .text : {
        _start = .;
        *(.entry)
        *(.literal .literal.*)
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > SRAM

    .resource_table : ALIGN(1024) {
        __resource_table_start = .;
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        __resource_table_end = .;
        . = ALIGN(4);
    } > SRAM

    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
        . = ALIGN(4);
    } > SRAM

    .trace_buffer : ALIGN(64) {
        _strace = .;
        KEEP(*(.trace_buffer))
        _etrace = .;
    } > SRAM

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
        . = ALIGN(4);
    } > SRAM

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + 0x4000; /* 16 KB Stack */
        _estack = .;
    } > SRAM
}

