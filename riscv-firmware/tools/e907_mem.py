#!/usr/bin/env python3
"""
e907_mem.py - XuanTie E907 Address Translation & SRAM Live Inspection Tool
Radxa Cubie A5E (Allwinner A527) & Cubie A7A (T527)

Provides:
1. Automatic SoC detection (A527 vs T527) from Device Tree.
2. Bidirectional address translation: Core DA (0x40000000) <-> Host Physical.
3. ELF symbol table resolution in pure Python (no external dependencies).
4. Direct /dev/mem inspection, memory watching, and core status decoding.
"""

import sys
import os
import time
import struct
import mmap
import argparse

# -----------------------------------------------------------------------------
# SoC Memory Map Profiles
# -----------------------------------------------------------------------------
SOC_MAPS = {
    "a527": {
        "name": "Allwinner A527 / A523 (Radxa Cubie A5E)",
        "sram0_phys": 0x07280000,
        "sram0_da":   0x3FFC0000,
        "sram0_size": 256 * 1024,
        "sram1_phys": 0x072C0000,
        "sram1_da":   0x40000000,
        "sram1_size": 256 * 1024,
        "cfg_phys":   0x07130000,
        "cfg_da":     0x07130000,
        "cfg_size":   4 * 1024,
        "dma_phys":   0x48100000,
        "dma_da":     0x48100000,
        "dma_size":   1024 * 1024,
    },
    "t527": {
        "name": "Allwinner T527 / A733",
        "sram0_phys": 0x07280000,
        "sram0_da":   0x3FFC0000,
        "sram0_size": 256 * 1024,
        "sram1_phys": 0x072C0000,
        "sram1_da":   0x40000000,
        "sram1_size": 256 * 1024,
        "cfg_phys":   0x07130000,
        "cfg_da":     0x07130000,
        "cfg_size":   4 * 1024,
        "dma_phys":   0x48100000,
        "dma_da":     0x48100000,
        "dma_size":   1024 * 1024,
    }
}

# Known default symbol offsets (for testBasic)
KNOWN_TESTBASIC_SYMBOLS = {
    "dtcm_scratch":       0x3FFC5050,
    "g_rproc_trace_buffer": 0x3FFC5428,
    "trace0":             0x3FFC5428,
    "sram_c_loc1":        0x3FFC6428,
    "sram_c_loc2":        0x3FFC6430,
    "crash_signature":    0x3FFFFF00,
    "work_mode":          0x07130248,
    "sta_add":            0x07130204,
}

def detect_soc():
    """Detect SoC model from device tree on the board."""
    compatible_paths = [
        "/sys/firmware/devicetree/base/compatible",
        "/proc/device-tree/compatible",
    ]
    for p in compatible_paths:
        if os.path.exists(p):
            try:
                with open(p, "rb") as f:
                    compat = f.read().decode("latin-1", errors="ignore").lower()
                if "t527" in compat or "a733" in compat:
                    return "t527"
                if "a527" in compat or "a523" in compat or "cubie_a5e" in compat or "cubie-a5e" in compat:
                    return "a527"
            except Exception:
                pass
    return "a527" # Default to Cubie A5E

def da_to_phys(da, soc_key="a527"):
    """Translate E907 Core DA to Host Physical Address."""
    soc = SOC_MAPS[soc_key]

    # Forbidden Zones
    if da < 0x00020000:
        return None, "FORBIDDEN (BootROM)"
    if 0x00020000 <= da < 0x00040000:
        return None, "FORBIDDEN (Cadence HiFi4 DSP Local RAM collision!)"
    if 0x00044000 <= da < 0x00068000:
        return None, "FORBIDDEN (Secure OP-TEE / TrustZone SRAM A2)"

    # Space 0 (r_sram)
    if soc["sram0_da"] <= da < (soc["sram0_da"] + soc["sram0_size"]):
        offset = da - soc["sram0_da"]
        return soc["sram0_phys"] + offset, f"SRAM_A3 Space 0 (offset +0x{offset:X})"

    # Space 1 (r_sram1)
    if soc["sram1_da"] <= da < (soc["sram1_da"] + soc["sram1_size"]):
        offset = da - soc["sram1_da"]
        return soc["sram1_phys"] + offset, f"SRAM_A3 Space 1 (offset +0x{offset:X})"

    # CFG Registers
    if soc["cfg_da"] <= da < (soc["cfg_da"] + soc["cfg_size"]):
        offset = da - soc["cfg_da"]
        return soc["cfg_phys"] + offset, f"RISC-V CFG Block (offset +0x{offset:X})"

    # DDR DMA Pool
    if soc["dma_da"] <= da < (soc["dma_da"] + soc["dma_size"]):
        offset = da - soc["dma_da"]
        return soc["dma_phys"] + offset, f"DDR DMA Carveout (offset +0x{offset:X})"

    return None, "UNMAPPED"

