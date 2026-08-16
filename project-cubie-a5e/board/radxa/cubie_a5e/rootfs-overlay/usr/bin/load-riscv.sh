#!/bin/sh
#
# XuanTie E907 RISC-V Co-Processor Loader for Allwinner A527/T527/A733
#

FIRMWARE_BIN="${2:-/lib/firmware/riscv-firmware.bin}"

# Physical Register Addresses & SRAM Base (Host ARM physical memory view)
CCU_MCU_CLK_REG="0x07010020"    # MCU and DSP Subsystem Bus Clocks
MCU_RST_REG="0x07010100"        # MCU Core Reset Controller
# Host ITCM window: 0x07110000 -> (0x07110000 / 4096) = 28944

case "$1" in
    start)
        if [ ! -f "$FIRMWARE_BIN" ]; then
            echo "Notice: RISC-V firmware binary '$FIRMWARE_BIN' not found. Skipping."
            exit 0
        fi

        echo "=== Loading XuanTie E907 RISC-V Firmware ==="

        # 1. Enable MCU / DSP Bus Clocks
        echo "Enabling MCU subsystem clocks (CCU 0x07010020 -> 0x03)..."
        devmem "$CCU_MCU_CLK_REG" 32 0x00000003 2>/dev/null || true

        # 2. Assert Core Reset (Core holds in reset while copying memory)
        echo "Asserting RISC-V core reset..."
        devmem "$MCU_RST_REG" 32 0x00000000 2>/dev/null || true

        # 3. Load Binary directly into ITCM/SRAM via /dev/mem
        echo "Copying $FIRMWARE_BIN to ITCM (0x07110000)..."
        dd if="$FIRMWARE_BIN" of=/dev/mem bs=4096 seek=28944 conv=notrunc status=none

        # 4. Release Core Reset (Boot!)
        echo "Releasing reset (Booting XuanTie E907 at 0x00000000)..."
        devmem "$MCU_RST_REG" 32 0x00020000 2>/dev/null || true

        echo "XuanTie E907 RISC-V co-processor is running."
        ;;

    stop)
        echo "Halting XuanTie E907 RISC-V co-processor..."
        devmem "$MCU_RST_REG" 32 0x00000000 2>/dev/null || true
        echo "RISC-V core is held in reset."
        ;;

    status)
        clk_val=$(devmem "$CCU_MCU_CLK_REG" 32 2>/dev/null || echo "N/A")
        rst_val=$(devmem "$MCU_RST_REG" 32 2>/dev/null || echo "N/A")
        echo "MCU Bus Clock Reg (0x07010020): $clk_val"
        echo "MCU Reset Reg     (0x07010100): $rst_val"
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
