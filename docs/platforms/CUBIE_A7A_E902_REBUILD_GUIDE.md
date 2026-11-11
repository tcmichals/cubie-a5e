# 📖 Radxa Cubie A7A: XuanTie E902 Remoteproc Rebuild & Bring-Up Guide

This guide documents the complete procedure to build, patch, package, and run the **XuanTie E902** coprocessor on the **Radxa Cubie A7A (Allwinner A733 / `sun60iw2`)** under the Linux `remoteproc` framework (Mode 2: Real-Time Coprocessor).

---

## 1. Hardware & Architectural Summary

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Coprocessor Core** | **T-Head XuanTie E902** | RV32EMC (`-march=rv32emc_zicsr -mabi=ilp32e`), 16 GPRs (`x0`–`x15`), no FPU |
| **Clock Frequency** | 200 MHz | Driven by PRCM `R_CPUCFG` / `R_CCU` |
| **Instruction SRAM (SRAM A2)** | **0x00040000 - 0x00073FFF** (208 KB) | Non-aliased contiguous SRAM window shared with ARM cores |
| **Configuration Space (`E902_CFG`)** | **0x07032000 - 0x070323FF** | Core control registers in Always-On (`R_`) domain |
| **Boot Address Register (`E902_STA_ADD_REG`)** | **0x07032204** | Latches E902 reset vector address upon core deassertion |
| **Reset Control** | `R_CPUCFG` (`0x07050008`) / `R_PRCM` (`0x07010210`) | Managed by `sunxi_rproc.c` |
| **EL3 Security Gate (`R_SPC` Port 3)** | **0x07002034** | Not needed: with `scp` not loaded, the stock BL31 allows non-secure access (tested 2026-11-10, §6). The binary patch `patch_bl31_security.py` can go |
| **Inter-Processor Signaling** | `msgbox0` (`0x07090000`, 8 bidirectional FIFO channels) | Channel 0 assigned to remoteproc doorbell kicks |
| **Host Operating System** | **Linux 7.1 PREEMPT_RT** | Driver compatible: `allwinner,sun60i-a733-rproc` |

---

## 2. Summary of Repository Changes

