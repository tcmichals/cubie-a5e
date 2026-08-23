#!/usr/bin/env python3
"""
XuanTie E907 RISC-V Hardware Bring-Up, Loader & Live Telemetry Tool (Option A)
Target SoC: Allwinner A523/A527 (Radxa Cubie A5E) & A733 (Radxa Cubie A7A)

Direct execution from 256 KB Dedicated RISC-V Local SRAM (0x07280000)
with System Shared SRAM C (0x00020000) for telemetry and host communication.
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
    parser = argparse.ArgumentParser(description="XuanTie E907 Hardware Bring-Up & Telemetry Tool (Option A)")
    parser.add_argument("--fw", default="/tmp/riscv-firmware.bin", help="Path to firmware binary (default: /tmp/riscv-firmware.bin)")
    parser.add_argument("--time", type=int, default=10, help="Monitoring duration in seconds (default: 10)")
    args = parser.parse_args()

    print("=" * 80)
    print("XUANTIE E907 RISC-V HARDWARE BRING-UP & TELEMETRY SUITE (OPTION A)")
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
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    mcu_ccu_mm.write(struct.pack("<I", 0x00030001))

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

    # 2. Map Dedicated RISC-V SRAM (0x07280000, 256 KB) & Shared SRAM C (0x00020000, 128 KB)
    r_sram_mm, r_sram_off = map_phys(dev_mem, 0x07280000, 0x40000)
    sram_c_mm, sram_c_off = map_phys(dev_mem, 0x00020000, 0x20000)

    # 3. Load Firmware into Dedicated RISC-V SRAM (0x07280000)
    print(f"\n[Step 3] Loading Firmware into Dedicated RISC-V SRAM (0x07280000)...")
    if not os.path.exists(args.fw):
        for p in ["/lib/firmware/riscv-firmware.bin", "/tmp/firmware.bin", "firmware.bin"]:
            if os.path.exists(p):
                args.fw = p
                break

    if os.path.exists(args.fw):
        with open(args.fw, "rb") as f:
            fw = f.read()
        print(f"  -> Read {len(fw)} bytes from {args.fw}.")
        r_sram_mm.seek(r_sram_off)
        r_sram_mm.write(fw)
        print("  -> Written to 0x07280000 (Core 0x00000000 Entry Point).")
    else:
        print("[!] ERROR: Firmware binary not found! Please build or copy firmware.bin")
        sys.exit(1)

    # Clear Shared Telemetry Block in SRAM C (0x00028000)
    sram_c_mm.seek(sram_c_off + 0x8000)
    sram_c_mm.write(b"\x00" * 64)

    # 4. Release Reset
    print("\n[Step 4] Releasing XuanTie E907 Core Reset (0x00070001)...")
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    mcu_ccu_mm.write(struct.pack("<I", 0x00070001))
    time.sleep(0.01)
    
    mcu_ccu_mm.seek(mcu_ccu_off + 0x124)
    rst_val = struct.unpack("<I", mcu_ccu_mm.read(4))[0]
    print(f"  -> Reset register status: 0x{rst_val:08X} (Core Running)")

    # 5. Monitor Live Telemetry
    print(f"\n[Step 5] Monitoring Live Telemetry at Shared SRAM C (0x00028000) for {args.time}s...")
    print("-" * 80)
    print(f"{'Time':^8} | {'Magic':^10} | {'Boot':^6} | {'Heartbeat':^12} | {'Loops':^12} | {'State':^16}")
    print("-" * 80)

    start_time = time.time()
    last_hb = 0
    ticking = False

    while time.time() - start_time < args.time:
        elapsed = time.time() - start_time
        sram_c_mm.seek(sram_c_off + 0x8000)
        data = sram_c_mm.read(32)
        magic, boot, hb, loops, alive = struct.unpack("<IIIII", data[:20])

        if alive == 0x414C4956:
            state = "RUNNING (ALIV)"
        elif boot == 1 or magic == 0x52495343:
            state = "ACTIVE (RISC)"
        else:
            state = "WAITING"

        if hb > last_hb and last_hb > 0:
            ticking = True

        print(f"{elapsed:6.1f}s  | 0x{magic:08X} | {boot:^6} | {hb:^12} | {loops:^12} | {state:^16}")
        last_hb = hb
        time.sleep(0.5)

    print("=" * 80)
    if ticking:
        print(f"[🎉 SUCCESS] XuanTie E907 IS RUNNING DIRECTLY FROM 0x07280000! Final Heartbeat: {last_hb}")
    else:
        print(f"[i] Completed. Final Heartbeat: {last_hb}")
    print("=" * 80)

if __name__ == "__main__":
    main()
