#!/usr/bin/env python3
import sys
import re
import subprocess
from pathlib import Path

def strip_comments(text: str) -> str:
    """Strip C (/* */) and C++ (//) style comments from linker script."""
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'//.*', '', text)
    return text

def parse_size_expr(expr_str: str) -> int:
    """Evaluate size expressions like '64K', '4M', '0x10000', or '256K - 4K'."""
    expr_str = expr_str.strip().upper()
    
    # Replace units with multipliers
    # Match numbers immediately followed by K or M (e.g., 64K -> (64*1024))
    def unit_replace(match):
        val = match.group(1)
        unit = match.group(2)
        multiplier = 1024 if unit.startswith('K') else (1024 * 1024)
        return f"({val} * {multiplier})"

    expr_clean = re.sub(r'([0-9a-fA-FxX]+)\s*(K|KB|M|MB)\b', unit_replace, expr_str)
    
    try:
        # Safe eval limited to basic arithmetic
        return int(eval(expr_clean, {"__builtins__": None}, {}))
    except Exception:
        # Fallback to direct hex/dec parsing
        m = re.search(r'0x[0-9A-F]+|\d+', expr_str)
        if m:
            val = m.group(0)
            return int(val, 16) if val.startswith(('0x', '0X')) else int(val)
        return 0

def load_linker_script(ld_path: Path) -> str:
    """Load linker script and recursively resolve INCLUDE directives."""
    if not ld_path.is_file():
        return ""
    
    raw = ld_path.read_text(errors='ignore')
    raw = strip_comments(raw)
    
    # Resolve INCLUDE "file.ld"
    def include_replacer(match):
        inc_name = match.group(1).strip('"\' ')
        inc_path = ld_path.parent / inc_name
        if inc_path.is_file():
            return load_linker_script(inc_path)
        return ""

    raw = re.sub(r'INCLUDE\s+([^\s;]+)', include_replacer, raw)
    return raw

def parse_memory_regions(ld_content: str) -> dict:
    """Extract all memory regions dynamically from the MEMORY block."""
    mem_match = re.search(r'MEMORY\s*\{([^}]+)\}', ld_content, re.DOTALL | re.IGNORECASE)
    if not mem_match:
        return {}

    regions = {}
    mem_block = mem_match.group(1)
    
    # Matches: NAME (ATTRS) : ORIGIN = expr , LENGTH = expr
    # Attributes like (rx), (rwx), or none are optional
    pattern = re.compile(
        r'(\w+)\s*(?:\([^)]*\))?\s*:\s*ORIGIN\s*=\s*([^,]+?)\s*,\s*LENGTH\s*=\s*([^,\n\r}]+)',
        re.IGNORECASE
    )
    
    for match in pattern.finditer(mem_block):
        name = match.group(1).strip()
        orig_expr = match.group(2).strip()
        len_expr = match.group(3).strip()
        
        origin = parse_size_expr(orig_expr)
        length = parse_size_expr(len_expr)
        
        if length > 0:
            regions[name] = {
                'origin': origin,
                'length': length,
                'end': origin + length,
                'used': 0,
                'sections': [],
                'symbols': []
            }
            
    return regions

