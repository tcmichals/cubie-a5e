# XuanTie E906/E907 Tightly-Coupled Memory (TCM) Map

This document charts the memory layout and physical address boundaries allocated to the XuanTie RISC-V co-processor on the Allwinner T527 / Radxa Cubie A5E flight controller.

---

## 1. Co-processor Tightly-Coupled Memory Map

The XuanTie core views memory through local TCM buses to ensure zero-latency execution without DDR bus contention. The memory maps are defined as follows:

| Region | Start Address | End Address | Size | Primary Purpose / Placement |
|---|---|---|---|---|
| **Instruction TCM (ITCM)** | `0x0000_0000` | `0x0000_FFFF` | 64 KB | Vector table, reset handlers, and time-critical SPI ISRs |
| **Data TCM (DTCM)** | `0x0008_0000` | `0x0008_FFFF` | 64 KB | Stacks, fast BSS/DATA variables, and DMA control rings |
| **SRAM C Pool** | `0x0002_8000` | `0x0007_7FFF` | 320 KB | Main firmware body, initialization routines, and large variables |
| **Shared SRAM Window** | `0x0007_8000` | `0x0007_FFFF` | 32 KB | Shared memory exchange buffer between ARM host and RISC-V |

---

## 2. Linker Segments Layout

To enforce this layout, the bare-metal linker script (`firmware.ld`) splits code and data compilation:

```text
       0x0000_0000 ┌──────────────────────────────────────┐
                   │    Vector Table & Boot Bootstrap     │ (Placed in ITCM)
                   ├──────────────────────────────────────┤
                   │       Fast SPI ISR Functions         │
       0x0000_FFFF └──────────────────────────────────────┘
                   
       0x0002_8000 ┌──────────────────────────────────────┐
                   │      Main Text (firmware.c logic)     │ (Placed in SRAM C)
                   ├──────────────────────────────────────┤
                   │          Read-Only Constants         │
       0x0007_7FFF └──────────────────────────────────────┘
                   
       0x0008_0000 ┌──────────────────────────────────────┐
                   │       Initialized Data (.data)       │ (Placed in DTCM)
                   ├──────────────────────────────────────┤
                   │        Uninitialized BSS (.bss)      │
                   ├──────────────────────────────────────┤
                   │         Stack & Heap Boundaries      │
       0x0008_FFFF └──────────────────────────────────────┘
```

---

## 3. Clock & Reset Management Registers

Clock control and boot control for the XuanTie core is routed through the System Control MCU CCU blocks:

* **DSP CCU Register (`CCU_DSP_CLK_REG`):** `0x07010000` + `0x0020` (DSP Core Clock Configuration).
* **Reset Control Register (`RST_BUS_MCU_DSP`):** `0x07010000` + `0x0100` (Bit 17 asserts/de-asserts the core system reset).

---

## 4. SoC Security Peripherals Controller (SPC) Interrupt Routing

To route peripheral interrupts (such as the Dual SPI controller interrupts) to the XuanTie RISC-V co-processor's PLIC instead of the ARM host GIC:

1. **SPC Base Address:** `0x03008000` (Security Peripherals Controller).
2. **Peripheral Permission Registers (`SPC_DECPORT_REG`):**
   - Each hardware peripheral has a target registration offset (e.g., `SPI0` is at port index 14, `SPI1` at port index 15).
   - Setting the target peripheral port configuration field to `0x3` routes its register access space and hardware interrupt lines exclusively to the co-processor domain (DSP/MCU).
3. **PLIC registers (Platform-Level Interrupt Controller):**
   - The RISC-V PLIC register block resides in the MCU register block (starting at base `0x07020000`).
   - Standard interrupt enable registers (`PLIC_INT_ENABLE_REG`) and priority registers (`PLIC_PRIORITY_REG`) are programmed directly by the RISC-V bare-metal firmware during initialization to register active ISR handlers.

