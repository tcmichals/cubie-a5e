# Blueprint 3: XuanTie RISC-V Core Bare-Metal Ring-Buffer & FPGA Dual SPI Loop

## 1. Mandated Rules
* **STRICTLY ISOLATED:** The real-time firmware must run completely decoupled from host Linux memory space.
* **ZERO DDR BUS CONTENTION:** Force all execution parameters inside internal tightly-coupled hardware memory blocks.
* **ALLOCATION FREE:** Absolutely no runtime heap allocations; use rigid, fixed structures.

## 2. Context & Origins
* **Where this comes from:** Low-level peripheral initialization configurations, power configurations, and clock gating routines are reverse-engineered directly from Allwinner’s official `sunxi-melis` SDK examples written for the XuanTie E906/E907 real-time processor complex.

## 3. Engineering Goals
* Establish microsecond-level deterministic ingestion firmware executing inside the auxiliary real-time core.
* Maintain full-duplex Dual SPI loops capturing sensor frames from an external FPGA fabric.

## 4. Implementation Phases
### Phase 1: Interrupt Steering Validation
* Write initialization values into the SoC Security Peripherals Controller (SPC) and CCU block matrices to route Dual SPI hardware interrupts cleanly into the RISC-V PLIC instead of the ARM host GIC.

### Phase 2: TCM Linker Layout Configuration
* Draft a strict, rigid linker script (`.ld`) that forces your high-priority SPI ISR functions directly into the Instruction TCM (ITCM) and builds the circular message buffers inside Data TCM (DTCM) or reclaimed SRAM C (320KB block pool).

### Phase 3: Ingestion Engine Development
* Construct an optimized pointer-exchange data ring handling 32-byte or 64-byte blocks to manage full-duplex SPI payloads without processing stalls.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt3_riscv_tcm_map.md`. This log must chart the precise memory boundaries of the ITCM, DTCM, and SRAM C layers, mapping how the `sunxi-melis` SDK register steps were implemented.
