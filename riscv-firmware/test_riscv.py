#!/usr/bin/env python3
"""
Comprehensive XuanTie RISC-V Co-Processor Automated Test & Bring-Up Tool
For Radxa Cubie A5E (Allwinner A523/A527 / XuanTie E907)
"""

import sys
import os
import mmap
import struct
import time
import argparse

# Physical Address Bases
SRAM_A1_BASE  = 0x00000000  # 32 KB (0x00000000..0x00007FFF)
SRAM_C_BASE   = 0x00020000  # 128 KB (0x00020000..0x0003FFFF)
MAIN_CCU_BASE = 0x02001000
SYS_CFG_BASE  = 0x03000000
MCU_CFG_BASE  = 0x07100000
MCU_CCU_BASE  = 0x07102000
MCU_MSGBOX    = 0x07103000
MASK_ROM_BASE = 0x07110000
PAGE_SIZE     = 4096

class MMIO:
    def __init__(self, base_addr, size=PAGE_SIZE):
        self.base_addr = base_addr
        self.size = size
        self.fd = os.open("/dev/mem", os.O_RDWR | os.O_SYNC)
        self.mem = mmap.mmap(self.fd, self.size, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE, offset=self.base_addr)

    def read32(self, offset):
        self.mem.seek(offset)
        return struct.unpack("<I", self.mem.read(4))[0]

    def write32(self, offset, val):
        self.mem.seek(offset)
        self.mem.write(struct.pack("<I", val))

    def close(self):
        self.mem.close()
        os.close(self.fd)

def enable_all_clocks():
    main_ccu = MMIO(MAIN_CCU_BASE, 0x1000)
    main_ccu.write32(0xc70, 0x80000000) # Enable CLK_DSP
    
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x108, 0x00010001) # TZMA0
    mcu_ccu.write32(0x10c, 0x00010001) # TZMA1
    mcu_ccu.write32(0x114, 0x00010001) # PubSRAM
    mcu_ccu.write32(0x11c, 0x00000003) # MBUS
    mcu_ccu.write32(0x120, 0x80000000) # RISCV_CLK 24MHz
    mcu_ccu.write32(0x128, 0x00010001) # Mailbox

def reset_cycle_core():
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x124, 0x00030001) # Hold in reset
    time.sleep(0.01)
    mcu_ccu.write32(0x124, 0x00070001) # Release reset

