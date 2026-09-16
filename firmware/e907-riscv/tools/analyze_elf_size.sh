#!/bin/bash

# Universal Embedded ELF Memory & Linker Region Analyzer
# Dispatches xPack toolchains and dynamically parses any arbitrary linker script (.ld)

set -eo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

ELF_FILE="$1"
LINKER_SCRIPT="$2"
TOP_SYMBOLS_PER_REGION="${TOP_SYMBOLS_PER_REGION:-10}"

if [ -z "$ELF_FILE" ] || [ ! -f "$ELF_FILE" ]; then
    echo -e "${RED}Error: Valid ELF binary required.${NC}"
    echo "Usage: [XPACK_DIR=...] $0 <path_to_elf_file> [path_to_linker_script]"
    echo ""
    echo "Example:"
    echo "  $0 ioprocessor_meta.elf ../../firmware/linker/ioprocessor.ld"
    exit 1
fi

ELF_DIR="$(cd "$(dirname "$ELF_FILE")" && pwd)"
ELF_BASE="$(basename "${ELF_FILE%.elf}")"
REPORT_DIR="${ANALYZE_REPORT_DIR:-$ELF_DIR/analysis_reports}"
mkdir -p "$REPORT_DIR"
RUN_TS="$(date +%Y%m%d_%H%M%S)"
REPORT_FILE="$REPORT_DIR/${ELF_BASE}_memory_report_${RUN_TS}.txt"
CSV_FILE="$REPORT_DIR/${ELF_BASE}_symbols_${RUN_TS}.csv"

# ==============================================================================
# Architecture Detection & Toolchain Discovery
# ==============================================================================

detect_elf_machine() {
    python3 - "$ELF_FILE" <<'EOPY'
import sys, struct

with open(sys.argv[1], 'rb') as f:
    magic = f.read(4)
    if magic != b'\x7fELF':
        print("INVALID")
        sys.exit(0)
    
    f.seek(5)
    endian = '<' if f.read(1) == b'\x01' else '>'
    f.seek(18)
    e_machine = struct.unpack(endian + 'H', f.read(2))[0]

    if e_machine == 243:
        print("RISCV")
    elif e_machine == 40:
        print("ARM")
    elif e_machine == 183:
        print("AARCH64")
    else:
        print(f"UNKNOWN_{e_machine}")
EOPY
}

find_xpack_prefix() {
    local tool_prefix="$1"
    local search_base="${XPACK_DIR:-$HOME/.tools}"

    if [ -d "$search_base" ]; then
        local match
        match=$(find "$search_base" -maxdepth 4 -type f -name "${tool_prefix}nm" 2>/dev/null | sort -V -r | head -n 1 || true)
        if [ -n "$match" ]; then
            echo "${match%nm}"
            return
        fi
    fi

    if command -v "${tool_prefix}nm" &>/dev/null; then
        echo "$tool_prefix"
        return
    fi

    echo ""
}

ARCH_ID="$(detect_elf_machine)"
case "$ARCH_ID" in
    RISCV)
        TOOL_PREFIX="$(find_xpack_prefix "riscv-none-elf-")"
        [ -z "$TOOL_PREFIX" ] && TOOL_PREFIX="$(find_xpack_prefix "riscv64-unknown-elf-")"
        ARCH_NAME="RISC-V (RV32/RV64)"
        ;;
    ARM)
        TOOL_PREFIX="$(find_xpack_prefix "arm-none-eabi-")"
        ARCH_NAME="ARM 32-bit (Cortex-M / Cortex-R)"
        ;;
    AARCH64)
        TOOL_PREFIX="$(find_xpack_prefix "aarch64-none-elf-")"
        ARCH_NAME="AArch64 64-bit (Cortex-A)"
        ;;
    *)
        TOOL_PREFIX=""
        ARCH_NAME="Generic / Unknown ($ARCH_ID)"
        ;;
esac

NM_CMD="${TOOL_PREFIX}nm"
SIZE_CMD="${TOOL_PREFIX}size"
OBJDUMP_CMD="${TOOL_PREFIX}objdump"

if ! command -v "$NM_CMD" &>/dev/null && [ ! -x "$NM_CMD" ]; then
    NM_CMD="nm"
    SIZE_CMD="size"
    OBJDUMP_CMD="objdump"
fi

exec > >(tee "$REPORT_FILE") 2>&1

echo -e "${GREEN}╔════════════════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║             EMBEDDED ELF MEMORY & SEGMENT BREAKDOWN REPORT                 ║${NC}"
echo -e "${GREEN}╚════════════════════════════════════════════════════════════════════════════╝${NC}"
echo "ELF File         : $ELF_FILE"
echo "Architecture     : $ARCH_NAME"
echo "Active Toolchain : $("${TOOL_PREFIX}gcc" --version 2>/dev/null | head -n 1 || echo "${TOOL_PREFIX:-System Native}")"
echo "Linker Script    : ${LINKER_SCRIPT:-Not provided}"
echo "Report Timestamp : $(date)"
echo ""

# 1. Total Binary Size
echo -e "${YELLOW}[1] Overall File Size on Disk:${NC}"
ls -lh "$ELF_FILE" | awk '{print "    Total ELF Image: " $5}'
echo ""

