# Linux Physical Address & `devmem2` Map: `testBasic`

Generated automatically during build from `testBasic.elf`.

This document maps XuanTie E907 RISC-V Core Device Addresses (`DA`) to **Linux Host (ARM64) Physical Addresses** for direct memory inspection in Linux process space via `devmem2` or `/dev/mem`.

> [!CAUTION]
> **NEVER USE `0x07200000` (CAUSES HARDWARE BUS ERROR)**
> In the actual Sun55i silicon bus architecture, `0x07200000` is unmapped and accessing it triggers an immediate hardware **Bus error** (external abort). The primary on-chip SRAM (`r_sram` / SRAM_A3 Space 0) is physically located at **`0x07280000`**.

## 1. Symbol Address Translation Table

| Symbol Name | E907 Core DA | Host Physical Address | Memory Region | Linux `devmem2` Command |
| :--- | :--- | :--- | :--- | :--- |
| **`STA_ADD_REG   (CFG)`** | `0x07130204` | `0x07130204` | CFG Block (+0x204) | `devmem2 0x07130204 w` |
| **`WORK_MODE_REG (CFG)`** | `0x07130248` | `0x07130248` | CFG Block (+0x248) | `devmem2 0x07130248 w` |
| **`default_trap_entry`** | `0x3FFC0010` | `0x07280010` | SRAM_A3 Space 0 (+0x10) | `devmem2 0x07280010 w` |
| **`main`** | `0x3FFC0794` | `0x07280794` | SRAM_A3 Space 0 (+0x794) | `devmem2 0x07280794 w` |
| **`global_resource_table`** | `0x3FFC1A30` | `0x07281A30` | SRAM_A3 Space 0 (+0x1A30) | `devmem2 0x07281A30 w` |
| **`g_trace_head`** | `0x3FFC1A74` | `0x07281A74` | SRAM_A3 Space 0 (+0x1A74) | `devmem2 0x07281A74 w` |
| **`_stack_bottom`** | `0x3FFC1A80` | `0x07281A80` | SRAM_A3 Space 0 (+0x1A80) | `devmem2 0x07281A80 w` |
| **`dtcm_scratch`** | `0x3FFC5A80` | `0x07285A80` | SRAM_A3 Space 0 (+0x5A80) | `devmem2 0x07285A80 w` |
| **`_stack_top`** | `0x3FFC5A80` | `0x07285A80` | SRAM_A3 Space 0 (+0x5A80) | `devmem2 0x07285A80 w` |
| **`_start`** | `0x3FFC5A88` | `0x07285A88` | SRAM_A3 Space 0 (+0x5A88) | `devmem2 0x07285A88 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC5A88` | `0x07285A88` | SRAM_A3 Space 0 (+0x5A88) | `devmem2 0x07285A88 w` |
| **`__trace_start`** | `0x3FFC5A88` | `0x07285A88` | SRAM_A3 Space 0 (+0x5A88) | `devmem2 0x07285A88 w` |
| **`__sram_c_start`** | `0x3FFC6A88` | `0x07286A88` | SRAM_A3 Space 0 (+0x6A88) | `devmem2 0x07286A88 w` |
| **`__trace_end`** | `0x3FFC6A88` | `0x07286A88` | SRAM_A3 Space 0 (+0x6A88) | `devmem2 0x07286A88 w` |
| **`sram_c_loc1`** | `0x3FFC6A88` | `0x07286A88` | SRAM_A3 Space 0 (+0x6A88) | `devmem2 0x07286A88 w` |
| **`sram_c_loc2`** | `0x3FFC6A90` | `0x07286A90` | SRAM_A3 Space 0 (+0x6A90) | `devmem2 0x07286A90 w` |
| **`__sram_c_end`** | `0x3FFC6A98` | `0x07286A98` | SRAM_A3 Space 0 (+0x6A98) | `devmem2 0x07286A98 w` |

---

## 2. Inspecting in Linux Process Space

### Option A: Shell Inspection via `devmem2`
```bash
# Read sram_c_loc1 magic (0xDEADBEEF) and counter:
devmem2 0x07286A88 w 2

# Read sram_c_loc2 magic ('RISC' / 0x52495343) and counter:
devmem2 0x07286A90 w 2

# Check RISC-V Hardware Lockup / Run Status:
devmem2 0x07130248 w
```

### Option B: Python Direct Process Address Space Mapping
```python
import mmap, struct

# Open physical memory and map SRAM Space 0 page
with open('/dev/mem', 'r+b') as f:
    # Page Base: 0x07286000, Offset: 0xA88 (sram_c_loc1)
    mm = mmap.mmap(f.fileno(), 0x1000, offset=0x07286000)
    magic, count = struct.unpack_from('<II', mm, 0xA88)
    print(f'sram_c_loc1: Magic=0x{magic:08X}, Counter={count}')
```
