#!/usr/bin/env python3
import sys
import re
import os
import subprocess

NM_TOOL = "/home/tcmichals/.tools/gcc-riscv-none-eabi/bin/riscv-none-elf-nm"

def analyze_firmware(map_file, elf_file):
    if not os.path.exists(map_file) or not os.path.exists(elf_file):
        print(f"Error: Could not find files. Run 'make' first.")
        sys.exit(1)

    regions = {}
    
    # 1. Parse Memory Regions from Map File
    with open(map_file, 'r') as f:
        lines = f.readlines()

    in_memory_config = False
    for line in lines:
        if line.strip() == "Memory Configuration":
            in_memory_config = True
            continue
        if in_memory_config:
            if line.strip() == "Linker script and memory map":
                break
            parts = line.split()
            if len(parts) >= 3 and parts[0] != "Name" and parts[0] != "*default*":
                regions[parts[0]] = {
                    "origin": int(parts[1], 16),
                    "length": int(parts[2], 16),
                    "used": 0,
                    "symbols": []
                }

    # 2. Get Section Usage from Map File
    for line in lines:
        match = re.match(r'^(\.[a-zA-Z0-9_\.-]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)', line)
        if match:
            sec_name, sec_addr_str, sec_size_str = match.groups()
            sec_addr, sec_size = int(sec_addr_str, 16), int(sec_size_str, 16)
            if any(skip in sec_name for skip in ['.debug', '.comment', '.riscv', '.symtab', '.strtab']):
                continue
            for r_name, r_data in regions.items():
                if r_data["origin"] <= sec_addr < (r_data["origin"] + r_data["length"]):
                    regions[r_name]["used"] += sec_size
                    break

    # 3. Get Top Contributors from ELF using nm
    try:
        output = subprocess.check_output(f"{NM_TOOL} -S --size-sort -r {elf_file}", shell=True, text=True)
        for line in output.strip().split('\n'):
            parts = line.split()
            if len(parts) >= 4:
                addr, size, sym_type, name = int(parts[0], 16), int(parts[1], 16), parts[2], " ".join(parts[3:])
                # Filter out linker absolutes (massive fake sizes)
                if size > 0 and size < 0x100000:
                    for r_name, r_data in regions.items():
                        if r_data["origin"] <= addr < (r_data["origin"] + r_data["length"]):
                            regions[r_name]["symbols"].append((name, size, sym_type))
                            break
    except Exception as e:
        print(f"Warning: Could not run nm tool: {e}")

    # 4. Print Beautiful Report
    print("="*65)
    print(f"{'MEMORY REGION':<15} | {'USED':<10} | {'TOTAL':<10} | {'UTILIZATION'}")
    print("="*65)
    for r_name, r_data in regions.items():
        used = r_data["used"]
        total = r_data["length"]
        percent = (used / total) * 100 if total > 0 else 0
        bar = "[" + "#" * int(20 * (used / total)) + "-" * (20 - int(20 * (used / total))) + "]"
        print(f"{r_name:<15} | {used:>6} B   | {total//1024:>4} KB   | {bar} {percent:5.1f}%")
        
        # Print Top 5 Contributors for this region
        if r_data["symbols"]:
            print(f"  └─ Top Contributors in {r_name}:")
            for sym_name, sym_size, sym_type in r_data["symbols"][:5]:
                print(f"       {sym_size:>5} B  [{sym_type}] {sym_name}")
            print("")
    print("="*65)

if __name__ == "__main__":
    analyze_firmware("firmware.map", "firmware.elf")
