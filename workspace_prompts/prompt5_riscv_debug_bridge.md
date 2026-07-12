# Blueprint 5: Local OpenOCD & GDB Debugging Bridge via ARM MMIO

## 1. Mandated Rules
* **HARDWARE-LESS TRACING:** Implement debugging routines without relying on a physical hardware JTAG adapter probe or external lines.
* **CLEAN PERMISSION CONTROL:** Expose access parameters securely via U-Boot parameters using standard memory flag configurations.
* **CROSS-COMPILATION ALIGNMENT:** Maintain compilation scripts within out-of-tree Buildroot structures.

## 2. Context & Origins
* **Where this comes from:** This architecture leverages Allwinner's hardware routing design, which maps the XuanTie RISC-V Debug Module Interface (DMI) registers straight into the global system interconnect memory map. This design allows local utilities on the ARM host to trace real-time co-processor states directly over the system bus.

## 3. Engineering Goals
* Provide a production-ready out-of-tree Buildroot script that compiles OpenOCD with native memory-mapped (`sunxi_mmap`) I/O capability.
* Enable local GDB debugging connectivity into the XuanTie E906/E907 real-time core directly from the ARM terminal under Linux.

## 4. Implementation Phases
### Phase 1: Boot Unlocking Parameter Patch
* Add the `iomem=relaxed` system boot configuration to the default target environment. This ensures user-space utilities are granted unrestricted access to the SoC peripheral nodes via `/dev/mem`.

### Phase 2: OpenOCD Target Script Construction
- Construct an advanced OpenOCD hardware script (`openocd_t527_local.cfg`). Map the configuration to interface using the Allwinner physical memory base address (`0x07090000`).
* Incorporate exact register sequences to activate and toggle the `dmactive` bit within the standard RISC-V `dmcontrol` hardware space.

### Phase 3: Buildroot Package Configuration
* Draft an out-of-tree Buildroot recipe addition (`package/openocd/openocd.mk`) to compile OpenOCD with MMIO support flags enabled.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt5_debugger_trm_alignment.md`. This document must provide an educational map illustrating exactly how the physical RISC-V DMI registers map to the system bus, enabling anyone to reconstruct the debugging pipeline.