def phys_to_da(phys, soc_key="a527"):
    """Translate Host Physical Address to E907 Core DA."""
    soc = SOC_MAPS[soc_key]

    if soc["sram0_phys"] <= phys < (soc["sram0_phys"] + soc["sram0_size"]):
        offset = phys - soc["sram0_phys"]
        return soc["sram0_da"] + offset, f"SRAM_A3 Space 0 (offset +0x{offset:X})"

    if soc["sram1_phys"] <= phys < (soc["sram1_phys"] + soc["sram1_size"]):
        offset = phys - soc["sram1_phys"]
        return soc["sram1_da"] + offset, f"SRAM_A3 Space 1 (offset +0x{offset:X})"

    if soc["cfg_phys"] <= phys < (soc["cfg_phys"] + soc["cfg_size"]):
        offset = phys - soc["cfg_phys"]
        return soc["cfg_da"] + offset, f"RISC-V CFG Block (offset +0x{offset:X})"

    if soc["dma_phys"] <= phys < (soc["dma_phys"] + soc["dma_size"]):
        offset = phys - soc["dma_phys"]
        return soc["dma_da"] + offset, f"DDR DMA Carveout (offset +0x{offset:X})"

    return None, "UNMAPPED"

def parse_elf_symbols(elf_path):
    """Simple 32-bit ELF symbol table extractor in pure Python."""
    if not os.path.isfile(elf_path):
        return {}
    try:
        with open(elf_path, "rb") as f:
            data = f.read()

        if len(data) < 52 or data[:4] != b"\x7fELF":
            return {}

        e_shoff = struct.unpack_from("<I", data, 32)[0]
        e_shentsize = struct.unpack_from("<H", data, 46)[0]
        e_shnum = struct.unpack_from("<H", data, 48)[0]
        e_shstrndx = struct.unpack_from("<H", data, 50)[0]

        # Read section headers
        sections = []
        for i in range(e_shnum):
            off = e_shoff + (i * e_shentsize)
            sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link = \
                struct.unpack_from("<IIIIIII", data, off)
            sections.append({
                "type": sh_type,
                "offset": sh_offset,
                "size": sh_size,
                "link": sh_link,
            })

        # Locate .symtab (type 2) and .strtab (type 3)
        symbols = {}
        for s in sections:
            if s["type"] == 2: # SHT_SYMTAB
                sym_data = data[s["offset"] : s["offset"] + s["size"]]
                str_s = sections[s["link"]]
                str_data = data[str_s["offset"] : str_s["offset"] + str_s["size"]]

                num_syms = len(sym_data) // 16
                for idx in range(num_syms):
                    st_name, st_value, st_size, st_info, st_other, st_shndx = \
                        struct.unpack_from("<IIIBBH", sym_data, idx * 16)
                    if st_name < len(str_data):
                        end = str_data.find(b"\x00", st_name)
                        sym_name = str_data[st_name:end].decode("latin-1", errors="ignore")
                        if sym_name and st_value > 0:
                            symbols[sym_name] = st_value
        return symbols
    except Exception:
        return {}

