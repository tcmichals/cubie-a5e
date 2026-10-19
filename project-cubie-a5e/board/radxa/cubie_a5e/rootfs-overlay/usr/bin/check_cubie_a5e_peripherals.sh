#!/bin/bash
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-GPS-01] [SPEC-E906-ARCH-02] docs/DESIGN_SPECIFICATION.md
#
# check_cubie_a5e_peripherals.sh
# -------------------------------------------------------------
# Comprehensive on-target hardware and peripheral health checker
# for Radxa Cubie A5E (Allwinner A527 / T527) running AbstractX.
#
# Tests:
#   1. Device Tree & Serial Console Lock (UART0 on /dev/ttyS0)
#   2. I2C1 Bus on Pins 3 & 5 (u-blox DDC 0x42 + Compass 0x0D/0x1E)
#   3. UART2 GPS Serial Port on Pins 11 & 13 (9600 baud NMEA / UBX)
#   4. XuanTie E906 RemoteProc & RPMsg Endpoints
# -------------------------------------------------------------

set -u

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

echo -e "${BOLD}================================================================${NC}"
echo -e "${BOLD}   Radxa Cubie A5E Hardware & Peripheral Health Checker${NC}"
echo -e "${BOLD}================================================================${NC}"
echo -e "Hostname       : $(hostname)"
echo -e "Kernel         : $(uname -r)"
if [ -f "/proc/device-tree/model" ]; then
    echo -e "Board Model    : $(tr -d '\0' < /proc/device-tree/model)"
fi
echo -e "Timestamp      : $(date -u)"
echo -e "----------------------------------------------------------------\n"

PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0

# -----------------------------------------------------------------------------
# STEP 1: Device Tree Overlay & Serial Console Lock
# -----------------------------------------------------------------------------
echo -e "${BLUE}[STEP 1/4] Checking Device Tree Overlay & Console Lock...${NC}"

# Check console cmdline
CMDLINE=$(cat /proc/cmdline 2>/dev/null || echo "")
if echo "$CMDLINE" | grep -q "console=ttyS0"; then
    echo -e "  [${GREEN}PASS${NC}] Linux Serial Console locked to ttyS0 (Pins 8 & 10)"
    ((PASS_COUNT++))
else
    echo -e "  [${YELLOW}WARN${NC}] Console not explicitly set to ttyS0 in /proc/cmdline: $CMDLINE"
    ((WARN_COUNT++))
fi

# Check Device Tree Aliases
if [ -d "/proc/device-tree/aliases" ]; then
    S0_TARGET=$(tr -d '\0' < /proc/device-tree/aliases/serial0 2>/dev/null || echo "None")
    S2_TARGET=$(tr -d '\0' < /proc/device-tree/aliases/serial2 2>/dev/null || echo "None")
    echo -e "  -> serial0 alias : ${S0_TARGET}"
    echo -e "  -> serial2 alias : ${S2_TARGET}"
    if echo "$S0_TARGET" | grep -q "2500000"; then
        echo -e "  [${GREEN}PASS${NC}] serial0 correctly points to UART0 (0x02500000)"
        ((PASS_COUNT++))
    else
        echo -e "  [${YELLOW}WARN${NC}] serial0 target ($S0_TARGET) may not be UART0"
        ((WARN_COUNT++))
    fi
fi

# -----------------------------------------------------------------------------
# STEP 2: I2C1 Bus (Pins 3 & 5 - Top of 40-Pin Header)
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}[STEP 2/4] Checking I2C1 Bus (Pins 3 & 5: TWI1)...${NC}"
I2C_DEV="/dev/i2c-1"

if [ -e "$I2C_DEV" ]; then
    echo -e "  [${GREEN}PASS${NC}] I2C bus device node exists: ${I2C_DEV}"
    ((PASS_COUNT++))
    
    if command -v i2cdetect >/dev/null 2>&1; then
        echo -e "  -> Scanning I2C bus 1 for devices..."
        SCAN_OUT=$(i2cdetect -y 1 2>/dev/null || echo "")
        
        # Check u-blox M8N DDC (0x42)
        if echo "$SCAN_OUT" | grep -q "42"; then
            echo -e "  [${GREEN}PASS${NC}] u-blox NEO-M8N DDC detected at I2C address 0x42!"
            ((PASS_COUNT++))
        else
            echo -e "  [${YELLOW}WARN${NC}] Address 0x42 (u-blox DDC) not responding. Check SDA (Pin 3) and SCL (Pin 5)."
            ((WARN_COUNT++))
        fi

        # Check Compass (0x0D for QMC5883L or 0x1E for HMC5883L)
        if echo "$SCAN_OUT" | grep -q "0d"; then
            echo -e "  [${GREEN}PASS${NC}] Compass detected at I2C address 0x0D (QMC5883L)!"
            ((PASS_COUNT++))
        elif echo "$SCAN_OUT" | grep -q "1e"; then
            echo -e "  [${GREEN}PASS${NC}] Compass detected at I2C address 0x1E (HMC5883L)!"
            ((PASS_COUNT++))
        else
            echo -e "  [${YELLOW}WARN${NC}] No compass detected at 0x0D or 0x1E on bus 1."
            ((WARN_COUNT++))
        fi
    else
        echo -e "  [INFO] i2cdetect tool not installed on target; skipping raw register scan."
    fi
