#!/usr/bin/env python3
"""
generate_devmem_map.py - Automatic Memory Translation & Devmem Report Generator
Runs during 'make' for any RISC-V firmware target.

Generates:
  <target>_devmem.md  (Markdown documentation table and devmem2 cheat-sheet)

Translates E907 Core Device Addresses (DA) into Host Linux Physical Addresses
for both Allwinner A527 (Cubie A5E) and T527 (Cubie A7A).
"""

import sys
import os
import re
import subprocess

# Verified Physical Bases for Allwinner Sun55i (A523 / A527 / T527)
# Note: 0x07200000 does NOT exist in silicon memory map and triggers a Bus Error.
SRAM0_PHYS_BASE = 0x07280000  # r_sram  (SRAM_A3 Space 0, Core DA 0x3FFC0000, 256 KB)
SRAM1_PHYS_BASE = 0x072C0000  # r_sram1 (SRAM_A3 Space 1, Core DA 0x40000000, 256 KB)
CFG_PHYS_BASE   = 0x07130000  # RISC-V CFG Block
DMA_PHYS_BASE   = 0x48100000  # DDR DMA Carveout

def da_to_phys(da):
    if 0x3FFC0000 <= da < 0x40000000:
        offset = da - 0x3FFC0000
        return SRAM0_PHYS_BASE + offset, f"SRAM_A3 Space 0 (+0x{offset:X})"
    if 0x40000000 <= da < 0x40040000:
        offset = da - 0x40000000
        return SRAM1_PHYS_BASE + offset, f"SRAM_A3 Space 1 (+0x{offset:X})"
    if 0x07130000 <= da < 0x07131000:
        offset = da - 0x07130000
        return CFG_PHYS_BASE + offset, f"CFG Block (+0x{offset:X})"
    if 0x40040000 <= da < 0x80000000:
        return da, f"DDR DRAM Carveout (+0x{da - 0x40040000:X})"
    return None, "Unmapped/Forbidden"

