# Linux Physical Address & `devmem2` Map: `testPingRpmsg`

Generated automatically during build from `testPingRpmsg.elf`.

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
| **`main`** | `0x3FFC0E38` | `0x07280E38` | SRAM_A3 Space 0 (+0xE38) | `devmem2 0x07280E38 w` |
| **`global_resource_table`** | `0x3FFC18EC` | `0x072818EC` | SRAM_A3 Space 0 (+0x18EC) | `devmem2 0x072818EC w` |
| **`g_trace_head`** | `0x3FFC1A10` | `0x07281A10` | SRAM_A3 Space 0 (+0x1A10) | `devmem2 0x07281A10 w` |
| **`_stack_bottom`** | `0x3FFC1A30` | `0x07281A30` | SRAM_A3 Space 0 (+0x1A30) | `devmem2 0x07281A30 w` |
| **`dtcm_scratch`** | `0x3FFC5A30` | `0x07285A30` | SRAM_A3 Space 0 (+0x5A30) | `devmem2 0x07285A30 w` |
| **`_start`** | `0x3FFC5A30` | `0x07285A30` | SRAM_A3 Space 0 (+0x5A30) | `devmem2 0x07285A30 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC5A30` | `0x07285A30` | SRAM_A3 Space 0 (+0x5A30) | `devmem2 0x07285A30 w` |
| **`_stack_top`** | `0x3FFC5A30` | `0x07285A30` | SRAM_A3 Space 0 (+0x5A30) | `devmem2 0x07285A30 w` |
| **`__trace_start`** | `0x3FFC5A30` | `0x07285A30` | SRAM_A3 Space 0 (+0x5A30) | `devmem2 0x07285A30 w` |
| **`__sram_c_end`** | `0x3FFC6A30` | `0x07286A30` | SRAM_A3 Space 0 (+0x6A30) | `devmem2 0x07286A30 w` |
| **`__sram_c_start`** | `0x3FFC6A30` | `0x07286A30` | SRAM_A3 Space 0 (+0x6A30) | `devmem2 0x07286A30 w` |
| **`__trace_end`** | `0x3FFC6A30` | `0x07286A30` | SRAM_A3 Space 0 (+0x6A30) | `devmem2 0x07286A30 w` |

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
