# Allwinner A733 (sun60iw2) XuanTie E902 Boot & Coprocessor Architecture

**Document Version:** 2.1  
**Date:** November 9, 2026 (2.0: September 6, 2026)  
**Target SoCs / Boards:** Allwinner A733 / sun60iw2 (Radxa Cubie A7A, Banana Pi A733, Radxa Cubie A7Z)  
**Author / Integration:** Embedded Real-Time & Heterogeneous Compute Team  

---

## 1. Executive Summary

This document provides the complete hardware, security, and boot architecture for the embedded **T-Head XuanTie E902 32-bit RISC-V core** on the Allwinner A733 (sun60iw2) SoC. It specifies how to target the platform for two distinct operational models:

1. **Mode 1: Standard Power Management (Suspend/Resume / `scp.fex`)**
   - Traditional consumer battery/tablet profile requiring S3 deep sleep (Suspend-to-RAM).
   - E902 runs Allwinner's `scp.fex` firmware loaded at boot time by `boot0` into DRAM at `0x40014000`.
2. **Mode 2: Real-Time Embedded Control (Linux `remoteproc`)**
   - Industrial automation, robotics, deterministic I/O, and real-time control where suspend/resume is disabled.
   - E902 is repurposed as an on-chip real-time coprocessor managed dynamically by the Linux `remoteproc` framework (`sunxi_rproc.c`) or loaded via custom boot firmware.

The two modes are exclusive. In the vendor firmware the E902 belongs to the secure world: BL31 starts the vendor SCP firmware ("arisc") on it and uses it for power management, which is why BL31 keeps the E902 configuration block secure (§3.A). Mode 2 has to undo **both** halves: stop BL31 from starting the arisc, and open the configuration block to Linux.

**How Mode 2 is built today (Cubie A7A Buildroot image):** we do not rebuild TF-A or U-Boot. `project-cubie-a5e/board/radxa/cubie_a7a/post-image.sh` takes the stock Radxa `radxa_a733_bootloader.bin` and applies two binary edits (§6):

| Edit | What it does | Where | Status (2026-11-09) |
| :--- | :--- | :--- | :--- |
| Drop the SCP from the boot package | Boot-package FIT at sector 848: `loadables = "scp", "uboot";` → `loadables = "uboot";`, so the arisc is never started | added in commit `cf724ae` | **Lost**: commit `4a276a2` (2026-09-04) deleted the step. Restore: TODO.md Part II §5, SCP-1 |
| Open R_SPC port 3 | Trampoline in BL31 `sunxi_security_setup` adds the port 3 write (`0x07002034`) | `patch_bl31_security.py`, commit `99fc419` | Applied |

Until SCP-1 is fixed, images built since 2026-09-04 start the arisc **and** give Linux the E902: the core has two owners.

---

## 2. Silicon Topology & Domain Architecture

Unlike the Allwinner T527 / A523 which features a XuanTie E906 core with a single-precision FPU (no TCMs; it runs from SRAM A3 and DRAM) inside an open MCU domain (`0x07100000+`), the **A733 integrates a XuanTie E902 core inside the CPUS / Always-On (`R_`) power management subsystem**:

| Silicon Feature | Allwinner T527 / A523 (Cubie A5E) | Allwinner A733 (Cubie A7A & Banana Pi A733) |
| :--- | :--- | :--- |
| **RISC-V Core IP** | XuanTie E906 (RV32IMAFCX, single-precision FPU) | **XuanTie E902 (RV32EMC, 16 GPRs, Integer only)** |
| **Subsystem Domain** | MCU / DSP Domain (`0x07100000+`) | **CPUS / Always-On (`R_`) Domain (`0x07000000+`)** |
| **Max Clock Rate** | 200 MHz | 200 MHz |
| **Registers** | 32 General Purpose Registers (`x0`–`x31`) | **16 General Purpose Registers (`x0`–`x15`)** |
| **Tightly Coupled Memory** | **NO TCMs** (SRAM Space 0 DA `0x3FFC0000`, Space 1 DA `0x40000000`, 256 KB each) | **NO TCMs** (Executes out of SRAM A2 or DRAM) |
| **On-Chip SRAM** | 256 KB Dedicated MCU SRAM (`0x07280000`) | **208 KB System SRAM A2 (`0x00040000`–`0x00073FFF`)** |
| **CFG Register Base** | `0x07130000` (`MCU_CFG`) | **`0x07032000` (`E902_CFG`)** |
| **Start Vector Register**| `0x07130204` (`STA_ADD`) | **`0x07032204` (`STA_ADD`)** |
| **TrustZone Firewall** | **Open Non-Secure** (Directly writable in EL1) | **Secure-only by design** in vendor BL31: the E902 runs the vendor SCP (arisc) power-management firmware (see §3.A) |

