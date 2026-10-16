# Linux Physical Address & `devmem2` Map: `testFreeRTOS`

Generated automatically during build from `testFreeRTOS.elf`.

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
| **`global_resource_table`** | `0x3FFC684C` | `0x0728684C` | SRAM_A3 Space 0 (+0x684C) | `devmem2 0x0728684C w` |
| **`main`** | `0x3FFCFE20` | `0x0728FE20` | SRAM_A3 Space 0 (+0xFE20) | `devmem2 0x0728FE20 w` |
| **`g_trace_head`** | `0x3FFCFE48` | `0x0728FE48` | SRAM_A3 Space 0 (+0xFE48) | `devmem2 0x0728FE48 w` |
| **`_stack_bottom`** | `0x3FFCFE90` | `0x0728FE90` | SRAM_A3 Space 0 (+0xFE90) | `devmem2 0x0728FE90 w` |
| **`dtcm_scratch`** | `0x3FFD3E90` | `0x07293E90` | SRAM_A3 Space 0 (+0x13E90) | `devmem2 0x07293E90 w` |
| **`_start`** | `0x3FFD3E90` | `0x07293E90` | SRAM_A3 Space 0 (+0x13E90) | `devmem2 0x07293E90 w` |
| **`g_rproc_trace_buffer`** | `0x3FFD3E90` | `0x07293E90` | SRAM_A3 Space 0 (+0x13E90) | `devmem2 0x07293E90 w` |
| **`_stack_top`** | `0x3FFD3E90` | `0x07293E90` | SRAM_A3 Space 0 (+0x13E90) | `devmem2 0x07293E90 w` |
| **`__trace_start`** | `0x3FFD3E90` | `0x07293E90` | SRAM_A3 Space 0 (+0x13E90) | `devmem2 0x07293E90 w` |
| **`__sram_c_start`** | `0x3FFD4E90` | `0x07294E90` | SRAM_A3 Space 0 (+0x14E90) | `devmem2 0x07294E90 w` |
| **`__trace_end`** | `0x3FFD4E90` | `0x07294E90` | SRAM_A3 Space 0 (+0x14E90) | `devmem2 0x07294E90 w` |
| **`__sram_c_end`** | `0x3FFD5EA0` | `0x07295EA0` | SRAM_A3 Space 0 (+0x15EA0) | `devmem2 0x07295EA0 w` |

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
