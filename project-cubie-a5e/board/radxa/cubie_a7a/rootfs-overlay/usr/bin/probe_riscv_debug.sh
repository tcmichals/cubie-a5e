#!/bin/sh
# probe_riscv_debug.sh - Discover XuanTie E907 debug module address on T527/A733
#
# This script:
#   1. Enables the RISCV CFG bus clock via MCU CCU
#   2. Deasserts RST_BUS_MCU_RISCV_CFG and RST_BUS_MCU_RISCV_DEBUG
#   3. Scans the MCU AHB bus for the RISC-V DTM IDCODE register

set -e

MCU_CCU_BASE=0x07102000
RISCV_CLK_REG=$((MCU_CCU_BASE + 0x120))
RISCV_CFG_REG=$((MCU_CCU_BASE + 0x124))

echo "=== XuanTie E907 Debug Module Probe ==="
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
echo ""
echo "--- Step 2: Enabling RISCV CFG+DEBUG clocks, releasing CFG+DEBUG resets ---"
NEW_VAL=$(( (CFG_VAL | 0x00070001) ))
devmem $(printf '0x%08X' $RISCV_CFG_REG) 32 $(printf '0x%08X' $NEW_VAL)
echo "Wrote $(printf '0x%08X' $NEW_VAL) to RISCV_CFG reg"
sleep 0.1

# Step 3: Scan MCU bus range for the RISC-V DTM IDCODE
echo ""
echo "--- Step 3: Probing XuanTie Debug Module (0x07090000) and MCU bus ---"

FOUND=""
val=$(devmem 0x07090000 32 2>/dev/null || echo "0xDEADBEEF")
echo "  Primary XuanTie DBG Base (0x07090000): $val"
if [ "$val" != "0x00000000" ] && [ "$val" != "0xFFFFFFFF" ] && [ "$val" != "0xDEADBEEF" ]; then
    echo "  SUCCESS at 0x07090000: dmstatus = $val  <-- XuanTie E907 DBG MODULE ACTIVE"
    FOUND="0x07090000"
fi

addr=$((0x07103000))
end_addr=$((0x07112000 - 0x1000))
while [ $addr -le $end_addr ]; do
    hex_addr=$(printf '0x%08X' $addr)
    val=$(devmem $hex_addr 32 2>/dev/null || echo "0xDEADBEEF")
    lsb=$(( val & 0xF ))
    if [ "$val" != "0x00000000" ] && [ "$val" != "0xFFFFFFFF" ] && \
       [ "$val" != "0xDEADBEEF" ] && [ "$lsb" -eq 1 ]; then
        echo "  CANDIDATE at $hex_addr: IDCODE = $val  <-- LIKELY MATCH"
        FOUND=$hex_addr
    else
        echo "  $hex_addr: $val"
    fi
    addr=$(( addr + 4096 ))
done

echo ""
if [ -n "$FOUND" ]; then
    echo "=== FOUND: RISC-V DTM at $FOUND ==="
    echo "  1. Start rbb_server: rbb_server $FOUND &"
    echo "  2. Start OpenOCD: openocd -f /etc/openocd/openocd_t527_local.cfg"
else
    echo "=== NOT FOUND in standard range ==="
fi
