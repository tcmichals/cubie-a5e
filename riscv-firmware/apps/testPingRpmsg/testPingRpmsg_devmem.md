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
| **`default_trap_entry`** | `0x3FFC0040` | `0x07280040` | SRAM_A3 Space 0 (+0x40) | `devmem2 0x07280040 w` |
| **`main`** | `0x3FFC14F4` | `0x072814F4` | SRAM_A3 Space 0 (+0x14F4) | `devmem2 0x072814F4 w` |
| **`global_resource_table`** | `0x3FFC44B8` | `0x072844B8` | SRAM_A3 Space 0 (+0x44B8) | `devmem2 0x072844B8 w` |
| **`g_trace_head`** | `0x3FFC45DC` | `0x072845DC` | SRAM_A3 Space 0 (+0x45DC) | `devmem2 0x072845DC w` |
| **`_stack_bottom`** | `0x3FFC4600` | `0x07284600` | SRAM_A3 Space 0 (+0x4600) | `devmem2 0x07284600 w` |
| **`dtcm_scratch`** | `0x3FFC8600` | `0x07288600` | SRAM_A3 Space 0 (+0x8600) | `devmem2 0x07288600 w` |
| **`_start`** | `0x3FFC8600` | `0x07288600` | SRAM_A3 Space 0 (+0x8600) | `devmem2 0x07288600 w` |
| **`g_rproc_trace_buffer`** | `0x3FFC8600` | `0x07288600` | SRAM_A3 Space 0 (+0x8600) | `devmem2 0x07288600 w` |
| **`_stack_top`** | `0x3FFC8600` | `0x07288600` | SRAM_A3 Space 0 (+0x8600) | `devmem2 0x07288600 w` |
| **`__trace_start`** | `0x3FFC8600` | `0x07288600` | SRAM_A3 Space 0 (+0x8600) | `devmem2 0x07288600 w` |
| **`__sram_c_end`** | `0x3FFC9600` | `0x07289600` | SRAM_A3 Space 0 (+0x9600) | `devmem2 0x07289600 w` |
| **`__sram_c_start`** | `0x3FFC9600` | `0x07289600` | SRAM_A3 Space 0 (+0x9600) | `devmem2 0x07289600 w` |
| **`__trace_end`** | `0x3FFC9600` | `0x07289600` | SRAM_A3 Space 0 (+0x9600) | `devmem2 0x07289600 w` |

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