---

## 3. TrustZone Security Boundary & Hardware Readback

### A. The Start Address Register (`0x07032204`)
The start address register `0x07032204` defines the instruction fetch entry point of the E902 upon reset:
* **Hardware Readback**: In stock bootloader configurations, reading `0x07032204` returns **`0x40014000`**.
* **DRAM Offset**: In Allwinner SPL architecture (`sun60iw2p1.h`), `SCP_CODE_DRAM_OFFSET` is `0x14000`. With DRAM mapped at `0x40000000`, this equals `0x40014000`.
* **Default BL31 Lockout (intentional)**: In vendor ARM Trusted Firmware (BL31), `0x07032000` is mapped in the Secure World. Writes from Non-Secure Linux EL1 are filtered by the interconnect firewall unless BL31 is unlocked (see §5).
* **Why it is locked**: in the vendor design the E902 is not a free coprocessor. It is the power-management core (Allwinner "arisc"/SCP). BL31 itself loads `scp.fex` into the E902, releases its reset and talks to it over the hardware msgbox, and uses it for idle/suspend clock and power gating and PMIC control. The stock Radxa `bl31.bin` (`device-a733/bin/bl31.bin` in the vendor BSP) contains the strings `[SCP] :load arisc image finish`, `[SCP] :set arisc reset to de-assert state`, `[SCP] :release arisc reset finish`, `[SCP] :hwmsgbox initialize` and `[SCP] :wait arisc ready....`, and `device-a733/configs/default/boot_package.cfg` packs `item=scp, scp.fex`. Leaving R_SPC port 3 secure keeps Linux from touching the core the secure world owns. It is a design choice, not an omission (corrected 2026-11-09; earlier text called it "missing").
* **Consequence**: opening port 3 alone gives the E902 two owners. If BL31 still loads `scp.fex`, Linux remoteproc stops and reloads the core the secure world depends on for suspend/idle/power-off. Mode 2 therefore needs **both** the port opened **and** the SCP removed from the boot package (and BL31 not starting arisc), not the port patch alone.

---

## 4. Dual-Mode Architecture: Suspend/Resume vs. Real-Time Control

```
+───────────────────────────────────────────────────────────────────────────────────────────────────+
| ALLWINNER A733 DUAL-MODE SELECTION BLUEPRINT                                                      |
+───────────────────────────────────────────────────────────────────────────────────────────────────+

                                     [ Power-On / boot0 (SPL) ]
                                                 │
                                                 ▼
                                     [ U-Boot / TOC1 Package ]
                                                 │
                   ┌─────────────────────────────┴─────────────────────────────┐
                   ▼                                                           ▼
       [ MODE 1: Suspend / Resume ]                                [ MODE 2: Real-Time RemoteProc ]
  ┌─────────────────────────────────────┐                     ┌─────────────────────────────────────┐
  │ • TOC1 contains scp.fex             │                     │ • FIT loadables drop "scp"          │
  │ • Loads to 0x40014000 at power-on   │                     │ • E902 held in reset at boot        │
  │ • E902 manages AXP8191 PMIC (s_twi0)│                     │ • U-Boot/Linux controls AXP8191 PMIC│
  │ • BL31 installs SCPI PSCI handlers  │                     │ • BL31 patched: R_SPC port 3 to NS  │
  │ • Linux reserves 0x40014000 (no-map)│                     │ • Linux sunxi_rproc loads firmware  │
  │ • remoteproc node disabled in DTS   │                     │ • remoteproc node enabled in DTS    │
  └─────────────────────────────────────┘                     └─────────────────────────────────────┘
```