def main():
    if len(sys.argv) < 5:
        print("Usage: analyze_elf_regions.py <ld_file> <elf_file> <objdump_cmd> <nm_cmd> [top_n]")
        sys.exit(1)

    ld_file = Path(sys.argv[1])
    elf_file = sys.argv[2]
    objdump_cmd = sys.argv[3]
    nm_cmd = sys.argv[4]
    top_n = int(sys.argv[5]) if len(sys.argv) > 5 else 10

    # 1. Parse Linker Script
    ld_content = load_linker_script(ld_file)
    regions = parse_memory_regions(ld_content)
    
    if not regions:
        print(f"    [!] No valid MEMORY regions found in '{ld_file}'.")
        sys.exit(0)

    # 2. Extract ALLOC Sections from ELF
    proc_objdump = subprocess.run([objdump_cmd, '-h', elf_file], capture_output=True, text=True, errors='ignore')
    for line in proc_objdump.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 7 and parts[0].isdigit() and parts[1].startswith('.'):
            sec_name = parts[1]
            try:
                sz = int(parts[2], 16)
                vma = int(parts[3], 16)
                lma = int(parts[4], 16)
            except ValueError:
                continue
            if sz == 0:
                continue
            
            # Map section to region based on VMA (execution address)
            for r_name, r in regions.items():
                if r['origin'] <= vma < r['end']:
                    r['used'] += sz
                    r['sections'].append({'name': sec_name, 'size': sz, 'vma': vma, 'lma': lma})
                    break

    # 3. Extract Symbols and Map to Regions
    proc_nm = subprocess.run([nm_cmd, '--print-size', '--size-sort', '--radix=x', '-C', elf_file], capture_output=True, text=True, errors='ignore')
    for line in proc_nm.stdout.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4:
            continue
        addr_s, size_s, sym_type, name = parts
        try:
            addr = int(addr_s, 16)
            size = int(size_s, 16)
        except ValueError:
            continue
        if size == 0:
            continue

        for r_name, r in regions.items():
            if r['origin'] <= addr < r['end']:
                r['symbols'].append({'name': name, 'size': size, 'addr': addr, 'type': sym_type})
                break

    # 4. Display Overview Table
    print("=" * 80)
    print(f"{'MEMORY REGION':<14} {'ORIGIN':<12} {'CAPACITY':<14} {'USED':<14} {'FREE':<14} {'USAGE %':<8}")
    print("-" * 80)

    for r_name, r in regions.items():
        orig_str = f"0x{r['origin']:08X}"
        tot_kb = r['length'] / 1024.0
        used_kb = r['used'] / 1024.0
        free_kb = (r['length'] - r['used']) / 1024.0
        pct = (r['used'] / r['length'] * 100.0) if r['length'] > 0 else 0.0

        if r['length'] >= 1024 * 1024:
            cap_str = f"{tot_kb/1024.0:7.2f} MB"
            used_str = f"{used_kb/1024.0:7.2f} MB"
            free_str = f"{free_kb/1024.0:7.2f} MB"
        else:
            cap_str = f"{tot_kb:7.1f} KB"
            used_str = f"{used_kb:7.1f} KB"
            free_str = f"{free_kb:7.1f} KB"

        status_flag = "⚠️ OVERFLOW" if r['used'] > r['length'] else ""
        print(f"{r_name:<14} {orig_str:<12} {cap_str:<14} {used_str:<14} {free_str:<14} {pct:6.1f}%  {status_flag}")
    print("=" * 80)
    print("")

    # 5. Detail Breakdown per Region
    for r_name, r in regions.items():
        print(f"▶ REGION: [{r_name}] (0x{r['origin']:08X} - 0x{r['end']:08X})")
        if not r['sections']:
            print("    (No allocated sections in this region)\n")
            continue

        print("    Sections mapped to this region:")
        print(f"      {'Section Name':<20} {'Size (Bytes)':<14} {'Size (KB)':<12} {'VMA (Exec)':<12} {'LMA (Load)':<12}")
        print("      " + "-" * 70)
        for s in sorted(r['sections'], key=lambda x: x['size'], reverse=True):
            print(f"      {s['name']:<20} {s['size']:<14d} {s['size']/1024.0:<12.2f} 0x{s['vma']:08X}   0x{s['lma']:08X}")

        if r['symbols']:
            print(f"\n    Top {top_n} Symbols in [{r_name}]:")
            for sym in sorted(r['symbols'], key=lambda x: x['size'], reverse=True)[:top_n]:
                print(f"      • {sym['size']:7d} bytes  [0x{sym['addr']:08X}] ({sym['type']})  {sym['name']}")
        print("\n" + "." * 80 + "\n")

if __name__ == '__main__':
    main()