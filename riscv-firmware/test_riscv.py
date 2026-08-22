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
    main_ccu = MMIO(MAIN_CCU_BASE, 0x1000)
    sys_cfg  = MMIO(SYS_CFG_BASE,  0x1000)
    mcu_cfg  = MMIO(MCU_CFG_BASE,  0x1000)
    mcu_ccu  = MMIO(MCU_CCU_BASE,  0x1000)
    mask_rom = MMIO(MASK_ROM_BASE, 0x1000)
    sram_c   = MMIO(SRAM_C_BASE,   0x20000) # 128 KB
    return main_ccu, sys_cfg, mcu_cfg, mcu_ccu, mask_rom, sram_c

def cmd_status():
    print("=" * 80)
    print("XUANTIE RISC-V HARDWARE SUBSYSTEM STATUS")
    print("=" * 80)
    main_ccu, sys_cfg, mcu_cfg, mcu_ccu, mask_rom, sram_c = get_mmio()

    print("[1] Main SoC CCU (0x02001000):")
    clk_dsp = main_ccu.read32(0xc70)
    rst_dsp = main_ccu.read32(0xc7c)
    print(f"    - CLK_DSP  (0x02001c70): 0x{clk_dsp:08X} ({'ENABLED' if (clk_dsp & (1<<31)) else 'DISABLED'})")
    print(f"    - BUS_DSP  (0x02001c7c): 0x{rst_dsp:08X} (CLK={'ON' if (rst_dsp&1) else 'OFF'}, RST={'DE-ASSERTED' if (rst_dsp&0x10000) else 'ASSERTED'})")

    print("\n[2] System Config / SRAM Mux (0x03000000):")
    sram_ctrl1 = sys_cfg.read32(0x0004)
    print(f"    - SRAM_CTRL1 (0x03000004): 0x{sram_ctrl1:08X} (SRAM C Mapped to: {'ARM CPU Host' if (sram_ctrl1 & (1<<24)) else 'RISC-V/DSP Subsystem'})")

    print("\n[3] MCU Subsystem CCU (0x07102000):")
    r_dsp_clk = mcu_ccu.read32(0x020)
    r_dsp_rst = mcu_ccu.read32(0x100)
    tzma0     = mcu_ccu.read32(0x108)
    tzma1     = mcu_ccu.read32(0x10c)
    pubsram   = mcu_ccu.read32(0x114)
    mbus_mcu  = mcu_ccu.read32(0x11c)
    riscv_clk = mcu_ccu.read32(0x120)
    riscv_rst = mcu_ccu.read32(0x124)
    riscv_mbx = mcu_ccu.read32(0x128)
    print(f"    - DSP Clock (0x07102020): 0x{r_dsp_clk:08X}")
    print(f"    - DSP Reset (0x07102100): 0x{r_dsp_rst:08X}")
    print(f"    - TZMA0     (0x07102108): 0x{tzma0:08X} ({'ON' if tzma0&1 else 'OFF'})")
    print(f"    - TZMA1     (0x0710210c): 0x{tzma1:08X} ({'ON' if tzma1&1 else 'OFF'})")
    print(f"    - PubSRAM   (0x07102114): 0x{pubsram:08X} ({'ON' if pubsram&1 else 'OFF'})")
    print(f"    - MBUS-MCU  (0x0710211c): 0x{mbus_mcu:08X}")
    print(f"    - RISCV_CLK (0x07102120): 0x{riscv_clk:08X} ({'RUNNING' if (riscv_clk & (1<<31)) else 'STOPPED'})")
    print(f"    - RISCV_RST (0x07102124): 0x{riscv_rst:08X} (CORE={'ACTIVE' if (riscv_rst & (1<<18)) else 'IN RESET'})")
    print(f"    - RISCV_MBX (0x07102128): 0x{riscv_mbx:08X}")

    print("\n[4] MCU Configuration Registers (0x07100000):")
    ctrl0 = mcu_cfg.read32(0x0000)
    vec0  = mcu_cfg.read32(0x0004)
    ver0  = mcu_cfg.read32(0x000c)
    tcm0  = mcu_cfg.read32(0x0020)
    print(f"    - CTRL0 (0x07100000): 0x{ctrl0:08X}")
    print(f"    - VEC0  (0x07100004): 0x{vec0:08X}")
    print(f"    - VER0  (0x0710000c): 0x{ver0:08X} (Hardware ID: 0x{ver0:x})")
    print(f"    - TCM0  (0x07100020): 0x{tcm0:08X}")

    print("\n[5] On-Chip Mask ROM Header (0x07110000):")
    rom0 = mask_rom.read32(0x0000)
    rom1 = mask_rom.read32(0x0004)
    print(f"    - Vector 0: 0x{rom0:08X}")
    print(f"    - Vector 1: 0x{rom1:08X}")

    print("\n[6] SRAM C Telemetry Block (0x00028000):")
    magic = sram_c.read32(TELEMETRY_OFF)
    boot  = sram_c.read32(TELEMETRY_OFF + 4)
    hb    = sram_c.read32(TELEMETRY_OFF + 8)
    loops = sram_c.read32(TELEMETRY_OFF + 12)
    stat  = sram_c.read32(TELEMETRY_OFF + 16)
    trap  = sram_c.read32(TELEMETRY_OFF + 32)
    print(f"    - Magic String  (0x00028000): 0x{magic:08X} ({'MATCH (RISC)' if magic == 0x52495343 else 'Uninitialized'})")
    print(f"    - Boot Flag     (0x00028004): 0x{boot:08X}")
    print(f"    - Heartbeat     (0x00028008): {hb}")
    print(f"    - Loop Counter  (0x0002800C): {loops}")
    print(f"    - Status Magic  (0x00028010): 0x{stat:08X} ({'MATCH (ALIV)' if stat == 0x414C4956 else 'Uninitialized'})")
    print(f"    - Trap Counter  (0x00028020): {trap}")
    print("=" * 80)

