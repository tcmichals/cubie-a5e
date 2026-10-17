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
| **`global_resource_table`** | `0x3FFC6520` | `0x07286520` | SRAM_A3 Space 0 (+0x6520) | `devmem2 0x07286520 w` |
| **`main`** | `0x3FFCFA90` | `0x0728FA90` | SRAM_A3 Space 0 (+0xFA90) | `devmem2 0x0728FA90 w` |
| **`g_trace_head`** | `0x3FFCFAB8` | `0x0728FAB8` | SRAM_A3 Space 0 (+0xFAB8) | `devmem2 0x0728FAB8 w` |
| **`_stack_bottom`** | `0x3FFCFB00` | `0x0728FB00` | SRAM_A3 Space 0 (+0xFB00) | `devmem2 0x0728FB00 w` |
| **`dtcm_scratch`** | `0x3FFD3B00` | `0x07293B00` | SRAM_A3 Space 0 (+0x13B00) | `devmem2 0x07293B00 w` |
| **`_start`** | `0x3FFD3B00` | `0x07293B00` | SRAM_A3 Space 0 (+0x13B00) | `devmem2 0x07293B00 w` |
| **`g_rproc_trace_buffer`** | `0x3FFD3B00` | `0x07293B00` | SRAM_A3 Space 0 (+0x13B00) | `devmem2 0x07293B00 w` |
| **`_stack_top`** | `0x3FFD3B00` | `0x07293B00` | SRAM_A3 Space 0 (+0x13B00) | `devmem2 0x07293B00 w` |
| **`__trace_start`** | `0x3FFD3B00` | `0x07293B00` | SRAM_A3 Space 0 (+0x13B00) | `devmem2 0x07293B00 w` |
| **`__sram_c_start`** | `0x3FFD4B00` | `0x07294B00` | SRAM_A3 Space 0 (+0x14B00) | `devmem2 0x07294B00 w` |
| **`__trace_end`** | `0x3FFD4B00` | `0x07294B00` | SRAM_A3 Space 0 (+0x14B00) | `devmem2 0x07294B00 w` |
| **`__sram_c_end`** | `0x3FFD5B10` | `0x07295B10` | SRAM_A3 Space 0 (+0x15B10) | `devmem2 0x07295B10 w` |

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