def cmd_auto_sram_test(fw_path="/tmp/riscv-firmware.bin"):
    print("=" * 80)
    print("AUTOMATED SRAM MEMORY & RESET VECTOR TEST SUITE")
    print("=" * 80)
    
    enable_all_clocks()
    
    # 0. Test Unlocking SRAM Remap via SYS_CFG (0x03000000)
    print("\n[Step 0] Probing SYS_CFG (0x03000000) SRAM Remap Modes...")
    sys_cfg = MMIO(SYS_CFG_BASE, 0x1000)
    ctrl0 = sys_cfg.read32(0x0000)
    ctrl1 = sys_cfg.read32(0x0004)
    print(f"  -> Initial SYS_CFG: CTRL0(0x00)=0x{ctrl0:08X}, CTRL1(0x04)=0x{ctrl1:08X}")
    
    # Try different remap configurations to map SRAM A1 to 0x00000000
    remap_modes = [0x00000001, 0x00000002, 0x00000003, 0x00000007, 0x00000010, 0x00000100]
    sram_a1_ok = False
    
    for mode in remap_modes:
        sys_cfg.write32(0x0000, mode)
        try:
            sram_a1 = MMIO(SRAM_A1_BASE, 0x1000)
            sram_a1.write32(0x0, 0x55AAAA55)
            if sram_a1.read32(0x0) == 0x55AAAA55:
                sram_a1.write32(0x0, 0x12345678)
                if sram_a1.read32(0x0) == 0x12345678:
                    print(f"  -> SUCCESS! SYS_CFG CTRL0 = 0x{mode:08X} unlocked writable SRAM A1 at 0x00000000!")
                    sram_a1_ok = True
                    break
        except Exception:
            pass

    # 1. Test SRAM A1 (0x00000000)
    print("\n[Step 1] SRAM A1 Status:")
    if sram_a1_ok:
        print("  -> SRAM A1 (0x00000000): READ/WRITE VERIFIED OK!")
    else:
        print("  -> SRAM A1 (0x00000000): Mapped to BROM (Read-Only).")

    # 2. Test SRAM C (0x00020000)
    print("\n[Step 2] Testing SRAM C (0x00020000..0x00040000)...")
    sram_c_ok = False
    try:
        sram_c = MMIO(SRAM_C_BASE, 0x20000)
        orig_c = sram_c.read32(0x0)
        sram_c.write32(0x0, 0xA55AA55A)
        wb1 = sram_c.read32(0x0)
        sram_c.write32(0x0, orig_c)
        if wb1 == 0xA55AA55A:
            print("  -> SRAM C (0x00020000): READ/WRITE VERIFIED OK!")
            sram_c_ok = True
        else:
            print(f"  -> SRAM C (0x00020000): FAILED (wb1=0x{wb1:08X})")
    except Exception as e:
        print(f"  -> SRAM C Access Error: {e}")

    # 3. Read firmware binary
    if not os.path.exists(fw_path):
        if os.path.exists("/lib/firmware/riscv-firmware.bin"):
            fw_path = "/lib/firmware/riscv-firmware.bin"
        else:
            print(f"[-] ERROR: Binary not found: {fw_path}")
            return

    with open(fw_path, "rb") as f:
        fw_data = bytearray(f.read())
    if len(fw_data) % 4 != 0:
        fw_data += b'\x00' * (4 - (len(fw_data) % 4))

    print(f"\n[Step 3] Loading Firmware ({len(fw_data)} bytes from {fw_path})...")

    # Load into SRAM C (0x00020000)
    if sram_c_ok:
        sram_c = MMIO(SRAM_C_BASE, 0x20000)
        for i in range(0, len(fw_data), 4):
            val = int.from_bytes(fw_data[i:i+4], 'little')
            sram_c.write32(i, val)
        # Clear telemetry block
        for i in range(0x8000, 0x8040, 4):
            sram_c.write32(i, 0)
        print("  -> Loaded into SRAM C (0x00020000) and initialized telemetry block (0x00028000).")

    # If SRAM A1 is writable, inject a reset trampoline at 0x00000000 that jumps to 0x00020000
    if sram_a1_ok:
        sram_a1 = MMIO(SRAM_A1_BASE, 0x4000)
        # RISC-V: 'lui x1, 0x20' -> 'jalr x0, 0(x1)' (jumps to 0x00020000)
        # Machine code: lui x1, 0x20 -> 0x000200B7, jalr x0, 0(x1) -> 0x00008067
        sram_a1.write32(0x00, 0x000200B7) # lui ra, 0x20
        sram_a1.write32(0x04, 0x00008067) # jalr zero, 0(ra) -> jumps straight to 0x00020000!
        print("  -> Injected Hardware Reset Trampoline at 0x00000000 (lui ra, 0x20; jalr zero, ra)")

    # 4. Cycle Core Reset
    print("\n[Step 4] Cycling XuanTie E907 Core Reset...")
    reset_cycle_core()
    print("  -> Core reset released (MCU RST REG: 0x00070001)")

    # 5. Live Telemetry Sampling
    print("\n[Step 5] Sampling Telemetry (0x00028000) for 5 seconds...")
    sram_c = MMIO(SRAM_C_BASE, 0x20000)
    active = False
    for t in range(10):
        magic = sram_c.read32(0x8000)
        boot  = sram_c.read32(0x8004)
        hb    = sram_c.read32(0x8008)
        loops = sram_c.read32(0x800C)
        stat  = sram_c.read32(0x8010)
        if magic == 0x52495343 or hb > 0 or loops > 0:
            active = True
            print(f"  [{t*0.5:4.1f}s] \033[92mSUCCESS: Magic=0x{magic:08X} | Boot={boot} | Heartbeat={hb} | Loops={loops} | Stat=0x{stat:08X}\033[0m")
        else:
            print(f"  [{t*0.5:4.1f}s] Magic=0x{magic:08X} | Boot={boot} | Heartbeat={hb} | Loops={loops}")
        time.sleep(0.5)

    print("\n" + "=" * 80)
    if active:
        print("\033[92mRESULT: XUANTIE RISC-V CORE IS OFFICIALLY RUNNING FROM SRAM!\033[0m")
    else:
        print("\033[93mRESULT: Core is still held in internal ROM wait state.\033[0m")
    print("=" * 80)

def cmd_scan():
    print("=" * 80)
    print("NON-ZERO HARDWARE REGISTER SCAN ACROSS ALL MCU REGIONS")
    print("=" * 80)
    main_ccu = MMIO(MAIN_CCU_BASE, 0x1000)
    sys_cfg  = MMIO(SYS_CFG_BASE,  0x1000)
    mcu_cfg  = MMIO(MCU_CFG_BASE,  0x1000)
    mcu_ccu  = MMIO(MCU_CCU_BASE,  0x1000)
    mcu_msg  = MMIO(MCU_MSGBOX,    0x1000)
    mask_rom = MMIO(MASK_ROM_BASE, 0x1000)
    sram_c   = MMIO(SRAM_C_BASE,   0x20000)

    def scan_region(name, mmio, size, start_off=0):
        print(f"\n--- Region: {name} (Physical 0x{mmio.base_addr + start_off:08X}, Size {size} bytes) ---")
        count = 0
        for off in range(start_off, start_off + size, 4):
            val = mmio.read32(off)
            if val != 0:
                print(f"  [0x{mmio.base_addr + off:08X}] (offset +0x{off:04X}): 0x{val:08X}")
                count += 1
        if count == 0:
            print("  (All registers read 0x00000000)")

    scan_region("Main CCU", main_ccu, 0x1000)
    scan_region("System Config", sys_cfg, 0x100)
    scan_region("MCU Config (0x07100000)", mcu_cfg, 0x100)
    scan_region("MCU CCU (0x07102000)", mcu_ccu, 0x200)
    scan_region("MCU Mailbox (0x07103000)", mcu_msg, 0x100)
    scan_region("Mask ROM (0x07110000)", mask_rom, 0x100)
    scan_region("SRAM C Base (0x00020000)", sram_c, 0x80)
    scan_region("SRAM C Telemetry (0x00028000)", sram_c, 0x40, start_off=0x8000)
    print("=" * 80)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Allwinner XuanTie RISC-V Automated Test Tool")
    parser.add_argument("action", choices=["test", "scan"], default="test", nargs="?")
    parser.add_argument("--fw", default="/tmp/riscv-firmware.bin", help="Path to firmware binary")
    args = parser.parse_args()

    if args.action == "test":
        cmd_auto_sram_test(args.fw)
    elif args.action == "scan":
        cmd_scan()