def read_phys_memory(phys_addr, num_words=1):
    """Read 32-bit words from physical memory via /dev/mem."""
    PAGE_SIZE = 4096
    base = phys_addr & ~(PAGE_SIZE - 1)
    page_offset = phys_addr - base
    bytes_needed = page_offset + (num_words * 4)

    try:
        with open("/dev/mem", "rb") as f:
            with mmap.mmap(f.fileno(), bytes_needed, flags=mmap.MAP_SHARED,
                           prot=mmap.PROT_READ, offset=base) as mm:
                words = []
                for i in range(num_words):
                    val = struct.unpack_from("<I", mm, page_offset + (i * 4))[0]
                    words.append(val)
                return words
    except PermissionError:
        print("[!] ERROR: Reading /dev/mem requires root privileges (run with sudo).")
        sys.exit(1)
    except Exception as e:
        print(f"[!] ERROR reading /dev/mem: {e}")
        return None

def main():
    parser = argparse.ArgumentParser(
        description="XuanTie E907 RISC-V Address Translation & SRAM Inspection Tool"
    )
    parser.add_argument("target", nargs="?", default="sram_c_loc1",
                        help="Symbol name (e.g. sram_c_loc1, trace0), Core DA (0x40006058), or Host Phys (0x07286058)")
    parser.add_argument("--elf", "-e", default=None,
                        help="Path to ELF binary for symbol lookup (e.g. /lib/firmware/testBasic.elf)")
    parser.add_argument("--soc", choices=["a527", "t527"], default=None,
                        help="Force SoC profile (default: auto-detect from Device Tree)")
    parser.add_argument("--read", "-r", type=int, default=4,
                        help="Number of 32-bit words to read from physical memory (default: 4)")
    parser.add_argument("--watch", "-w", action="store_true",
                        help="Watch memory address live, polling every 500ms")
    parser.add_argument("--trace", "-t", action="store_true",
                        help="Dump RemoteProc trace0 buffer content from physical SRAM")
    parser.add_argument("--status", "-s", action="store_true",
                        help="Inspect RISC-V CFG Status (WORK_MODE_REG 0x07130248 & STA_ADD 0x07130204)")

    args = parser.parse_args()

    soc_key = args.soc or detect_soc()
    soc = SOC_MAPS[soc_key]

    print(f"\033[1;36m======================================================================\033[0m")
    print(f"\033[1;36m  XuanTie E907 Memory Translation & Inspection Tool\033[0m")
    print(f"  SoC Detected : \033[1;32m{soc['name']}\033[0m (profile: {soc_key})")
    print(f"  SRAM Space 0 : DA 0x{soc['sram0_da']:08X} -> Host Phys 0x{soc['sram0_phys']:08X} (256 KB)")
    print(f"  SRAM Space 1 : DA 0x{soc['sram1_da']:08X} -> Host Phys 0x{soc['sram1_phys']:08X} (256 KB)")
    print(f"\033[1;36m======================================================================\033[0m")

    # Handle Core Status Query
    if args.status:
        phys_work_mode = soc["cfg_phys"] + 0x248
        phys_sta_add   = soc["cfg_phys"] + 0x204
        vals = read_phys_memory(phys_work_mode, 1)
        boot = read_phys_memory(phys_sta_add, 1)
        if vals and boot:
            wm = vals[0]
            sa = boot[0]
            print(f"\n[Hardware Status Regs]")
            print(f"  STA_ADD_REG   (0x{phys_sta_add:08X}) : 0x{sa:08X} (Boot Vector)")
            print(f"  WORK_MODE_REG (0x{phys_work_mode:08X}) : 0x{wm:08X}")
            mcu_run = bool(wm & (1 << 0))
            run_sta = bool(wm & (1 << 1))
            lock_sta = bool(wm & (1 << 3))
            print(f"    - MCU_RUN     (Bit 0): {mcu_run} ({'Running/Active' if mcu_run else 'In Reset'})")
            print(f"    - BIT_RUN_STA (Bit 1): {run_sta}")
            print(f"    - BIT_LOCK_STA(Bit 3): {lock_sta} (\033[1;{'31mSILICON LOCKUP DETECTED' if lock_sta else '32mClean / No Lockup'}\033[0m)")
        return

    # Handle Trace Dump
    if args.trace:
        trace_da = KNOWN_TESTBASIC_SYMBOLS["trace0"]
        phys, region = da_to_phys(trace_da, soc_key)
        print(f"\n[Trace0 Direct Physical SRAM Dump]")
        print(f"  Trace0 DA   : 0x{trace_da:08X}")
        print(f"  Host Phys   : 0x{phys:08X} ({region})")
        words = read_phys_memory(phys, 128)
        if words:
            raw_bytes = struct.pack(f"<{len(words)}I", *words)
            text = raw_bytes.split(b"\x00")[0].decode("latin-1", errors="replace")
            print(f"  --- Live Log Content ({len(text)} bytes) ---")
            print(text)
        return

    # Resolve target to address
    symbols = dict(KNOWN_TESTBASIC_SYMBOLS)
    if args.elf and os.path.isfile(args.elf):
        elf_syms = parse_elf_symbols(args.elf)
        if elf_syms:
            symbols.update(elf_syms)
            print(f"[*] Loaded {len(elf_syms)} symbols from {args.elf}")

    target_str = args.target.strip()
    target_addr = None
    target_name = target_str

    if target_str in symbols:
        target_addr = symbols[target_str]
        target_name = target_str
    elif target_str.startswith("0x") or target_str.startswith("0X"):
        try:
            target_addr = int(target_str, 16)
        except ValueError:
            pass
    else:
        try:
            target_addr = int(target_str)
        except ValueError:
            pass

    if target_addr is None:
        print(f"[!] Unknown symbol or address '{target_str}'. Known symbols:")
        for k, v in symbols.items():
            print(f"    {k:20s} -> 0x{v:08X}")
        sys.exit(1)

    # Determine if address is Core DA or Host Physical
    if 0x3FFC0000 <= target_addr < 0x40080000:
        da = target_addr
        phys, region = da_to_phys(da, soc_key)
    elif target_addr in (soc["sram0_phys"], soc["sram1_phys"]) or \
         (soc["sram0_phys"] <= target_addr < soc["sram0_phys"] + soc["sram0_size"]) or \
         (soc["sram1_phys"] <= target_addr < soc["sram1_phys"] + soc["sram1_size"]):
        phys = target_addr
        da, region = phys_to_da(phys, soc_key)
    elif target_addr == 0x07130248:
        da = 0x07130248
        phys = 0x07130248
        region = "RISC-V CFG Block (WORK_MODE_REG)"
    else:
        da = target_addr
        phys, region = da_to_phys(da, soc_key)

    print(f"\n[Address Translation Results for '{target_name}']")
    print(f"  E907 Core DA   : \033[1;33m0x{da:08X}\033[0m" if da else "  E907 Core DA   : N/A")
    print(f"  Host Physical  : \033[1;32m0x{phys:08X}\033[0m" if phys else "  Host Physical  : N/A")
    print(f"  Memory Region  : {region}")
    if phys:
        print(f"  devmem2 syntax : devmem2 0x{phys:08X} w {args.read}")

    if not phys:
        print("[!] Target address cannot be read from host physical memory.")
        return

    # Check if user requested reading or watching memory
    if os.path.exists("/dev/mem"):
        print(f"\n[Live Physical Memory Read via /dev/mem]")
        if args.watch:
            print(f"[*] Watching 0x{phys:08X} (DA 0x{da:08X}). Press Ctrl+C to stop.\n")
            prev_val = None
            while True:
                words = read_phys_memory(phys, args.read)
                if words:
                    curr_val = words[0]
                    t_str = time.strftime("%H:%M:%S")
                    hex_words = " ".join([f"0x{w:08X}" for w in words])
                    delta_str = f"(+0x{curr_val - prev_val:X})" if prev_val is not None else ""
                    print(f"[{t_str}] [0x{phys:08X}] -> {hex_words} {delta_str}")
                    prev_val = curr_val
                time.sleep(0.5)
        else:
            words = read_phys_memory(phys, args.read)
            if words:
                for idx, w in enumerate(words):
                    cur_phys = phys + (idx * 4)
                    cur_da = (da + (idx * 4)) if da else None
                    da_desc = f"(DA: 0x{cur_da:08X})" if cur_da else ""
                    # ASCII representation if printable
                    b = struct.pack("<I", w)
                    asc = "".join([chr(c) if 32 <= c <= 126 else "." for c in b])
                    print(f"  [0x{cur_phys:08X}] {da_desc:18s} = 0x{w:08X}  ('{asc}')")

if __name__ == "__main__":
    main()
