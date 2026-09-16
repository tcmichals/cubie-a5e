/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Cadence Tensilica HiFi4 Audio DSP Linker Script
 * Allwinner T527 / A527 (Radxa Cubie A5E)
 *
 * Maps firmware to the 4 MB dedicated execution window at 0x40100000
 * and reserves 1 MB shared DMA buffer window at 0x40000000.
 */

MEMORY
{
    SHM (rwx)      : ORIGIN = 0x40000000, LENGTH = 1M
    FIRMWARE (rx)  : ORIGIN = 0x40100000, LENGTH = 3M
    DATA (rwx)     : ORIGIN = 0x40400000, LENGTH = 1M
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
    } > FIRMWARE

    .resource_table : ALIGN(1024) {
        __resource_table_start = .;
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        __resource_table_end = .;
        . = ALIGN(4);
    } > FIRMWARE

    .data : {
        _sdata = .;
        *(.data*)
        _edata = .;
        . = ALIGN(4);
    } > DATA

    .trace_buffer : ALIGN(64) {
        _strace = .;
        KEEP(*(.trace_buffer))
        _etrace = .;
    } > DATA

    .bss (NOLOAD) : {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        _ebss = .;
        . = ALIGN(4);
    } > DATA

    .stack (NOLOAD) : {
        . = ALIGN(16);
        _sstack = .;
        . = . + 0x4000; /* 16 KB Stack */
        _estack = .;
    } > DATA
}