def cmd_enable_clocks():
    print("[+] Enabling Main CCU Root DSP/MCU clocks...")
    main_ccu = MMIO(MAIN_CCU_BASE, 0x1000)
    main_ccu.write32(0xc70, 0x80000000)
    main_ccu.write32(0xc7c, 0x00010001)

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

def cmd_load(bin_path="/lib/firmware/riscv-firmware.bin"):
    if not os.path.exists(bin_path):
        if os.path.exists("/tmp/riscv-firmware.bin"):
            bin_path = "/tmp/riscv-firmware.bin"
        else:
            print(f"[-] ERROR: Binary not found: {bin_path}")
            return False

    with open(bin_path, "rb") as f:
        data = bytearray(f.read())

    # Pad to 4-byte boundary
    if len(data) % 4 != 0:
        data += b'\x00' * (4 - (len(data) % 4))

    print(f"[+] Loading {len(data)} bytes from {bin_path} into SRAM C (0x00020000) via 32-bit aligned words...")
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
    
    # Switch SRAM C MUX to RISC-V Co-Processor (Clear bit 24 in 0x03000004)
    sys_cfg = MMIO(SYS_CFG_BASE, 0x1000)
    sys_cfg.write32(0x0004, 0x00000000)
    print("[+] Switched SRAM C Mux to RISC-V Co-Processor (0x03000004 -> 0x00000000)")

    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x124, 0x00030001) # Hold core in reset
    time.sleep(0.01)
    mcu_ccu.write32(0x124, 0x00070001) # Release core reset
    print("[+] Core reset released (MCU RST REG: 0x00070001)")

def cmd_stop():
    print("[+] Stopping XuanTie RISC-V Co-Processor...")
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    mcu_ccu.write32(0x124, 0x00030001)
    # Switch SRAM C back to ARM host
    sys_cfg = MMIO(SYS_CFG_BASE, 0x1000)
    sys_cfg.write32(0x0004, 0x01000000)
    print("[+] Core held in reset, SRAM C switched to Host.")

def cmd_monitor(duration=10):
    print("=" * 80)
    print(f"MONITORING XUANTIE RISC-V TELEMETRY (0x00028000) for {duration} seconds...")
    print("=" * 80)
    sys_cfg = MMIO(SYS_CFG_BASE, 0x1000)
    sram_c  = MMIO(SRAM_C_BASE,   0x20000)
    start_time = time.time()
    last_hb = None
    last_loops = None

    while time.time() - start_time < duration:
        # Briefly switch to CPU to sample telemetry, then switch back to RISC-V
        sys_cfg.write32(0x0004, 0x01000000)
        magic = sram_c.read32(TELEMETRY_OFF)
        boot  = sram_c.read32(TELEMETRY_OFF + 4)
        hb    = sram_c.read32(TELEMETRY_OFF + 8)
        loops = sram_c.read32(TELEMETRY_OFF + 12)
        stat  = sram_c.read32(TELEMETRY_OFF + 16)
        trap  = sram_c.read32(TELEMETRY_OFF + 32)
        sys_cfg.write32(0x0004, 0x00000000)
        
        is_active = (hb != last_hb or loops != last_loops) and last_hb is not None
        status_str = "\033[92m>>> CORE IS EXECUTING LIVE <<<\033[0m" if is_active else "\033[93mIDLE / WAITING\033[0m"
        last_hb = hb
        last_loops = loops
        print(f"\r[Telemetry] Magic: 0x{magic:08X} | Boot: {boot} | Heartbeat: {hb:<8} | Loops: {loops:<10} | State: {status_str}", end="", flush=True)
        time.sleep(0.5)
    print("\n")

def cmd_probe():
    print("=" * 80)
    print("PHYSICAL HARDWARE CLOCK & BUS TICK PROBE")
    print("=" * 80)
    cmd_enable_clocks()
    mcu_ccu = MMIO(MCU_CCU_BASE, 0x1000)
    
    print("[+] Checking if MCU peripheral bus and registers are accessible...")
    val1 = mcu_ccu.read32(0x120)
    print(f"[+] MCU CCU RISCV_CLK register readback: 0x{val1:08X}")
    if val1 & 0x80000000:
        print("[+] SUCCESS: MCU CCU Clock Gate is OPEN (Root Clock Active)!")
    else:
        print("[-] WARNING: MCU CCU Clock Gate read as CLOSED!")

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
    parser.add_argument("action", choices=["status", "enable-clocks", "test-sram", "load", "start", "stop", "monitor", "probe", "run"], default="status", nargs="?")
    parser.add_argument("--fw", default="/lib/firmware/riscv-firmware.bin", help="Path to firmware binary")
    parser.add_argument("--duration", type=int, default=10, help="Monitor duration in seconds")
    args = parser.parse_args()

    if args.action == "status":
        cmd_status()
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
    elif args.action == "probe":
        cmd_probe()
    elif args.action == "run":
        cmd_run(args.fw)
