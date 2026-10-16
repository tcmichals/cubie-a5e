# Linux Physical Address & `devmem2` Map: `testPingRpmsg`

Generated automatically during build from `testPingRpmsg.elf`.

This document maps XuanTie E906 RISC-V Core Device Addresses (`DA`) to **Linux Host (ARM64) Physical Addresses** for direct memory inspection in Linux process space via `devmem2` or `/dev/mem`.

> [!CAUTION]
> **NEVER USE `0x07200000` (CAUSES HARDWARE BUS ERROR)**
> In the actual Sun55i silicon bus architecture, `0x07200000` is unmapped and accessing it triggers an immediate hardware **Bus error** (external abort). The primary on-chip SRAM (`r_sram` / SRAM_A3 Space 0) is physically located at **`0x07280000`**.

## 1. Symbol Address Translation Table

| Symbol Name | E906 Core DA | Host Physical Address | Memory Region | Linux `devmem2` Command |
| :--- | :--- | :--- | :--- | :--- |
| **`STA_ADD_REG   (CFG)`** | `0x07130204` | `0x07130204` | CFG Block (+0x204) | `devmem2 0x07130204 w` |
| **`WORK_MODE_REG (CFG)`** | `0x07130248` | `0x07130248` | CFG Block (+0x248) | `devmem2 0x07130248 w` |
| **`default_trap_entry`** | `0x3FFC0040` | `0x07280040` | SRAM_A3 Space 0 (+0x40) | `devmem2 0x07280040 w` |
| **`main`** | `0x3FFC1452` | `0x07281452` | SRAM_A3 Space 0 (+0x1452) | `devmem2 0x07281452 w` |
| **`global_resource_table`** | `0x3FFC2DE8` | `0x07282DE8` | SRAM_A3 Space 0 (+0x2DE8) | `devmem2 0x07282DE8 w` |
| **`g_trace_head`** | `0x3FFC2F0C` | `0x07282F0C` | SRAM_A3 Space 0 (+0x2F0C) | `devmem2 0x07282F0C w` |
| **`_stack_bottom`** | `0x3FFC2F40` | `0x07282F40` | SRAM_A3 Space 0 (+0x2F40) | `devmem2 0x07282F40 w` |
| **`dtcm_scratch`** | `0x3FFC6F40` | `0x07286F40` | SRAM_A3 Space 0 (+0x6F40) | `devmem2 0x07286F40 w` |
| **`_start`** | `0x3FFC6F40` | `0x07286F40` | SRAM_A3 Space 0 (+0x6F40) | `devmem2 0x07286F40 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC6F40` | `0x07286F40` | SRAM_A3 Space 0 (+0x6F40) | `devmem2 0x07286F40 w` |
| **`_stack_top`** | `0x3FFC6F40` | `0x07286F40` | SRAM_A3 Space 0 (+0x6F40) | `devmem2 0x07286F40 w` |
| **`__trace_start`** | `0x3FFC6F40` | `0x07286F40` | SRAM_A3 Space 0 (+0x6F40) | `devmem2 0x07286F40 w` |
| **`__sram_c_end`** | `0x3FFC7F40` | `0x07287F40` | SRAM_A3 Space 0 (+0x7F40) | `devmem2 0x07287F40 w` |
| **`__sram_c_start`** | `0x3FFC7F40` | `0x07287F40` | SRAM_A3 Space 0 (+0x7F40) | `devmem2 0x07287F40 w` |
| **`__trace_end`** | `0x3FFC7F40` | `0x07287F40` | SRAM_A3 Space 0 (+0x7F40) | `devmem2 0x07287F40 w` |

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
