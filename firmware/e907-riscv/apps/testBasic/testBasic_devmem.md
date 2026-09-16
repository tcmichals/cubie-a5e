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
| **`default_trap_entry`** | `0x3FFC0040` | `0x07280040` | SRAM_A3 Space 0 (+0x40) | `devmem2 0x07280040 w` |
| **`main`** | `0x3FFC097C` | `0x0728097C` | SRAM_A3 Space 0 (+0x97C) | `devmem2 0x0728097C w` |
| **`global_resource_table`** | `0x3FFC3F50` | `0x07283F50` | SRAM_A3 Space 0 (+0x3F50) | `devmem2 0x07283F50 w` |
| **`g_trace_head`** | `0x3FFC3F94` | `0x07283F94` | SRAM_A3 Space 0 (+0x3F94) | `devmem2 0x07283F94 w` |
| **`_stack_bottom`** | `0x3FFC3FA0` | `0x07283FA0` | SRAM_A3 Space 0 (+0x3FA0) | `devmem2 0x07283FA0 w` |
| **`dtcm_scratch`** | `0x3FFC7FA0` | `0x07287FA0` | SRAM_A3 Space 0 (+0x7FA0) | `devmem2 0x07287FA0 w` |
| **`_stack_top`** | `0x3FFC7FA0` | `0x07287FA0` | SRAM_A3 Space 0 (+0x7FA0) | `devmem2 0x07287FA0 w` |
| **`_start`** | `0x3FFC7FA8` | `0x07287FA8` | SRAM_A3 Space 0 (+0x7FA8) | `devmem2 0x07287FA8 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC7FA8` | `0x07287FA8` | SRAM_A3 Space 0 (+0x7FA8) | `devmem2 0x07287FA8 w` |
| **`__trace_start`** | `0x3FFC7FA8` | `0x07287FA8` | SRAM_A3 Space 0 (+0x7FA8) | `devmem2 0x07287FA8 w` |
| **`__sram_c_start`** | `0x3FFC8FA8` | `0x07288FA8` | SRAM_A3 Space 0 (+0x8FA8) | `devmem2 0x07288FA8 w` |
| **`__trace_end`** | `0x3FFC8FA8` | `0x07288FA8` | SRAM_A3 Space 0 (+0x8FA8) | `devmem2 0x07288FA8 w` |
| **`sram_c_loc1`** | `0x3FFC8FA8` | `0x07288FA8` | SRAM_A3 Space 0 (+0x8FA8) | `devmem2 0x07288FA8 w` |
| **`sram_c_loc2`** | `0x3FFC8FB0` | `0x07288FB0` | SRAM_A3 Space 0 (+0x8FB0) | `devmem2 0x07288FB0 w` |
| **`__sram_c_end`** | `0x3FFC8FB8` | `0x07288FB8` | SRAM_A3 Space 0 (+0x8FB8) | `devmem2 0x07288FB8 w` |

---

## 2. Inspecting in Linux Process Space

### Option A: Shell Inspection via `devmem2`
```bash
# Read sram_c_loc1 magic (0xDEADBEEF) and counter:
devmem2 0x07288FA8 w 2

# Read sram_c_loc2 magic ('RISC' / 0x52495343) and counter:
devmem2 0x07288FB0 w 2

# Check RISC-V Hardware Lockup / Run Status:
devmem2 0x07130248 w
```

### Option B: Python Direct Process Address Space Mapping
```python
import mmap, struct

# Open physical memory and map SRAM Space 0 page
with open('/dev/mem', 'r+b') as f:
    # Page Base: 0x07288000, Offset: 0xFA8 (sram_c_loc1)
    mm = mmap.mmap(f.fileno(), 0x1000, offset=0x07288000)
    magic, count = struct.unpack_from('<II', mm, 0xFA8)
    print(f'sram_c_loc1: Magic=0x{magic:08X}, Counter={count}')
```
