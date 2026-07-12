#!/bin/sh
# probe_riscv_debug.sh — Discover XuanTie E907 debug module address on T527/A523
#
# This script:
#   1. Enables the RISCV CFG bus clock via MCU CCU (confirmed offset 0x124)
#   2. Deasserts RST_BUS_MCU_RISCV_CFG (bit 16) and RST_BUS_MCU_RISCV_DEBUG (bit 17)
#   3. Scans the MCU AHB bus for the RISC-V DTM IDCODE register
#
# Based on confirmed mainline kernel data from ccu-sun55i-a523-mcu.c:
#   RST_BUS_MCU_RISCV_CFG   = { 0x0124, BIT(16) }
#   RST_BUS_MCU_RISCV_DEBUG = { 0x0124, BIT(17) }
#   RST_BUS_MCU_RISCV_CORE  = { 0x0124, BIT(18) }
#
# Usage:
#   chmod +x probe_riscv_debug.sh
#   ./probe_riscv_debug.sh
#   # Pass the found address to rbb_server:
#   rbb_server <found_address> &
#
# Requires: devmem (from busybox), /dev/mem access

set -e

MCU_CCU_BASE=0x07102000
RISCV_CLK_REG=$((MCU_CCU_BASE + 0x120))    # RISCV core clock gate
RISCV_CFG_REG=$((MCU_CCU_BASE + 0x124))    # CFG clock + all 3 RISCV resets

echo "=== T527 XuanTie E907 Debug Module Probe ==="
echo ""
echo "MCU CCU base:     $(printf '0x%08X' $MCU_CCU_BASE)"
echo "RISCV CLK reg:    $(printf '0x%08X' $RISCV_CLK_REG)"
echo "RISCV CFG reg:    $(printf '0x%08X' $RISCV_CFG_REG)"
echo ""

# Step 1: Read current state of RISCV CFG register
echo "--- Step 1: Reading current RISCV CFG register state ---"
CFG_VAL=$(devmem $(printf '0x%08X' $RISCV_CFG_REG) 32)
echo "RISCV_CFG reg = $CFG_VAL"

# Step 2: Enable bus clock (bit 0) and deassert CFG, DEBUG, CORE resets (bits 16,17,18)
# bit 0  = CLK_BUS_MCU_RISCV_CFG gate enable
# bit 16 = RST_BUS_MCU_RISCV_CFG  (write 1 to deassert = release from reset)
# bit 17 = RST_BUS_MCU_RISCV_DEBUG
# bit 18 = RST_BUS_MCU_RISCV_CORE (only release if firmware already loaded!)
echo ""
echo "--- Step 2: Enabling RISCV CFG+DEBUG clocks, releasing CFG+DEBUG resets ---"
# Enable CFG clock (bit0) + deassert CFG reset (bit16) + deassert DEBUG reset (bit17)
# NOTE: Do NOT deassert CORE reset (bit18) here — remoteproc does that at boot time
NEW_VAL=$(( (CFG_VAL | 0x00030001) ))
devmem $(printf '0x%08X' $RISCV_CFG_REG) 32 $(printf '0x%08X' $NEW_VAL)
echo "Wrote $(printf '0x%08X' $NEW_VAL) to RISCV_CFG reg"
sleep 0.1

# Step 3: Scan MCU bus range for the RISC-V DTM IDCODE
# JTAG IDCODE has bits [3:0] = 0b0001 (per IEEE 1149.1)
# XuanTie E907 IDCODE: 0x?????001 (version/manuf vary, lsbit must be 1)
# Known range: between MCU CCU (0x07102200) and I2S0 (0x07112000)
echo ""
echo "--- Step 3: Scanning MCU bus 0x07103000 - 0x07111FFF for RISC-V DTM IDCODE ---"

FOUND=""
addr=0x07103000
while [ $addr -le $((0x07112000 - 0x1000)) ]; do
    val=$(devmem $(printf '0x%08X' $addr) 32 2>/dev/null || echo "0xDEADBEEF")
    # Valid RISC-V DTM IDCODE: bit0=1, not 0xFFFFFFFF, not 0x00000000, not 0xDEADBEEF
    lsb=$(( val & 0xF ))
    if [ "$val" != "0x00000000" ] && [ "$val" != "0xFFFFFFFF" ] && \
       [ "$val" != "0xDEADBEEF" ] && [ "$lsb" -eq 1 ]; then
        echo "  CANDIDATE at $(printf '0x%08X' $addr): IDCODE = $val  <-- LIKELY MATCH"
        FOUND=$(printf '0x%08X' $addr)
    else
        echo "  $(printf '0x%08X' $addr): $val"
    fi
    addr=$(( addr + 0x1000 ))
done

echo ""
if [ -n "$FOUND" ]; then
    echo "=== FOUND: RISC-V DTM at $FOUND ==="
    echo ""
    echo "Next steps:"
    echo "  1. Start the Remote Bitbang server:"
    echo "     rbb_server $FOUND &"
    echo "  2. Start OpenOCD:"
    echo "     openocd -f /etc/openocd/openocd_t527_local.cfg"
    echo "  3. Connect GDB from dev host:"
    echo "     riscv-none-elf-gdb firmware.elf"
    echo "     (gdb) target remote cubie-a5e:3333"
    echo ""
    echo "If rbb_server works, record the address and update openocd_t527_local.cfg:"
    echo "  RBB_SERVER_ADDR=$FOUND"
else
    echo "=== NOT FOUND in standard range ==="
    echo ""
    echo "Possible reasons:"
    echo "  - Debug module is at a different address (try 0x07060000 range)"
    echo "  - RISCV_CORE reset must be deasserted first (remoteproc start needed)"
    echo "  - Debug module is only accessible after the core has executed"
    echo ""
    echo "Try: echo start > /sys/class/remoteproc/remoteproc0/state"
    echo "     then re-run this script"
fi
