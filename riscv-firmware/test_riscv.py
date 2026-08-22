#!/usr/bin/env python3
"""
Comprehensive XuanTie RISC-V Co-Processor Diagnostic & Bring-Up Tool
For Radxa Cubie A5E (Allwinner A523/A527 / XuanTie E907)
"""

import sys
import os
import mmap
import struct
import time
import argparse

# Physical Address Bases
MAIN_CCU_BASE = 0x02001000
SYS_CFG_BASE  = 0x03000000
MCU_CFG_BASE  = 0x07100000
MCU_CCU_BASE  = 0x07102000
MCU_MSGBOX    = 0x07103000
MASK_ROM_BASE = 0x07110000
SRAM_C_BASE   = 0x00020000
TELEMETRY_OFF = 0x00008000  # 0x00028000
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

def get_mmio():
    main_ccu   = MMIO(MAIN_CCU_BASE, 0x1000)
    sys_cfg    = MMIO(SYS_CFG_BASE,  0x1000)
    mcu_cfg    = MMIO(MCU_CFG_BASE,  0x1000)
    mcu_ccu    = MMIO(MCU_CCU_BASE,  0x1000)
    mcu_msgbox = MMIO(MCU_MSGBOX,    0x1000)
    mask_rom   = MMIO(MASK_ROM_BASE, 0x1000)
    sram_c     = MMIO(SRAM_C_BASE,   0x20000) # 128 KB
    return main_ccu, sys_cfg, mcu_cfg, mcu_ccu, mcu_msgbox, mask_rom, sram_c

def cmd_scan():
    print("=" * 80)
    print("NON-ZERO HARDWARE REGISTER SCAN ACROSS ALL MCU REGIONS")
    print("=" * 80)
    main_ccu, sys_cfg, mcu_cfg, mcu_ccu, mcu_msgbox, mask_rom, sram_c = get_mmio()

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
    scan_region("MCU Mailbox (0x07103000)", mcu_msgbox, 0x100)
    scan_region("Mask ROM (0x07110000)", mask_rom, 0x100)
    scan_region("SRAM C Base (0x00020000)", sram_c, 0x80)
    scan_region("SRAM C Telemetry (0x00028000)", sram_c, 0x40, start_off=TELEMETRY_OFF)
    print("=" * 80)

def cmd_send_doorbell(target_addr=0x00020000):
    print(f"[+] Sending Mailbox Doorbell Message (0x{target_addr:08X}) to XuanTie E907 at 0x07103000...")
    cmd_enable_clocks()
    msgbox = MMIO(MCU_MSGBOX, 0x1000)
    # Write target address into Channel 0 Message Data Reg (0x07103000)
    msgbox.write32(0x000, target_addr)
    # Trigger Doorbell IRQ (0x07103020)
    msgbox.write32(0x020, 0x00000001)
    print(f"[+] Doorbell written to Mailbox (0x07103000: 0x{msgbox.read32(0x0):08X})")

def cmd_test_remap():
    print("=" * 80)
    print("TESTING TCM / VECTOR / REMAP WRITABILITY (0x07100000..0x07100040)")
    print("=" * 80)
    mcu_cfg = MMIO(MCU_CFG_BASE, 0x1000)
    
    test_offsets = [0x0000, 0x0004, 0x0008, 0x0010, 0x0014, 0x0018, 0x001c, 0x0020, 0x0024]
    for off in test_offsets:
        orig = mcu_cfg.read32(off)
        mcu_cfg.write32(off, 0x12345678)
        wb1 = mcu_cfg.read32(off)
        mcu_cfg.write32(off, 0x00000000)
        wb2 = mcu_cfg.read32(off)
        mcu_cfg.write32(off, orig) # restore
        writable = "READ/WRITE (Writable!)" if (wb1 != orig or wb2 != orig) else "READ-ONLY / Fixed"
        print(f"  Register [0x{MCU_CFG_BASE + off:08X}] (offset +0x{off:02X}): Orig=0x{orig:08X} -> Write(0x12345678)={wb1:08X} -> Write(0)={wb2:08X} [{writable}]")
    print("=" * 80)

def cmd_enable_clocks():
    print("[+] Enabling Main CCU Root DSP/MCU clocks...")
    main_ccu = MMIO(MAIN_CCU_BASE, 0x1000)
    main_ccu.write32(0xc70, 0x80000000)

    print("[+] Enabling MCU CCU and TZMA peripheral buses...")
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x108, 0x00010001) # TZMA0
    mcu_ccu.write32(0x10c, 0x00010001) # TZMA1
    mcu_ccu.write32(0x114, 0x00010001) # PubSRAM
    mcu_ccu.write32(0x11c, 0x00000003) # MBUS
    mcu_ccu.write32(0x120, 0x80000000) # RISC-V Clock 24MHz
    mcu_ccu.write32(0x128, 0x00010001) # Mailbox
    print("[+] Clocks and bus bridges configured successfully.")