# 2. Dynamic Python Linker Script & Memory Region Analyzer
echo -e "${YELLOW}[2] Linker Memory Region & Segment Breakdown:${NC}"
if [ -n "$LINKER_SCRIPT" ] && [ -f "$LINKER_SCRIPT" ]; then
    python3 - "$LINKER_SCRIPT" "$ELF_FILE" "$OBJDUMP_CMD" "$NM_CMD" "$TOP_SYMBOLS_PER_REGION" <<'EOPY'
import sys, re, subprocess
from pathlib import Path

def strip_comments(text: str) -> str:
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    text = re.sub(r'//.*', '', text)
    return text

def parse_size_expr(expr_str: str) -> int:
    expr_str = expr_str.strip().upper()
    def unit_replace(match):
        val = match.group(1)
        unit = match.group(2)
        multiplier = 1024 if unit.startswith('K') else (1024 * 1024)
        return f"({val} * {multiplier})"

    expr_clean = re.sub(r'([0-9a-fA-FxX]+)\s*(K|KB|M|MB)\b', unit_replace, expr_str)
    try:
        return int(eval(expr_clean, {"__builtins__": None}, {}))
    except Exception:
        m = re.search(r'0x[0-9A-F]+|\d+', expr_str)
        if m:
            val = m.group(0)
            return int(val, 16) if val.startswith(('0x', '0X')) else int(val)
        return 0

def load_linker_script(ld_path: Path) -> str:
    if not ld_path.is_file():
        return ""
    raw = ld_path.read_text(errors='ignore')
    raw = strip_comments(raw)
    def include_replacer(match):
        inc_name = match.group(1).strip('"\' ')
        inc_path = ld_path.parent / inc_name
        return load_linker_script(inc_path) if inc_path.is_file() else ""
    return re.sub(r'INCLUDE\s+([^\s;]+)', include_replacer, raw)

def parse_memory_regions(ld_content: str) -> dict:
    mem_match = re.search(r'MEMORY\s*\{([^}]+)\}', ld_content, re.DOTALL | re.IGNORECASE)
    if not mem_match:
        return {}
    regions = {}
    pattern = re.compile(
        r'(\w+)\s*(?:\([^)]*\))?\s*:\s*ORIGIN\s*=\s*([^,]+?)\s*,\s*LENGTH\s*=\s*([^,\n\r}]+)',
        re.IGNORECASE
    )
    for match in pattern.finditer(mem_match.group(1)):
        name = match.group(1).strip()
        orig = parse_size_expr(match.group(2).strip())
        length = parse_size_expr(match.group(3).strip())
        if length > 0:
            regions[name] = {
                'origin': orig,
                'length': length,
                'end': orig + length,
                'used': 0,
                'sections': [],
                'symbols': []
            }
    return regions

ld_file = Path(sys.argv[1])
elf_file = sys.argv[2]
objdump_cmd = sys.argv[3]
nm_cmd = sys.argv[4]
top_n = int(sys.argv[5])

ld_content = load_linker_script(ld_file)
regions = parse_memory_regions(ld_content)

if not regions:
    print("    [!] Could not parse MEMORY block from linker script.")
    sys.exit(0)

# Parse ALLOC Sections via objdump (filters out non-alloc DWARF debug and comment sections)
proc_objdump = subprocess.run([objdump_cmd, '-h', elf_file], capture_output=True, text=True, errors='ignore')
lines = proc_objdump.stdout.splitlines()
for i in range(len(lines)):
    line = lines[i]
    parts = line.split()
    if len(parts) >= 7 and parts[0].isdigit() and parts[1].startswith('.'):
        sec_name = parts[1]
        
        # Check if ALLOC flag is present on this line or the immediate attribute line below
        attr_line = lines[i+1] if (i + 1) < len(lines) else ""
        if "ALLOC" not in line and "ALLOC" not in attr_line:
            continue
        if sec_name.startswith(('.debug', '.comment', '.riscv.attributes')):
            continue

        try:
            sz = int(parts[2], 16)
            vma = int(parts[3], 16)
            lma = int(parts[4], 16)
        except ValueError:
            continue
        if sz == 0:
            continue
            
        for r_name, r in regions.items():
            if r['origin'] <= vma < r['end']:
                r['used'] += sz
                r['sections'].append({'name': sec_name, 'size': sz, 'vma': vma, 'lma': lma})
                break

# Parse Symbols via nm
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

# Print Overview Summary Table
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

# Print Detailed Section & Symbol Breakdown
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
EOPY
else
    echo -e "${YELLOW}[!] Linker script not provided or not found. Displaying raw section headers:${NC}"
    "$SIZE_CMD" -A -d "$ELF_FILE" | awk 'NR<=25 {print "    " $0}'
fi

# 3. Export Full Symbol CSV
echo -e "${YELLOW}[3] Exporting Full Symbol Table to CSV:${NC} $CSV_FILE"
echo "Address,Size,Type,Symbol" > "$CSV_FILE"
"$NM_CMD" --print-size --size-sort --radix=x -C "$ELF_FILE" 2>/dev/null | \
    awk '{addr=$1; size=$2; type=$3; $1=$2=$3=""; sub(/^[ \t]+/, ""); print addr","size","type",\""$0"\""}' >> "$CSV_FILE"

echo ""
echo -e "${GREEN}Memory analysis complete. Report written to: ${REPORT_FILE}${NC}"
