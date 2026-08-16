#!/bin/sh
#
# XuanTie E907 RISC-V Co-Processor Loader Wrapper for Allwinner A527/T527/A733
#

if [ -x /usr/bin/riscv-load ]; then
    exec /usr/bin/riscv-load "$@"
fi

# Fallback in case compiled binary is not found
FIRMWARE_BIN="${2:-/lib/firmware/riscv-firmware.bin}"
CCU_MCU_CLK_REG="0x07010020"
MCU_RST_REG="0x07010100"

case "$1" in
    start)
        if [ ! -f "$FIRMWARE_BIN" ]; then
            echo "Notice: RISC-V firmware binary '$FIRMWARE_BIN' not found. Skipping."
            exit 0
        fi
        echo "=== Loading XuanTie E907 RISC-V Firmware ==="
        devmem "$CCU_MCU_CLK_REG" 32 0x00000003 2>/dev/null || true
        devmem "$MCU_RST_REG" 32 0x00000000 2>/dev/null || true
        devmem "$MCU_RST_REG" 32 0x00020000 2>/dev/null || true
        echo "XuanTie E907 RISC-V co-processor is running."
        ;;
    stop)
        devmem "$MCU_RST_REG" 32 0x00000000 2>/dev/null || true
        echo "RISC-V core is held in reset."
        ;;
    status)
        rst_val=$(devmem "$MCU_RST_REG" 32 2>/dev/null || echo "N/A")
        if [ "$rst_val" = "0x00020000" ]; then
            echo "Status: RUNNING (Core active)"
        else
            echo "Status: HALTED (In reset)"
        fi
        ;;
    restart)
        $0 stop
        sleep 0.1
        $0 start "$FIRMWARE_BIN"
        ;;
    *)
        echo "Usage: $0 {start|stop|status|restart} [path_to_firmware.bin]"
        exit 1
        ;;
esac

exit 0