def extract_symbols_from_map(map_path):
    """Parse GNU ld map file for memory sections and variable symbols."""
    symbols = {}
    if not os.path.isfile(map_path):
        return symbols

    # Key symbols of interest to look for
    interest = [
        "sram_c_loc1", "sram_c_loc2", "dtcm_scratch",
        "g_rproc_trace_buffer", "__trace_start", "__trace_end",
        "__sram_c_start", "__sram_c_end", "_stack_top", "_stack_bottom",
        "global_resource_table", "g_trace_head",
        "main", "_start", "default_trap_entry"
    ]

    with open(map_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            for item in interest:
                # Match symbol definitions like:
                # 0x40006078                sram_c_loc1
                # or build/main.o: 0x40006078 sram_c_loc1
                # or demangled: _ZL11sram_c_loc1
                m = re.search(r"(0x[0-9a-fA-F]{8})\s+(?:.*(?:_ZL\d+)?|\s*)(" + re.escape(item) + r")\b", line)
                if m:
                    addr_val = int(m.group(1), 16)
                    sym_name = item
                    if sym_name not in symbols or addr_val > 0:
                        symbols[sym_name] = addr_val
    return symbols

def extract_symbols_from_nm(elf_path, cross_compile=""):
    """Extract symbols using nm if available."""
    symbols = {}
    nm_bin = cross_compile + "nm"
    if not os.path.isfile(elf_path):
        return symbols

    cmd = [nm_bin, "-C", elf_path] if cross_compile else ["nm", "-C", elf_path]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
        for line in res.stdout.splitlines():
            parts = line.strip().split()
            if len(parts) >= 3:
                try:
                    addr = int(parts[0], 16)
                    name = parts[2]
                    # Filter for symbols of interest
                    for key in ["sram_c_loc1", "sram_c_loc2", "dtcm_scratch",
                                "g_rproc_trace_buffer", "__trace_start", "__trace_end",
                                "__sram_c_start", "__sram_c_end", "_stack_top",
                                "global_resource_table", "g_trace_head", "main", "_start"]:
                        if key in name:
                            symbols[key] = addr
                except ValueError:
                    continue
    except Exception:
        pass
    return symbols

def main():
    if len(sys.argv) < 3:
        print("Usage: generate_devmem_map.py <target.elf> <target.map> [output_prefix] [cross_compile_prefix]")
        sys.exit(1)

    elf_path = sys.argv[1]
    map_path = sys.argv[2]
    target_name = sys.argv[3] if len(sys.argv) > 3 else os.path.splitext(os.path.basename(elf_path))[0]
    cross_compile = sys.argv[4] if len(sys.argv) > 4 else ""

    symbols = extract_symbols_from_nm(elf_path, cross_compile)
    map_symbols = extract_symbols_from_map(map_path)
    # Merge map symbols
    for k, v in map_symbols.items():
        if k not in symbols or symbols[k] == 0:
            symbols[k] = v

    # Add hardcoded fallback for well-known fixed hardware registers
    symbols["WORK_MODE_REG (CFG)"] = 0x07130248
    symbols["STA_ADD_REG   (CFG)"] = 0x07130204

    # Sort symbols by address
    sorted_syms = sorted(symbols.items(), key=lambda x: (x[1] if x[1] else 0))

    # 1. Generate Markdown Report
    md_file = f"{target_name}_devmem.md"

    with open(md_file, "w", encoding="utf-8") as md:
        md.write(f"# Linux Physical Address & `devmem2` Map: `{target_name}`\n\n")
        md.write(f"Generated automatically during build from `{os.path.basename(elf_path)}`.\n\n")
        md.write("This document maps XuanTie E907 RISC-V Core Device Addresses (`DA`) to **Linux Host (ARM64) Physical Addresses** for direct memory inspection in Linux process space via `devmem2` or `/dev/mem`.\n\n")
        
        md.write("> [!CAUTION]\n")
        md.write("> **NEVER USE `0x07200000` (CAUSES HARDWARE BUS ERROR)**\n")
        md.write("> In the actual Sun55i silicon bus architecture, `0x07200000` is unmapped and accessing it triggers an immediate hardware **Bus error** (external abort). The primary on-chip SRAM (`r_sram` / SRAM_A3 Space 0) is physically located at **`0x07280000`**.\n\n")

        md.write("## 1. Symbol Address Translation Table\n\n")
        md.write("| Symbol Name | E907 Core DA | Host Physical Address | Memory Region | Linux `devmem2` Command |\n")
        md.write("| :--- | :--- | :--- | :--- | :--- |\n")

        for name, da in sorted_syms:
            phys, reg = da_to_phys(da)

            phys_str = f"`0x{phys:08X}`" if phys else "N/A"
            devmem_cmd = f"`devmem2 0x{phys:08X} w`" if phys else "N/A"

            md.write(f"| **`{name}`** | `0x{da:08X}` | {phys_str} | {reg} | {devmem_cmd} |\n")

        md.write("\n---\n\n")
        md.write("## 2. Inspecting in Linux Process Space\n\n")
        md.write("### Option A: Shell Inspection via `devmem2`\n")
        md.write("```bash\n")
        if "sram_c_loc1" in symbols:
            p, _ = da_to_phys(symbols["sram_c_loc1"])
            md.write(f"# Read sram_c_loc1 magic (0xDEADBEEF) and counter:\n")
            md.write(f"devmem2 0x{p:08X} w 2\n\n")
        if "sram_c_loc2" in symbols:
            p2, _ = da_to_phys(symbols["sram_c_loc2"])
            md.write(f"# Read sram_c_loc2 magic ('RISC' / 0x52495343) and counter:\n")
            md.write(f"devmem2 0x{p2:08X} w 2\n\n")
        md.write("# Check RISC-V Hardware Lockup / Run Status:\n")
        md.write("devmem2 0x07130248 w\n")
        md.write("```\n\n")

        md.write("### Option B: Python Direct Process Address Space Mapping\n")
        md.write("```python\n")
        md.write("import mmap, struct\n\n")
        md.write("# Open physical memory and map SRAM Space 0 page\n")
        md.write("with open('/dev/mem', 'r+b') as f:\n")
        if "sram_c_loc1" in symbols:
            p, _ = da_to_phys(symbols["sram_c_loc1"])
            page_base = p & ~0xFFF
            offset = p & 0xFFF
            md.write(f"    # Page Base: 0x{page_base:08X}, Offset: 0x{offset:03X} (sram_c_loc1)\n")
            md.write(f"    mm = mmap.mmap(f.fileno(), 0x1000, offset=0x{page_base:08X})\n")
            md.write(f"    magic, count = struct.unpack_from('<II', mm, 0x{offset:X})\n")
            md.write("    print(f'sram_c_loc1: Magic=0x{magic:08X}, Counter={count}')\n")
        md.write("```\n")

    # Print summary to terminal
    print(f"\n\033[1;32m[Memory Map Generated]\033[0m -> {md_file}")
    print(f"{'SYMBOL':<22} | {'CORE DA':<10} | {'PHYS ADDR':<10} | {'LINUX DEVMEM2 COMMAND'}")
    print("-" * 68)
    for name in ["sram_c_loc1", "sram_c_loc2", "dtcm_scratch", "g_rproc_trace_buffer"]:
        if name in symbols:
            da = symbols[name]
            pa, _ = da_to_phys(da)
            print(f"{name:<22} | 0x{da:08X} | 0x{pa:08X} | devmem2 0x{pa:08X} w 2")
    print("-" * 68)

if __name__ == "__main__":
    main()