---

## 4.1 A733 U-Boot Power FEX (`sys_config.fex` / `power.fex`) Architecture & Compilation

The Allwinner A733 hardware power tree and regulator voltage tables are defined using Allwinner's **FEX configuration format** and compiled directly into the binary boot structures during the U-Boot packaging phase.

### A. Power FEX Configuration Schema (`sys_config.fex`)

The power configuration defines the initial boot voltages, PMIC bus bindings, and power domain assignments:

```ini
[power_sply]
dcdc1_vol       = 3300000   ; 3.3V - USB 2.0 Hub (FE1.1S), AIC8800 Wi-Fi 6, Motorcomm GbE PHY
dcdc2_vol       = 900000    ; 0.9V - ARM Cortex-A76 Big Cores VDD-CPU
dcdc3_vol       = 900000    ; 0.9V - ARM Cortex-A55 Little Cores VDD-CPU
dcdc4_vol       = 1100000   ; 1.1V - LPDDR5 VDD2 DRAM Core Power
aldo1_vol       = 1800000   ; 1.8V - VCC-PL / VCC-PM PRCM IO Banks & Analog PLL
aldo2_vol       = 1800000   ; 1.8V - MIPI-CSI / MIPI-DSI Analog Power
aldo3_vol       = 3300000   ; 3.3V - GPIO Port H / Port B IO Voltage (VCC-IO)
bldo1_vol       = 1800000   ; 1.8V - LPDDR5 VDDQ / PLL Reference
cldo1_vol       = 3300000   ; 3.3V - MicroSD Card VCC-SD (`PF0`-`PF5`)

[pmu1_para]
pmu_used        = 1
pmu_twi_addr    = 0x36      ; Primary PMIC (AXP8191) Address on s_twi0 (0x34 for AXP515)
pmu_twi_id      = 0         ; Bound to s_twi0 / R_TWI0 (0x07083000, pins PL0/PL1)
pmu_irq_id      = 203       ; GIC SPI 203 / R_PIO Interrupt
pmu_battery_rdc = 100
pmu_battery_cap = 0
pmu_bat_unused  = 1
pmu_power_key   = 1
pmu_reset_key   = 1
```

### B. Compilation & Packaging Pipeline (`dragonsecboot`)

During the U-Boot build, the text FEX files are compiled into binary parameter blocks and packaged into the TOC1 container:

1. **FEX Compiler (`fexc` / `script.bin` generator)**:
   - Converts `sys_config.fex` into binary struct format with 32-bit little-endian fields and CRC checks.
2. **TOC1 Manifest Integration (`boot_package.cfg`)**:
   ```ini
   [package]
   item=u-boot,          u-boot.bin,          0x4a000000
   item=monitor,         bl31.bin,            0x48000000
   item=scp,             scp.fex,             0x40014000
   item=dtb,             sun60i-a733-a7a.dtb, 0x4fa00000
   item=power_cfg,       power.fex,           0x40020000
   ```
3. **Packaging (`dragonsecboot`)**:
   - Compiles the components into `boot_package.fex` and writes it to **Sector 24576 (12.0 MB offset)** on the boot media.

### C. Boot-Time Hardware Rail Sequencing