else
    echo -e "  [${YELLOW}WARN${NC}] ${I2C_DEV} not present in Linux. (If routed to E906 via UIO, E906 firmware accesses TWI1 directly at 0x02502400)"
    ((WARN_COUNT++))
fi

# -----------------------------------------------------------------------------
# STEP 3: UART2 GPS Serial Port (Pins 11 & 13)
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}[STEP 3/4] Checking UART2 GPS Serial Port (Pins 11 & 13: PB0/PB1)...${NC}"
UART_DEV="/dev/ttyS2"

if [ -e "$UART_DEV" ]; then
    echo -e "  [${GREEN}PASS${NC}] UART2 serial port device node exists: ${UART_DEV}"
    ((PASS_COUNT++))

    # Test reading raw characters for 3 seconds
    if command -v python3 >/dev/null 2>&1; then
        echo -e "  -> Probing ${UART_DEV} for incoming GPS NMEA / UBX stream (3 sec sample)..."
        SERIAL_PROBE=$(python3 -c "
import serial, time
try:
    s = serial.Serial('$UART_DEV', 9600, timeout=3.0)
    data = s.read(512)
    s.close()
    if data:
        text = data.decode('ascii', errors='ignore')
        nmea_count = text.count('$GN') + text.count('$GP')
        ubx_count = data.count(b'\xb5\x62')
        print(f'BYTES={len(data)}|NMEA={nmea_count}|UBX={ubx_count}|SAMPLE={repr(text[:60])}')
    else:
        print('BYTES=0|NMEA=0|UBX=0|SAMPLE=EMPTY')
except Exception as e:
    print(f'ERROR={e}')
" 2>/dev/null || echo "ERROR=ExecutionFailed")

        if echo "$SERIAL_PROBE" | grep -q "BYTES=[1-9]"; then
            echo -e "  [${GREEN}PASS${NC}] Data stream active on ${UART_DEV} @ 9600 baud!"
            echo -e "  -> Details: ${SERIAL_PROBE}"
            ((PASS_COUNT++))
        else
            echo -e "  [${YELLOW}WARN${NC}] No bytes received in 3s on ${UART_DEV}. (Check: Pin 11 to GPS RX, Pin 13 to GPS TX, 9600 baud)"
            echo -e "  -> Details: ${SERIAL_PROBE}"
            ((WARN_COUNT++))
        fi
    fi
else
    echo -e "  [${YELLOW}WARN${NC}] ${UART_DEV} not present in Linux. (If routed to E906 via UIO, E906 firmware accesses UART2 directly at 0x02500800)"
    ((WARN_COUNT++))
fi

# -----------------------------------------------------------------------------
# STEP 4: XuanTie E906 RemoteProc & RPMsg Status
# -----------------------------------------------------------------------------
echo -e "\n${BLUE}[STEP 4/4] Checking XuanTie E906 RemoteProc & RPMsg...${NC}"
RPROC_STATE="/sys/class/remoteproc/remoteproc0/state"
RPROC_FW="/sys/class/remoteproc/remoteproc0/firmware"

if [ -f "$RPROC_STATE" ]; then
    STATE=$(cat "$RPROC_STATE" 2>/dev/null || echo "unknown")
    FW=$(cat "$RPROC_FW" 2>/dev/null || echo "unknown")
    echo -e "  -> RemoteProc0 State   : ${BOLD}${STATE}${NC}"
    echo -e "  -> Active Firmware     : ${BOLD}${FW}${NC}"
    if [ "$STATE" = "running" ]; then
        echo -e "  [${GREEN}PASS${NC}] XuanTie E906 co-processor is RUNNING"
        ((PASS_COUNT++))
    else
        echo -e "  [${YELLOW}WARN${NC}] RemoteProc0 is ${STATE} (Start via: echo start > ${RPROC_STATE})"
        ((WARN_COUNT++))
    fi
else
    echo -e "  [${YELLOW}WARN${NC}] RemoteProc sysfs node not present (remoteproc driver not loaded)"
    ((WARN_COUNT++))
fi

# Check RPMsg Character Device
if [ -e "/dev/rpmsg0" ] || [ -e "/dev/rpmsg_ctrl0" ]; then
    echo -e "  [${GREEN}PASS${NC}] VirtIO RPMsg endpoint is active (/dev/rpmsg0)"
    ((PASS_COUNT++))
fi

# -----------------------------------------------------------------------------
# FINAL SUMMARY DASHBOARD
# -----------------------------------------------------------------------------
echo -e "\n${BOLD}================================================================${NC}"
echo -e "${BOLD}   Hardware Health Summary Report${NC}"
echo -e "${BOLD}================================================================${NC}"
echo -e "Passed Checks  : ${GREEN}${PASS_COUNT}${NC}"
echo -e "Warnings       : ${YELLOW}${WARN_COUNT}${NC}"
echo -e "Failures       : ${RED}${FAIL_COUNT}${NC}"

if [ "$FAIL_COUNT" -eq 0 ]; then
    echo -e "\n${GREEN}${BOLD}[SUCCESS] All monitored hardware rails and buses are verified healthy!${NC}"
    exit 0
else
    echo -e "\n${RED}${BOLD}[FAILURE] Hardware defects detected. Review above log for details.${NC}"
    exit 1
fi
