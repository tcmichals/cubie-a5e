#!/usr/bin/env python3
"""
XuanTie E907 RISC-V All-in-One Automated Hardware Bring-Up & Telemetry Tool
Target SoC: Allwinner A523/A527 (Radxa Cubie A5E) & A733 (Radxa Cubie A7A)

Loads firmware into ITCM (0x07110000) & SRAM C (0x00020000), enables Main CCU
CLK_DSP (0x02001c70) and MCU CCU bridges (0x07102108..0x07102128), cycles core
reset, and continuously samples live telemetry at 0x00028000.
"""

import sys
import os
import mmap
import struct
import time
import argparse

PAGE_SIZE = 4096

def map_phys(fd, addr, size):
    base = addr & ~(PAGE_SIZE - 1)
    offset = addr - base
    mm = mmap.mmap(fd, size + offset, flags=mmap.MAP_SHARED,
                   prot=mmap.PROT_READ | mmap.PROT_WRITE, offset=base)
    return mm, offset

def main():
    parser = argparse.ArgumentParser(description="XuanTie E907 Hardware Bring-Up & Telemetry Tool")
    parser.add_argument("--fw", default="/tmp/riscv-firmware.bin", help="Path to firmware binary (default: /tmp/riscv-firmware.bin)")
    parser.add_argument("--time", type=int, default=10, help="Monitoring duration in seconds (default: 10)")
    args = parser.parse_args()

    print("=" * 80)
    print("XUANTIE E907 RISC-V HARDWARE BRING-UP & TELEMETRY SUITE")
    print("=" * 80)

    try:
        dev_mem = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
    except PermissionError:
        print("[!] ERROR: Must run as root (sudo).")
        sys.exit(1)

    # 1. Map Main CCU (0x02001000) & MCU CCU (0x07102000)
    main_ccu_mm, main_ccu_off = map_phys(dev_mem, 0x02001000, 0x1000)
    mcu_ccu_mm, mcu_ccu_off = map_phys(dev_mem, 0x07102000, 0x1000)

    print("\n[Step 1] Enabling Main SoC CCU Root DSP Parent Clock (0x02001c70)...")
    main_ccu_mm.seek(main_ccu_off + 0x0c70)
    main_ccu_mm.write(struct.pack("<I", 0x80000000))
    print("  -> Main CCU CLK_DSP enabled: 0x80000000")

    print("\n[Step 2] Holding XuanTie E907 in Reset & Initializing MCU CCU Bus Bridges...")
    # Hold reset
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    mcu_ccu_mm.write(struct.pack("<I", 0x00030001))

    # Enable TZMA, PubSRAM, MBUS, RISCV_CLK, MSGBOX
    regs = [
        (0x108, 0x00010001, "TZMA0 (SRAM Adapter)"),
        (0x10c, 0x00010001, "TZMA1 (Periph Adapter)"),
        (0x114, 0x00010001, "PubSRAM Clock Gate"),
        (0x11c, 0x00000003, "MBUS Subsystem Clock"),
        (0x120, 0x80000000, "RISCV_CLK (24MHz OSC)"),
        (0x128, 0x00010001, "MSGBOX Hardware Mailbox")
    ]
    for reg, val, desc in regs:
        mcu_ccu_mm.seek(mcu_ccu_off + reg)
        mcu_ccu_mm.write(struct.pack("<I", val))
        print(f"  -> {desc} (0x{0x07102000+reg:08X}) = 0x{val:08X}")

    # 2. Map ITCM (0x07110000) & SRAM C (0x00020000)
    itcm_mm, itcm_off = map_phys(dev_mem, 0x07110000, 0x10000)
    sram_mm, sram_off = map_phys(dev_mem, 0x00020000, 0x20000)

    # 3. Load Firmware
    print(f"\n[Step 3] Loading Firmware from {args.fw}...")
    if not os.path.exists(args.fw):
        print(f"[!] Warning: {args.fw} not found. Searching in common paths...")
        for p in ["/lib/firmware/riscv-firmware.bin", "/tmp/firmware.bin", "firmware.bin"]:
            if os.path.exists(p):
                args.fw = p
                print(f"[+] Found firmware at: {args.fw}")
                break

    if os.path.exists(args.fw):
        with open(args.fw, "rb") as f:
            fw = f.read()
        print(f"  -> Read {len(fw)} bytes.")
        
        # Load into ITCM (Reset Vector 0x00000000)
        itcm_mm.seek(itcm_off)
        itcm_mm.write(fw)
        print("  -> Loaded into ITCM (0x07110000 / Core 0x00000000)")

        # Load into SRAM C (Shared Memory 0x00020000)
        sram_mm.seek(sram_off)
        sram_mm.write(fw)
        print("  -> Loaded into SRAM C (0x00020000 / Core 0x00020000)")
    else:
        print("[!] ERROR: Firmware binary not found! Please build or copy firmware.bin")
        sys.exit(1)

    # Clear Telemetry Block in SRAM C (0x00028000)
    sram_mm.seek(sram_off + 0x8000)
    sram_mm.write(b"\x00" * 64)

    # 4. Release Reset
    print("\n[Step 4] Releasing XuanTie E907 Core Reset (0x00070001)...")
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    mcu_ccu_mm.write(struct.pack("<I", 0x00070001))
    time.sleep(0.01)
    
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    rst_val = struct.unpack("<I", mcu_ccu_mm.read(4))[0]
    print(f"  -> Reset register status: 0x{rst_val:08X}")

    # 5. Monitor Telemetry
    print(f"\n[Step 5] Monitoring Telemetry (0x00028000) for {args.time} seconds...")
    print("-" * 80)
    print(f"{'Time':^8} | {'Magic':^10} | {'Boot':^6} | {'Heartbeat':^12} | {'Loops':^12} | {'State':^16}")
    print("-" * 80)

    start_time = time.time()
    last_hb = 0
    ticking = False

    while time.time() - start_time < args.time:
        elapsed = time.time() - start_time
        sram_mm.seek(sram_off + 0x8000)
        data = sram_mm.read(32)
        magic, boot, hb, loops, alive = struct.unpack("<IIIII", data[:20])

        if alive == 0x414C4956: # "ALIV"
            state = "RUNNING (ALIV)"
        elif boot == 1:
            state = "BOOTED"
        elif magic == 0x52495343: # "RISC"
            state = "STARTUP"
        else:
            state = "WAITING"

        if hb > last_hb and last_hb > 0:
            ticking = True

        print(f"{elapsed:6.1f}s  | 0x{magic:08X} | {boot:^6} | {hb:^12} | {loops:^12} | {state:^16}")
        last_hb = hb
        time.sleep(0.5)

    print("=" * 80)
    if ticking:
        print("[🎉 SUCCESS] XuanTie E907 IS RUNNING AND ACTIVELY REPORTING TELEMETRY!")
    else:
        print(f"[i] Completed. Final Heartbeat: {last_hb}")
    print("=" * 80)

if __name__ == "__main__":
    main()