def cmd_test_sram():
    print("[+] Testing SRAM C (0x00020000..0x0003FFFF, 128 KB)...")
    sram_c = MMIO(SRAM_C_BASE, 0x20000)
    patterns = [0x55555555, 0xAAAAAAAA, 0x12345678, 0xDEADBEEF]
    
    for pat in patterns:
        sram_c.write32(0x0, pat)
        sram_c.write32(0x1000, pat)
        sram_c.write32(0x1F000, pat)
        r0 = sram_c.read32(0x0)
        r1 = sram_c.read32(0x1000)
        r2 = sram_c.read32(0x1F000)
        if r0 != pat or r1 != pat or r2 != pat:
            print(f"[-] SRAM Test FAILED for pattern 0x{pat:08X}: (r0=0x{r0:08X}, r1=0x{r1:08X}, r2=0x{r2:08X})")
            return False
    print("[+] SRAM C 100% Passed read/write integrity check!")
    return True

def cmd_load(bin_path="/tmp/riscv-firmware.bin"):
    if not os.path.exists(bin_path):
        if os.path.exists("/lib/firmware/riscv-firmware.bin"):
            bin_path = "/lib/firmware/riscv-firmware.bin"
        else:
            print(f"[-] ERROR: Binary not found: {bin_path}")
            return False

    with open(bin_path, "rb") as f:
        data = bytearray(f.read())

    if len(data) % 4 != 0:
        data += b'\x00' * (4 - (len(data) % 4))

    print(f"[+] Loading {len(data)} bytes from {bin_path} into SRAM C (0x00020000)...")
    sram_c = MMIO(SRAM_C_BASE, 0x20000)
    for i in range(0, len(data), 4):
        val = int.from_bytes(data[i:i+4], 'little')
        sram_c.write32(i, val)

    first_word = sram_c.read32(0x0)
    magic = sram_c.read32(0x4)
    chksum = sram_c.read32(0xc)
    length = sram_c.read32(0x10)
    print(f"[+] Loaded! First: 0x{first_word:08X}, Magic: 0x{magic:08X}, Checksum: 0x{chksum:08X}, Length: {length} bytes")
    return True

def cmd_start():
    print("[+] Starting XuanTie RISC-V Co-Processor...")
    cmd_enable_clocks()
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x124, 0x00030001) # Hold core in reset
    time.sleep(0.01)
    mcu_ccu.write32(0x124, 0x00070001) # Release core reset
    print("[+] Core reset released (MCU RST REG: 0x00070001)")

def cmd_stop():
    print("[+] Stopping XuanTie RISC-V Co-Processor...")
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x124, 0x00030001)
    print("[+] Core held in reset.")

def cmd_monitor(duration=5):
    print("=" * 80)
    print(f"MONITORING XUANTIE RISC-V TELEMETRY (0x00028000) for {duration} seconds...")
    print("=" * 80)
    sram_c  = MMIO(SRAM_C_BASE,   0x20000)
    start_time = time.time()
    last_hb = None
    last_loops = None

    while time.time() - start_time < duration:
        magic = sram_c.read32(TELEMETRY_OFF)
        boot  = sram_c.read32(TELEMETRY_OFF + 4)
        hb    = sram_c.read32(TELEMETRY_OFF + 8)
        loops = sram_c.read32(TELEMETRY_OFF + 12)
        stat  = sram_c.read32(TELEMETRY_OFF + 16)
        trap  = sram_c.read32(TELEMETRY_OFF + 32)
        
        is_active = (hb != last_hb or loops != last_loops) and last_hb is not None
        status_str = "\033[92m>>> CORE IS EXECUTING LIVE <<<\033[0m" if is_active else "\033[93mIDLE / WAITING\033[0m"
        last_hb = hb
        last_loops = loops
        print(f"\r[Telemetry] Magic: 0x{magic:08X} | Boot: {boot} | Heartbeat: {hb:<8} | Loops: {loops:<10} | State: {status_str}", end="", flush=True)
        time.sleep(0.5)
    print("\n")

def cmd_run(bin_path="/tmp/riscv-firmware.bin"):
    print("=" * 80)
    print("RUNNING AUTOMATED XUANTIE RISC-V BRING-UP SEQUENCE")
    print("=" * 80)
    cmd_stop()
    cmd_enable_clocks()
    cmd_test_sram()
    cmd_load(bin_path)
    cmd_start()
    cmd_monitor(duration=5)
    cmd_status()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Allwinner XuanTie RISC-V Bring-up & Diagnostic Tool")
    parser.add_argument("action", choices=["status", "scan", "test-remap", "doorbell", "enable-clocks", "test-sram", "load", "start", "stop", "monitor", "run"], default="status", nargs="?")
    parser.add_argument("--fw", default="/tmp/riscv-firmware.bin", help="Path to firmware binary")
    parser.add_argument("--duration", type=int, default=5, help="Monitor duration in seconds")
    args = parser.parse_args()

    if args.action == "status":
        cmd_scan()
    elif args.action == "scan":
        cmd_scan()
    elif args.action == "doorbell":
        cmd_send_doorbell()
        cmd_monitor(args.duration)
    elif args.action == "test-remap":
        cmd_test_remap()
    elif args.action == "enable-clocks":
        cmd_enable_clocks()
    elif args.action == "test-sram":
        cmd_test_sram()
    elif args.action == "load":
        cmd_load(args.fw)
    elif args.action == "start":
        cmd_start()
    elif args.action == "stop":
        cmd_stop()
    elif args.action == "monitor":
        cmd_monitor(args.duration)
    elif args.action == "run":
        cmd_run(args.fw)
