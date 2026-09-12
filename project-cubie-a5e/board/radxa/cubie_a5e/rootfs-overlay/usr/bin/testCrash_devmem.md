# Linux Physical Address & `devmem2` Map: `testCrash`

Generated automatically during build from `testCrash.elf`.

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
| **`global_resource_table`** | `0x3FFC10BC` | `0x072810BC` | SRAM_A3 Space 0 (+0x10BC) | `devmem2 0x072810BC w` |
| **`g_trace_head`** | `0x3FFC1100` | `0x07281100` | SRAM_A3 Space 0 (+0x1100) | `devmem2 0x07281100 w` |
| **`_stack_bottom`** | `0x3FFC1110` | `0x07281110` | SRAM_A3 Space 0 (+0x1110) | `devmem2 0x07281110 w` |
| **`dtcm_scratch`** | `0x3FFC5110` | `0x07285110` | SRAM_A3 Space 0 (+0x5110) | `devmem2 0x07285110 w` |
| **`_start`** | `0x3FFC5110` | `0x07285110` | SRAM_A3 Space 0 (+0x5110) | `devmem2 0x07285110 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC5110` | `0x07285110` | SRAM_A3 Space 0 (+0x5110) | `devmem2 0x07285110 w` |
| **`_stack_top`** | `0x3FFC5110` | `0x07285110` | SRAM_A3 Space 0 (+0x5110) | `devmem2 0x07285110 w` |
| **`__trace_start`** | `0x3FFC5110` | `0x07285110` | SRAM_A3 Space 0 (+0x5110) | `devmem2 0x07285110 w` |
| **`__sram_c_start`** | `0x3FFC6110` | `0x07286110` | SRAM_A3 Space 0 (+0x6110) | `devmem2 0x07286110 w` |
| **`__trace_end`** | `0x3FFC6110` | `0x07286110` | SRAM_A3 Space 0 (+0x6110) | `devmem2 0x07286110 w` |
| **`__sram_c_end`** | `0x3FFC6118` | `0x07286118` | SRAM_A3 Space 0 (+0x6118) | `devmem2 0x07286118 w` |

---

## 2. Inspecting in Linux Process Space

### Option A: Shell Inspection via `devmem2`
```bash
# Check RISC-V Hardware Lockup / Run Status:
devmem2 0x07130248 w
```

### Option B: Python Direct Process Address Space Mapping
```python
import mmap, struct

# Open physical memory and map SRAM Space 0 page
with open('/dev/mem', 'r+b') as f:
```
