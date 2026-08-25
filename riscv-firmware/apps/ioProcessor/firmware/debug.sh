#!/usr/bin/env bash
# Quick helper to connect GDB to OpenOCD for XuanTie E907
GDB_BIN="${CROSS_COMPILE}gdb"
if ! command -v "$GDB_BIN" &> /dev/null; then
    if command -v riscv32-unknown-elf-gdb &> /dev/null; then
        GDB_BIN="riscv32-unknown-elf-gdb"
    elif command -v riscv64-unknown-elf-gdb &> /dev/null; then
        GDB_BIN="riscv64-unknown-elf-gdb"
    elif command -v gdb-multiarch &> /dev/null; then
        GDB_BIN="gdb-multiarch"
    else
        GDB_BIN="gdb"
    fi
fi

echo "Starting GDB with $GDB_BIN on ioprocessor_firmware.elf..."
$GDB_BIN -x gdbinit build/ioprocessor_firmware.elf
