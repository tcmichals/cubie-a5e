#!/bin/bash
# =================================================================
# COMPREHENSIVE RUNTIME VALIDATION SCRIPT FOR T527 COPROCESSORS
# =================================================================
# Supports Allwinner T527 / A527 (HiFi4 Audio DSP & XuanTie RISC-V)

set -e

echo "=== PHASE 1: Isolating and Testing Hardware Message Box ==="
# 1. Load the standalone testing driver module
if ! lsmod | grep -q "mailbox_test"; then
    modprobe mailbox-test 2>/dev/null || true
fi

MBOX_DIR=""
if [ -d "/sys/kernel/debug/mailbox-test" ]; then
    MBOX_DIR="/sys/kernel/debug/mailbox-test"
elif [ -d "/sys/kernel/debug/mailbox/mbox-test" ]; then
    MBOX_DIR="/sys/kernel/debug/mailbox/mbox-test"
elif [ -d "/sys/kernel/debug/mailbox_test" ]; then
    MBOX_DIR="/sys/kernel/debug/mailbox_test"
fi

if [ -n "$MBOX_DIR" ]; then
    echo "[OK] Mailbox test framework loaded successfully at $MBOX_DIR."
    
    # Fire validation ping down TX FIFO
    if [ -f "$MBOX_DIR/channel_tx" ]; then
        echo -n "PING" > "$MBOX_DIR/channel_tx"
        echo "[OK] Pushed 'PING' to $MBOX_DIR/channel_tx"
        if [ -f "$MBOX_DIR/channel_rx" ]; then
            echo "Hardware RX Buffer: $(cat "$MBOX_DIR/channel_rx" 2>/dev/null || true)"
        fi
    elif [ -f "$MBOX_DIR/message" ]; then
        echo -n "PING" > "$MBOX_DIR/message"
        echo "[OK] Pushed 'PING' to $MBOX_DIR/message"
        echo "Hardware RX Buffer: $(cat "$MBOX_DIR/message" 2>/dev/null || true)"
    fi
else
    echo "[WARN] Mailbox test debugfs node not yet created. Check Device Tree overlay status."
fi

echo -e "\n=== PHASE 2: Initializing Remoteproc Core Manager ==="
FIRMWARE_FILE="${1:-testBasic.elf}"

# Check for firmware file in /lib/firmware
if [ ! -f "/lib/firmware/$FIRMWARE_FILE" ] && [ -f "$FIRMWARE_FILE" ]; then
    cp "$FIRMWARE_FILE" "/lib/firmware/$FIRMWARE_FILE"
fi

RPROC_SYS="/sys/class/remoteproc/remoteproc0"
if [ -d "$RPROC_SYS" ]; then
    echo "[OK] RemoteProc device node found at $RPROC_SYS"
    
    # 1. Bind target firmware
    echo -n "$FIRMWARE_FILE" > "$RPROC_SYS/firmware"
    echo "[OK] Bound firmware: $FIRMWARE_FILE"
    
    # 2. Stop if already running
    STATE=$(cat "$RPROC_SYS/state")
    if [ "$STATE" = "running" ]; then
        echo "Stopping active core..."
        echo stop > "$RPROC_SYS/state"
        sleep 1
    fi
    
    # 3. Boot coprocessor core
    echo "Booting coprocessor core..."
    echo start > "$RPROC_SYS/state"
    sleep 1
    
    NEW_STATE=$(cat "$RPROC_SYS/state")
    echo "Current Core State: $NEW_STATE"
    
    # 4. Check Trace0 Buffer
    TRACE_FILE="/sys/kernel/debug/remoteproc/remoteproc0/trace0"
    if [ -f "$TRACE_FILE" ]; then
        echo -e "\n=== Remoteproc Trace0 Output ==="
        cat "$TRACE_FILE"
    fi
    
    echo -e "\n=== Kernel Ring Buffer (dmesg) ==="
    dmesg | tail -n 15
else
    echo "[ERROR] RemoteProc node $RPROC_SYS not found."
fi
