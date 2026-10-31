# Linux Physical Address & `devmem2` Map: `testDRAMMsg`

Generated automatically during build from `testDRAMMsg.elf`.

This document maps XuanTie E906 RISC-V Core Device Addresses (`DA`) to **Linux Host (ARM64) Physical Addresses** for direct memory inspection in Linux process space via `devmem2` or `/dev/mem`.

> [!CAUTION]
> **NEVER USE `0x07200000` (CAUSES HARDWARE BUS ERROR)**
> In the actual Sun55i silicon bus architecture, `0x07200000` is unmapped and accessing it triggers an immediate hardware **Bus error** (external abort). The primary on-chip SRAM (`r_sram` / SRAM_A3 Space 0) is physically located at **`0x07280000`**.

## 1. Symbol Address Translation Table

| Symbol Name | E906 Core DA | Host Physical Address | Memory Region | Linux `devmem2` Command |
| :--- | :--- | :--- | :--- | :--- |
| **`default_trap_entry`** | `0x00040040` | N/A | Unmapped/Forbidden | N/A |
| **`_start`** | `0x000400F8` | N/A | Unmapped/Forbidden | N/A |
| **`main`** | `0x000409AE` | N/A | Unmapped/Forbidden | N/A |
| **`global_resource_table`** | `0x00042324` | N/A | Unmapped/Forbidden | N/A |
| **`g_trace_head`** | `0x00042368` | N/A | Unmapped/Forbidden | N/A |
| **`_stack_bottom`** | `0x00042370` | N/A | Unmapped/Forbidden | N/A |
| **`dtcm_scratch`** | `0x00046370` | N/A | Unmapped/Forbidden | N/A |
| **`g_rproc_trace_buffer`** | `0x00046370` | N/A | Unmapped/Forbidden | N/A |
| **`_stack_top`** | `0x00046370` | N/A | Unmapped/Forbidden | N/A |
| **`STA_ADD_REG   (CFG)`** | `0x07130204` | `0x07130204` | CFG Block (+0x204) | `devmem2 0x07130204 w` |
| **`WORK_MODE_REG (CFG)`** | `0x07130248` | `0x07130248` | CFG Block (+0x248) | `devmem2 0x07130248 w` |

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