```
┌────────────────────────────────────────────────────────────────────────┐
│ 1. Vendor boot0 (SRAM @ 0x47000)                                       │
│    • Reads power.fex binary block from boot_package.fex (Sector 24576) │
│    • Configures AXP8191 DCDC2/3 (0.9V VDD-CPU) & DCDC4 (1.1V LPDDR5)   │
│    • Calibrates LPDDR5 PHY at target voltage                           │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ 2. scp.fex Execution (XuanTie E902 @ 0x40014000)                       │
│    • Reads [power_sply] tables from DRAM                               │
│    • Initializes I2C Bus (s_twi0 / R_TWI0) at 0x07083000                │
│    • Programs AXP8191 DCDC1 = 3.3V (Enables FE1.1S Hub & AIC8800 Wi-Fi)│
│    • Programs ALDO1/3 & CLDO1 for system I/O buses                     │
│    • Enters SCPI command listener loop for TF-A BL31 DVFS calls        │
└──────────────────────────────────┬─────────────────────────────────────┘
                                   │
                                   ▼
┌────────────────────────────────────────────────────────────────────────┐
│ 3. Mainline U-Boot Proper (ARM EL2 @ 0x4A000000)                       │
│    • Queries PMIC state and verifies voltage rail stability            │
│    • Boots Linux kernel 7.1 PREEMPT_RT with active DTB regulator trees │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 5. E902 as a Dedicated I/O Processor (Offloading Linux)

In Mode 2, the XuanTie E902 is repurposed as a **high-speed I/O Front-End and Hardware Serializer**. Instead of burdening the Linux ARM host with thousands of individual hardware interrupts per second, the E902 handles all time-critical peripheral transactions directly:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        XUANTIE E902 I/O FRONT-END PROCESSOR                            │
├─────────────────────────┬────────────────────────────┬─────────────────────────────────┤
│    PERIPHERAL BUS       │      HARDWARE TARGET       │        REAL-TIME TASK           │
├─────────────────────────┼────────────────────────────┼─────────────────────────────────┤
│ • High-Speed SPI        │ IMU (ICM-42688 / BMI270)   │ 1 kHz - 8 kHz burst sampling   │
│ • PIO Edge Interrupts   │ IMU DRDY, Encoders, PPS    │ Microsecond-accurate ISR capture│
│ • I2C (TWI Bus)         │ Barometer, Compass         │ Background telemetry polling   │
│ • Serial UART           │ GPS (NMEA / UBX Protocol)  │ Hardware FIFO drain & parsing   │
└─────────────────────────┴─────────────┬──────────────┴─────────────────────────────────┘
                                        │
                                        │  Batched binary frames + 64-bit timestamps
                                        ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│               LOCK-FREE SPSC RINGBUFFER IN SYSTEM SRAM A2 (0x00040000)                 │
└───────────────────────────────────────┬────────────────────────────────────────────────┘
                                        │
                                        │  Mailbox Doorbell Interrupt (msgbox0)
                                        ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        LINUX ARM HOST (PREEMPT_RT EL1)                                 │
│ • 0% CPU cycles wasted on high-frequency per-byte / per-sample IRQ thrashing            │
│ • Wakes only to consume pre-validated, timestamped sensor batches in user space         │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

### Key Advantages of the I/O Offload Model:
1. **Zero Linux IRQ Jitter**: Servicing high-speed UART bytes (e.g. 460800 baud GPS streams) and 8 kHz IMU DRDY lines inside Linux causes frequent context switches and cache thrashing. The E902 absorbs 100% of these interrupts.
2. **Microsecond Hardware Timestamping**: Sensor frames and GPS PPS pulses are timestamped in hardware by the E902 at the exact moment of arrival before any OS scheduling delay occurs.
3. **Fail-Safe Operation**: If the Linux host temporarily spikes in load or performs an update, the E902 continues to capture sensor history and maintain actuator holding states without missing a single byte.

---

## 6. Mode 2 Implementation Blueprint (Linux RemoteProc & Real-Time Control)

When building embedded control systems, robotics, or industrial automation platforms that run 24/7 without suspend/resume, follow this blueprint across all firmware and kernel layers:

### A. ARM Trusted Firmware (TF-A / BL31): Open R_SPC Port 3

**What the stock Radxa BL31 already opens** (disassembly of `sunxi_security_setup` at VA `0x48013180`, CUBIE_A7A_DEBUG_LOG.md Milestone 11): SPC ports 0..7 (`0x02054000`), R_TZMA registers 0/4/8 (`0x07003000`, so System SRAM A2 is already non-secure), and R_SPC ports 0, 1 and 2. It leaves **R_SPC port 3** (`0x07002034`) secure on purpose, because the E902 behind it (`E902_CFG` `0x07032000`, `E902_STA_ADD_REG` `0x07032204`) is the arisc it owns. Mode 2 only needs port 3.

**What we do today — WORKAROUND, not the solution (owner decision 2026-11-10, DESIGN_DECISIONS.md "A7A BL31: source code in git")**: the vendor BL31 has no public source and no Radxa release opens port 3 (checked `radxa/allwinner-device` `bin/bl31.bin` v1.4.6/v1.4.8: ports 0–2 only). The solution is a BL31 built from tracked source (`dlan17/trusted-firmware-a` `A733` plus our commits; TODO.md "A7A BL31 from source"). Until it boots, the binary patch keeps the board working: `patch_bl31_security.py` hooks the port 2 store at VA `0x480131c8` (file offset `0x15e96c` in `radxa_a733_bootloader.bin`) with a branch to a trampoline in alignment padding at VA `0x48015570`, which repeats the port 2 write, adds `str w0, [x1, #48]` (port 3), and branches back. The script is idempotent and runs from `post-image.sh`. (Its docstring still says port 3 was "omitted"; read that as "left secure on purpose".)

**If you build TF-A from source instead**, the same change in `sunxi_security_setup()` is one more write:
```c
mmio_write_32(SUNXI_R_SPC_BASE + 0x34, 0xffffffff);   /* R_SPC port 3: E902_CFG */
```
A BL31 from another vendor (e.g. the Orange Pi build Armbian uses for sun60iw2) or a mainline-style TF-A may already open port 3. That only helps if the same build does not also start the arisc (§6.B).

**Power management without the arisc (unverified)**: the stock Radxa BL31 has no `sunxi_native_pm.c`; its only platform PM file is `plat/allwinner/sun60iw2p1/plat_pm.c`. What PSCI suspend, power-off and reset do once the arisc is not running has not been tested on the board (TODO.md Part II §5, SCP-2). Mode 2 assumes no suspend.

---

### B. U-Boot & TOC1 Packaging Changes
1. **Packaging**: Remove the SCP from the boot package so BL31 never starts the arisc. In our image this is a FIT edit in `post-image.sh` (sector 848 of `radxa_a733_bootloader.bin`): `loadables = "scp", "uboot";` → `loadables = "uboot";`, done by `patch_fit_no_scp.py` (restored 2026-11-10, SCP-1; first added in `cf724ae`, lost in `4a276a2`). It edits the property in place instead of re-packing with `dtc`, because the BL31 port-3 patch writes fixed offsets inside the same FIT, and the build fails if the property is not found. With the U-Boot 2026.04 blob we ship, the arisc is not started even with `scp` listed (nothing runs `sunxi_boot_arisc`); the edit keeps the SPL from loading the SCP at all (CUBIE_A7A_DEBUG_LOG.md Milestone 21).
2. **PMIC Standalone Initialization**:
   Ensure U-Boot initializes the **AXP8191 PMIC** over I2C (`s_twi0` / `sunxi_r_i2c0` @ `0x07083000`):
   - Enable `DCDC1` (3.3V system power for USB hub, Wi-Fi, Ethernet).
   - Enable `ALDO1` (3.3V for `VCC-PL` and `VCC-PM` I/O banks).
   - Enable `ELDO1`, `ELDO2`, `ELDO4` (USB PHY & core rails).
3. **Core Reset**: Leave the E902 held in reset in `r_ccu` at boot.

---

### C. Linux Kernel Driver & Device Tree Changes
1. **Device Tree Overlay (`cubie-a7a-rproc.dtso`)**:
   ```dts
   /dts-v1/;
   /plugin/;

   / {
       compatible = "radxa,cubie-a7a", "allwinner,sun60i-a733";

       fragment@0 {
           target = <&rproc>;
           __overlay__ {
               status = "okay";
               memory-region = <&rproc_carveout>;
           };
       };

       fragment@1 {
           target-path = "/reserved-memory";
           __overlay__ {
               #address-cells = <2>;
               #size-cells = <2>;

               rproc_carveout: rproc@4e000000 {
                   compatible = "shared-dma-pool";
                   reg = <0x00 0x4e000000 0x00 0x01000000>; /* 16 MB pool */
                   no-map;
               };
           };
       };
   };
   ```

2. **Driver (`sunxi_rproc.c`)**:
   - Register `"allwinner,sun60i-a733-rproc"`.
   - Map **SRAM A2 (`0x00040000`–`0x00073FFF`, 208 KB)** as the primary fast execution window via `devm_ioremap_wc()` with `is_iomem = true`.
   - Write entry address to `priv->cfg_va + 0x0204` (`E902_STA_ADD_REG`) during `sunxi_rproc_start()`.
   - Control core execution via `r_ccu` reset bits.

---

### D. RISC-V Firmware Compiler & ABI Contract
Because the E902 is RV32E, firmware applications must be built targeting:
```bash
-march=rv32emc_zicsr -mabi=ilp32e -mcmodel=medany
```
- **Registers**: Uses only `x0`–`x15`. Compiling with standard `ilp32` (32 registers) or float instructions will trigger illegal instruction hardware faults.
- **Linker Address**: Link firmware to `ORIGIN = 0x00044000` (System SRAM A2) or `ORIGIN = 0x4E000000` (DRAM Carveout).

---

### E. Checklist for Other Distributions (Armbian, Debian, Yocto, ...)

Linux remoteproc on the A733 E902 does not work with a stock vendor boot chain, whatever distribution sits on top. Any distribution that wants it has to make the same two boot-firmware changes we make; only the way it makes them can differ.

| # | Requirement | Why | Our way (reference) | Other ways |
| :--- | :--- | :--- | :--- | :--- |
| 1 | BL31 must **not start the arisc** | Otherwise the secure world owns the E902 and Linux takes it away (two owners) | Drop `"scp"` from the boot-package FIT `loadables` (§6.B; SCP-1) | Leave `scp` out of `boot_package.cfg` when packing with `dragonsecboot`; a TF-A build without the arisc driver |
| 2 | **R_SPC port 3** (`0x07002034`) non-secure | Linux must write `E902_STA_ADD_REG` (`0x07032204`); otherwise SError | Binary trampoline in stock BL31 (`patch_bl31_security.py`, §6.A) | One `mmio_write_32()` in a TF-A built from source; a vendor BL31 that already opens it |
| 3 | Accept no arisc power management | Suspend/idle/PMIC paths that used the arisc are gone; PSCI behaviour without it is unverified (SCP-2) | Mode 2 runs without suspend; Linux drives the PMIC over `s_twi0` | Test suspend, power-off and reboot on that BL31 build |
| 4 | Kernel driver and DT | — | `allwinner,sun60i-a733-rproc` in `sunxi_rproc.c`, `cubie-a7a-rproc.dtbo` | — |

**Do not assume** from "it boots" or "remoteproc starts" that a BL31 is suitable. Both checks below must pass on the image the distribution ships:
1. The serial log shows no `[SCP]` lines (`[SCP] :arisc startup ready` means requirement 1 is not met). This alone is not proof: with the Radxa U-Boot 2026.04 blob the log has no `[SCP]` lines whether or not `scp` is in the FIT, because that U-Boot only starts the arisc from its `sunxi_boot_arisc` command. Also check what the U-Boot build does (an older vendor U-Boot with `CONFIG_ARISC_DEASSERT_BEFORE_KERNEL=y` starts it before every kernel).
2. With the remoteproc clock on and reset released, reading `0x07032204` from Linux does not abort (requirement 2).

Each vendor builds its own BL31 (Radxa, Orange Pi, ...), so check these for every BL31 build, not once per SoC.

---

### F. Board Test: Mode 2 Without the Vendor SCP

Run 2026-11-10 on the camera-work image. Results: `docs/platforms/CUBIE_A7A_DEBUG_LOG.md` Milestone 21. TODO.md (Part I §2) only shows the status.

- Why: vendor U-Boot (`radxa-cubie-a7a_defconfig`: `CONFIG_SUNXI_ARISC_EXIST=y`, `CONFIG_ARISC_DEASSERT_BEFORE_KERNEL=y`) asks BL31 to start the arisc (SMC `0x8000ff10`, `arch/arm/lib/bootm.c:387`) right before the kernel; BL31 keeps R_SPC port 3 secure for it. We use Radxa's prebuilt U-Boot, so the arisc is stopped by dropping `scp` from the boot-package FIT instead (that step was lost in `4a276a2`). Needed before sending the TF-A list question.
- **Finding (2026-11-10):** that defconfig is the old vendor U-Boot. The U-Boot we ship (`2026.04-3-boot-dlan17-g24da4dae7637`) never asks BL31 for the arisc during a normal boot; it only does so from the `sunxi_boot_arisc` command. Boots with and without `scp` in the FIT print the same BL31 lines, no `[SCP]` lines, and the SCP image is not in SRAM A2. So the question "what does BL31 do when asked for a missing arisc" did not come up on this blob.
- [x] SCP-1: restore the FIT `loadables` edit in `post-image.sh` (fail the build if the line is not found); keep the port-3 patch. (`patch_fit_no_scp.py`, in-place edit; both scripts now stop the build on failure.)
- [x] Read back the FIT in the built `radxa_a733_bootloader.bin`: no `scp` in `loadables`. (Checked on a patched copy of the 2026-11-08 build and on the board's SD card; a full Buildroot rebuild has not run the new step yet.)
- [x] Boot with serial console: Linux comes up, no `[SCP] :arisc startup ready`. Save what BL31 prints when U-Boot calls `ARM_SVC_ARISC_STARTUP` (error and continue, or hang in `wait arisc ready`). (U-Boot never makes that call; see the finding above.)
- [x] PMIC rails still fine without the arisc: USB hub, AIC8800 Wi-Fi, camera all come up. (Camera: `/dev/video0` present, no frames captured in this test.)
- [x] E902 remoteproc: start/stop firmware 10 times (10/10). `tools/run_full_sweep.py --target a7a`: **FAIL**, an existing RPMsg/msgbox bug that is not caused by removing the SCP (msgbox channel direction never set: `sun6i-msgbox.c:106` WARN, then `ping_rpmsg` hangs in the kernel and blocks `stop` and `reboot`). Tracked in TODO.md.
- [ ] `reboot` works (done, three boots); `poweroff` really cuts power (vendor U-Boot's own power-off starts the arisc first, `board_sun60iw2.c:40`); try suspend (expected unsupported) and record what happens.
- [x] Record results in `docs/platforms/CUBIE_A7A_DEBUG_LOG.md` (Milestone 21) and `docs/A733_E902_BOOT_AND_COPROCESSOR_ARCHITECTURE.md` §6.B/§6.E.

---

## 7. Comparison Matrix: Mode 1 vs. Mode 2

| Feature / Subsystem | Mode 1: Suspend/Resume (`scp.fex`) | Mode 2: Linux RemoteProc (Real-Time Control) |
| :--- | :--- | :--- |
| **Primary Use Case** | Consumer Battery Devices (S3 Sleep) | **24/7 Embedded Control, Robotics, Real-Time I/O** |
| **Boot package** | FIT `loadables = "scp", "uboot"` | **FIT `loadables = "uboot"` (SCP-1: step currently lost)** |
| **E902 Boot Time** | Starts at power-on (`boot0`) | **Starts dynamically from Linux (`remoteproc`)** |
| **PMIC Control** | Handled by `scp.fex` over `s_twi0` (I2C) | **Handled directly by U-Boot / Linux I2C driver** |
| **BL31 Security** | R_SPC port 3 secure (E902 owned by the arisc) | **R_SPC port 3 opened by binary patch; R_TZMA and ports 0–2 are already open in stock BL31** |
| **DRAM Map** | `0x40014000` reserved (`no-map`) | **`0x4E000000` DMA pool / SRAM A2 (`0x00040000`)** |
| **Linux RemoteProc** | Disabled (`status = "disabled"`) | **Enabled (`sunxi_rproc.c`)** |
| **Firmware Toolchain**| Vendor binary | **Bare-metal C++ (`-march=rv32emc_zicsr -mabi=ilp32e`)** |

---

## 8. Mode 2 Action Plan: Decoupling Power from E902 Safely

To safely repurpose the XuanTie E902 without power collapse or rail instability, follow this 4-step decoupling strategy:

```
+───────────────────────────────────────────────────────────────────────────────────────+
|                  POWER DECOUPLING STRATEGY FOR E902 MODE 2                            |
+───────────────────────────────────────────────────────────────────────────────────────+

 1. BOOTLOADER STAGE (boot0 + U-Boot):
    • Vendor boot0 sets core DRAM voltages (DCDC2/3 = 0.9V, DCDC4 = 1.1V).
    • U-Boot initializes s_twi0 (0x07083000) directly:
      - Writes AXP8191 registers to latch DCDC1 = 3.3V, ALDO1 = 1.8V, ELDO1/2/4 ON.
      - Boot-package FIT has no "scp" loadable, so BL31 never starts
        the arisc and the E902 stays in reset (SCP-1: step currently lost).

 2. SECURE MONITOR (TF-A / BL31):
    • Stock Radxa BL31 with one binary patch: R_SPC port 3 opened so
      Non-Secure Linux EL1 can access E902_CFG (0x07032000).
    • System SRAM A2 (0x00040000) is already non-secure (stock R_TZMA setup).
    • PSCI suspend/off/reset without the arisc: unverified (SCP-2).

 3. LINUX HOST KERNEL:
    • Binds s_twi0 to allwinner,sun6i-a31-i2c (NOT sun8i-a23-rsb).
    • axp20x / axp8191 regulator driver directly manages all rails.
    • Key rails declared regulator-always-on and regulator-boot-on.
    • sunxi_rproc loads real-time firmware into SRAM A2 and releases E902 reset.

 4. XUANTIE E902 (User Real-Time Firmware):
    • Runs real-time application (sensor sampling, IMU, GPS, motor PWM).
    • Has ZERO power management responsibilities.
    • Never touches s_twi0 or AXP registers; communicated with ARM via msgbox0.
```

### Action Items & Deliverables:
- [ ] **Task P.1 (U-Boot Bus Probe)**: Verify `s_twi0` bus ID in U-Boot (`i2c bus`), probe PMIC addresses `0x36` and `0x34`, and confirm register read/write works without ARISC.
- [ ] **Task P.2 (DTS Bus Correction)**: In `0001-arm64-dts-allwinner-add-sun60i-a733-cubie-a7a.patch`, replace `r_rsb: rsb@7083000` with `s_twi0: i2c@7083000` (`allwinner,sun6i-a31-i2c`). This eliminates the `-110` timeout and brings up the AXP8191 regulator tree in Linux.
- [ ] **Task P.3 (Fixed Voltage Stability)**: Lock CPU and system rails to standard high-performance operating points in Device Tree (`regulator-always-on`) so DVFS is not required during real-time flight operation.
- [x] **Task P.4 (TF-A Firewall Patch)**: Open R_SPC port 3 so `sunxi_rproc` can write `0x07032204` (`E902_STA_ADD_REG`) without a bus fault. Done as a binary patch (`patch_bl31_security.py`, commit `99fc419`); R_TZMA needed no change.
- [ ] **Task P.6 (Restore SCP removal)**: Put the FIT `loadables` edit back in `post-image.sh` (TODO SCP-1).
- [ ] **Task P.7 (PSCI without arisc)**: Test suspend, power-off and reboot with the arisc not running (TODO SCP-2).
- [ ] **Task P.5 (E902 Firmware Build)**: Target `-march=rv32emc_zicsr -mabi=ilp32e` linked to SRAM A2 (`0x00044000`) for ping and telemetry testing over `msgbox0`.