### A. Linux Kernel (`linux-cubie`)
* **Topic Branch**: [`a7a_remoteproc_camera_usb`](https://github.com/tcmichals/linux-cubie/tree/a7a_remoteproc_camera_usb) (commit `8e92f2e5014e`).
* **Isolation Rule**: `cubie-linux-7.1` and `next-sun55i-rproc-msgbox` remain 100% clean and untouched for upstream v3 patch submission.
* **Driver Enhancements (`drivers/remoteproc/sunxi_rproc.c` & `sunxi_rproc.h`)**:
  - Added `SUN60I_SRAM_A2_SYS` (`0x00040000`), `SUN60I_SRAM_A2_SIZE` (`208 KB`), and `E902_STA_ADD_REG` (`0x0204`).
  - Added `sun60i_rproc_att[]` memory translation table for SRAM A2.
  - Added `sun60i_e902_cfg` with compatible `"allwinner,sun60i-a733-rproc"`.
  - Made optional: `r_sram1`, `remap` register, `sram` bus clock, and `debug` reset (absent on A733 E902).

### B. Buildroot & Target Integration (`cubie-a5e`)
* **Bootloader Patch (`patch_bl31_security.py`)**: Automatically hooks `radxa_a733_bootloader.bin` at VA `0x480131c8`, branching to an in-segment trampoline at `0x48015570` to write `0xffffffff` to `R_SPC` Port 3 (`0x07002034`), un-gating `0x07032000` (`E902_CFG`).
* **Packaging Script (`project-cubie-a5e/board/radxa/cubie_a7a/post-image.sh`)**: Automatically calls `patch_bl31_security.py` during image generation and stages `cubie-a7a-rproc.dtbo`.
* **Device Tree Overlay (`project-cubie-a5e/dts-overlay/allwinner/cubie-a7a-rproc.dtso`)**: Declares `rproc_e902` with 208 KB SRAM A2, 16 MB DRAM carveout (`0x4E000000`), and `msgbox` channel 0.
* **Boot Configuration (`project-cubie-a5e/board/radxa/cubie_a7a/config.txt`)**: Enabled `dtoverlay=cubie-a7a-rproc`.
* **Kernel Configuration (`project-cubie-a5e/board/radxa/cubie_a7a/linux.config`)**: Enabled `CONFIG_REMOTEPROC=y`, `CONFIG_SUNXI_REMOTEPROC=y`, `CONFIG_RPMSG_VIRTIO=y`, and `CONFIG_RPMSG_CHAR=y`.
* **Defconfig (`project-cubie-a5e/configs/cubie_a7a_defconfig`)**: Set `BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="a7a_remoteproc_camera_usb"`.

### C. RISC-V Coprocessor Firmware (`cubie-a5e`)
* **Linker Script (`firmware/common/arch_riscv/e902_sram.ld`)**: Linked for SRAM A2 (`ORIGIN = 0x00040000`, `LENGTH = 208K`).
* **Application (`firmware/riscv-firmware/apps/exampleRiscv/`)**: Configured `BOARD=a7a` with strict RV32E compilation flags (`-march=rv32emc_zicsr -mabi=ilp32e`).

---

## 3. Step-by-Step Rebuild Instructions

Run all steps from your development host.

### Step 1: Build the RISC-V Coprocessor Firmware
```bash
cd /home/tcmichals/projects/cubie/cubie-a5e/firmware/riscv-firmware/apps/exampleRiscv
make clean
make BOARD=a7a

# Stage the verified ELF binary into rootfs overlays
cp -f exampleRiscv.elf /home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a7a/rootfs-overlay/lib/firmware/exampleRiscv.elf
cp -f exampleRiscv.elf /home/tcmichals/projects/cubie/bld.a7a/target/lib/firmware/exampleRiscv.elf
```

### Step 2: Ensure the Kernel Topic Branch is Checked Out
```bash
cd /home/tcmichals/projects/cubie/linux-cubie
git checkout a7a_remoteproc_camera_usb
```

### Step 3: Rebuild the Linux Kernel
```bash
cd /home/tcmichals/projects/cubie/cubie-a5e
make -C /home/tcmichals/projects/cubie/bld.a7a linux-rebuild
```
*(Buildroot will rsync from `linux-cubie`, apply `linux.config`, build `vmlinux`/`Image`, and install to `bld.a7a/images/Image`.)*

### Step 4: Re-Pack the Bootloader, Overlays, and `sdcard.img`
```bash
HOST_DIR=/home/tcmichals/projects/cubie/bld.a7a/host \
BINARIES_DIR=/home/tcmichals/projects/cubie/bld.a7a/images \
TARGET_DIR=/home/tcmichals/projects/cubie/bld.a7a/target \
BUILD_DIR=/home/tcmichals/projects/cubie/bld.a7a/build \
/home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a7a/post-image.sh
```
*(This automatically patches `radxa_a733_bootloader.bin` with the BL31 security un-gating trampoline, stages `cubie-a7a-rproc.dtbo`, generates `boot.vfat`, and writes the completed `sdcard.img`.)*

---

## 4. Flashing to MicroSD Card

Identify your SD card device (e.g. `/dev/sdX`) with `lsblk`:

```bash
sudo dd if=/home/tcmichals/projects/cubie/bld.a7a/images/sdcard.img of=/dev/sdX bs=4M status=progress conv=fsync
```

---

## 5. Board Verification Sequence (Target Console)

After inserting the flashed SD card and powering on the Radxa Cubie A7A:

### 1. Verify BL31 Security Un-gating
Read the E902 reset vector address register from Linux user space:
```bash
devmem2 0x07032204
```
* **Success**: Reads `0x00040000` (or `0x00000000`) cleanly without hanging.
* **Failure (Security Gated)**: Linux triggers an immediate kernel panic with `SError Interrupt / Asynchronous Data Abort`.
* **Correction (2026-11-10, §6):** this check proves nothing. Reads work with the stock BL31 too (power-on value `0x40014000`), and writes are ignored while the core reset is held. Check a write with read-back while the core is out of reset instead.

### 2. Verify Remoteproc Driver Probing
```bash
dmesg | grep -i remoteproc
```
Expected output:
```text
remoteproc remoteproc0: 7032000.remoteproc is available
remoteproc remoteproc0: powering up 7032000.remoteproc
remoteproc remoteproc0: Booting fw image exampleRiscv.elf, size 8444
remoteproc remoteproc0: remote processor 7032000.remoteproc is now up
```

### 3. Check Remoteproc Sysfs Control
```bash
# Check current operational status
cat /sys/class/remoteproc/remoteproc0/state
# Output: running

# Stop the E902 coprocessor
echo stop > /sys/class/remoteproc/remoteproc0/state

# Change firmware if desired
echo exampleRiscv.elf > /sys/class/remoteproc/remoteproc0/firmware

# Start the E902 coprocessor
echo start > /sys/class/remoteproc/remoteproc0/state
```

### 4. Verify Coprocessor Execution via Memory Dump
`exampleRiscv.elf` writes diagnostic heartbeat patterns to SRAM A2 and DRAM carveout:
```bash
# Inspect SRAM A2 mailbox area
devmem2 0x00050000 w 4
# Expected: 0x31313131 0x32323232 0x33333333 0x34343434

# Inspect DRAM carveout area
devmem2 0x4e010000 w 4
# Expected: Incremental heartbeat counter updated by E902 timer loop
```

---

## 6. Open Test: Is a BL31 Change Needed Without the SCP? (2026-11-10)

**Why:** a review suggested that with `scp.fex` left out of the boot package, the stock BL31 would let Linux run the E902 with **no BL31 change**. Every image since `99fc419` (2026-10-24) has had the BL31 patch, so that was never tested. If it works, `patch_bl31_security.py` and the §5.1 expectation above go away, and the TF-A work (TODO.md "A7A BL31 from source") no longer needs the port-3 commit.

**What the code says (reviewed 2026-11-10):**
- Stock BL31 `sunxi_security_setup` (VA `0x48013180`) is straight-line: SPC ports 0–7, R_TZMA 0/4/8, R_SPC ports 0, 1, 2. It never writes R_SPC port 3, has no branch on whether an SCP was loaded, and nothing else in BL31 refers to R_SPC or to `0x07032000`/`0x07032204`.
- So port 3 is left at its **power-on default**. The A733 User Manual lists `S_SPC` at `0x07002000` but documents no registers, so the default is not known from documents. The code cannot answer the question; the board test does.
- Linux touches one register behind port 3: the start-address write in `sunxi_rproc_start()` (`sunxi_rproc.c:263`). Reset and clocks are in `r_ccu`, which is non-secure already.

**Test:**
1. On the SD card, put back the stock BL31 bytes (undo `patch_bl31_security.py`): `0xb9002020` at byte `0x15e96c` (sector 2804) and 12 zero bytes at `0x160d14` (sector 2822). Keep `scp` out of the FIT (`patch_fit_no_scp.py`).
2. Boot. Set `sysctl kernel.panic=10`, so an SError reboots the board instead of hanging it. (Boot itself does not touch `0x07032204`: the boot-time firmware load fails before the driver writes it.)
3. `devmem 0x07032204 32`, then start `exampleRiscv.elf` through `/sys/class/remoteproc/remoteproc0/state`.
4. Pass: no SError, state `running`, start/stop works → **no BL31 change needed** once the SCP is not loaded. Fail: SError → port 3 defaults to secure → a BL31 change is needed (tracked TF-A commit `315e08e004`); put the patch back.

**Result (2026-11-10): no BL31 change is needed. The real bug is in the driver.**

| BL31 on the SD card | Core reset (`RISCV_CFG_RST`, r_ccu 12) | Read `0x07032204` | Write `0x00040000`, read back |
| :--- | :--- | :--- | :--- |
| stock (`b9002020` at `0x15e96c`), cold power-on | held (remoteproc offline) | `0x40014000`, no SError | `0x40014000`: write ignored |
| patched (`140008ea`) | held | `0x40014000` | `0x40014000`: write ignored |
| patched | released (remoteproc running) | `0x40014000` | **`0x00040000`: write sticks** |
| **stock** | **released** | `0x40014000`, no SError | **`0x00040000`: write sticks, no SError** |

- With `scp` not loaded, the **stock** BL31 lets Linux read and write `E902_STA_ADD_REG`. The review was right: R_SPC port 3 does not block it, and `patch_bl31_security.py` is not needed. (Port 3 is never written by the stock BL31, so its power-on default allows non-secure access; whether loading `scp` changes that was not tested, because our U-Boot never starts the arisc.)
- The E902 config block **ignores writes while `RISCV_CFG_RST` is asserted**. `sunxi_rproc_start()` writes the start address and only then releases that reset (`sunxi_rproc.c:263`), so the write is always lost and the E902 starts from the power-on default `0x40014000` (zero-filled DRAM in the DT `scp@40014000` region), not from our firmware. That is why `state` says `running` while `trace0` stays empty (`testStringBinaryTrace0.elf`), and it explains the A7A RPMsg failures (TODO.md).
- So the 2026-11-10 "10/10 start/stop" and the earlier "remoteproc running" results only show that the reset toggles. The E902 has not been shown to run our firmware on the A7A.
- §5.1 above is wrong: an SError was not seen, and a read alone proves nothing (non-secure reads always worked). The check is a **write with read-back while the core is out of reset**.
- Next (driver): set the start address in a way that survives the reset. `RISCV_CFG_RST` is the only reset, so writing after releasing it races against the core fetching from `0x40014000`. Find the vendor sequence (how vendor BL31 "setup arisc para" then "release arisc reset"; vendor BSP), e.g. gate the E902 core clock, release reset, write the address, ungate. Until then, firmware linked at `0x40014000` would run, if that DRAM is reserved and mapped for the E902.

### 6.1 Vendor start sequence and first E902 execution (2026-11-10)

The vendor kernel has no E902 driver (`CONFIG_AW_REMOTEPROC` and `CONFIG_REMOTEPROC` off in `config-5.15.147-21-a733`); only BL31 starts the E902 (arisc). Disassembly of the shipped BL31 right after `[SCP] :set arisc reset to de-assert state` (VA `0x48011584`):

```
memcpy(0x00044000, 0x48100000, 0x28000)   ; SCP image into SRAM (ARM view)
0x0701021c  = 0x00010003    ; R_CCU: riscv + riscv-cfg gates (bits 0,1), RST_BUS_RISCV_CFG released (bit 16)
0x07032204  = 0x40004000    ; E902_STA_ADD_REG, written AFTER bit 16 is released
0x07050008 |= 0x1           ; R_CPUCFG +0x8 bit 0: E902 core reset release (core runs)
0x07010210 |= 0x80000000    ; R_PRCM +0x210 bit 31: riscv-24m clock gate
0x50005120 &= ~0x1          ; not identified
```

Our driver (`a7a_remoteproc_camera_usb`) does only the first line: in "running" the board shows `0x0701021c=0x10003`, `0x07050008=0`, `0x07010210=0`, and the start address write is lost (done before bit 16). So the E902 was never released.

**Board test, stock BL31, no `scp`, everything from Linux with `devmem`:** a 9-word position-independent RV32E probe at ARM `0x44000` (stores its own `auipc` address, `0x12345678` and a running counter at +0x100), then the vendor sequence above (except the unidentified last write):

| Result | Value |
| :--- | :--- |
| `0x07032204` after the write | `0x40004000` (kept) |
| marker: probe's own address as the E902 sees it | **`0x40004000`** |
| marker 2 | `0x12345678` |
| counter | `0x0208a984` → `0x040a0066` one second later (about 33 M/s) |
| after clearing `0x07050008` bit 0 | counter stops: that bit is the core reset |

Conclusions:
1. **The E902 runs code from Linux with the stock BL31, no BL31 change, no SCP.**
2. **The E902 sees SRAM at ARM address + `0x3ffc0000`** (ARM `0x44000` = E902 `0x40004000`). The A7A ATT in `sunxi_rproc.c` (SRAM A2 1:1 at `0x00040000`) and firmware linked at `0x40000` are wrong for the E902; vendor `scp` is linked at `0x40004000`. The power-on start address `0x40014000` is ARM `0x54000`. How DRAM looks from the E902 is not tested yet.
3. **Driver start sequence to implement:** gates + `RST_BUS_RISCV_CFG` release, then write `E902_STA_ADD_REG`, then `riscv-24m` gate and `0x07050008` bit 0. Stop: clear `0x07050008` bit 0. The core reset (`R_CPUCFG` `0x07050008` bit 0) needs a reset provider; it is not in the A733 DT today.


### 6.2 First Linux remoteproc run, and the 16 KB the E902 cannot run from (2026-11-10, evening)

Image of 19:07 (kernel #20 with the `cfg`/`core` resets and the `0x40000000` ATT, stock BL31, no `scp`), firmware linked at `0x40000000`: `remoteproc` said `running`, the registers were right (`0x07032204=0x40000000`, `0x07050008=1`, `0x0701021c=0x10003`, code loaded at host `0x40000`), but nothing ran (`.bss`, trace buffer and the exampleRiscv probe address stayed 0).

The §6.1 probe, by hand with `devmem`:

| Probe at host → started at (E902) | Result |
| :--- | :--- |
| `0x44000` → `0x40004000` | runs: marker `0x40004000`, counter ~33 M/s |
| `0x40000` → `0x40000000` | never runs |
| `0x44000` → `0x40004000`, `riscv-24m` gate (`0x07010210` bit 31) **off** | runs: the gate is not needed |

**The E902 does not run code from the first 16 KB of SRAM A2** (host `0x40000`–`0x43fff`). The vendor SCP sits at `0x44000` for that reason. `e902_sram.ld` now links at `0x40004000`, 192 KB (host `0x44000`–`0x73fff`); the ATT and `translate_da` need no change. Whether the E902 can read or write that first 16 KB as data is not tested.

With firmware linked at `0x40004000` (copied to `/tmp` over serial, `firmware_class.path=/tmp`):
- `exampleRiscv.elf`: its `_start` probe loop counts at host `0x50000` (about 20 M/s). It never reaches `main()` by design, so `0x50004` stays 0.
- `testStringBinaryTrace0.elf`: `trace0` shows the banner and telemetry frames; the packet at host `0x468e0` (`MLET`, sequence number) goes up by 2 a second, so the firmware timer works.
- 10 stop/start cycles: all `running`, no remoteproc errors.

So **Linux remoteproc runs the E902 with the stock vendor BL31, no SCP and no BL31 patch.** Still open: RPMsg (msgbox channel direction, TODO), the E902 view of DRAM and the msgbox, `0x50005120`. The firmware banner text still says "T527 E906".

### 6.3 E902 memory map (A733 User Manual, the source of truth)

Host view: **SRAM A2 `0x00040000`–`0x00073FFF`, "16K+192K (The 16K is only used by CPUS.)"**. E902 view:

| E902 address | Size | What | Use |
| :--- | :--- | :--- | :--- |
| `0x40000000`–`0x40003FFF` | 16 KB | SRAM A2, CPUS-only part (host `0x40000`–`0x43fff`) | **Not used.** The E902 does not run code there; some words ignore host writes (§6.2) |
| `0x40004000`–`0x40033FFF` | 192 KB | SRAM A2, general part (host `0x44000`–`0x73fff`) | Firmware (`e902_sram.ld`), driver ATT, overlay `sram` window |
| `0x40034000`–`0x7FFFFFFF` | 1 GB − 208 KB | DRAM, same address as the host | Carveouts (`0x4e000000`), no ATT entry needed |
| `0x80000000`–`0xBFFFFFFF` | 1 GB | DRAM window, `e902_remap` selects the DDR GB (0 = not mapped) | Not used |
| `0x00080000`–`0x0709FFFF` | | Peripherals at the host addresses | msgbox banks `0x03004000` (host) / `0x07094000` (E902) |

Shared SRAM (`0x74000`–`0xF3FFF`, 512 KB) is usable only after NPU mapping with the NPU powered (manual), so not for the E902 today.
