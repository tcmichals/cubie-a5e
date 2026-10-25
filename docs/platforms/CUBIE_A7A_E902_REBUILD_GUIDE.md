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
| **EL3 Security Gate (`R_SPC` Port 3)** | **0x07002034** | Un-gated in TF-A / BL31 via in-segment executable trampoline |
| **Inter-Processor Signaling** | `msgbox0` (`0x07090000`, 8 bidirectional FIFO channels) | Channel 0 assigned to remoteproc doorbell kicks |
| **Host Operating System** | **Linux 7.1 PREEMPT_RT** | Driver compatible: `allwinner,sun60i-a733-rproc` |

---

## 2. Summary of Repository Changes

### A. Linux Kernel (`linux-cubie`)
* **Topic Branch**: [`feature/a733-e902-remoteproc`](https://github.com/tcmichals/linux-cubie/tree/feature/a733-e902-remoteproc) (commit `8e92f2e5014e`).
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
* **Defconfig (`project-cubie-a5e/configs/cubie_a7a_defconfig`)**: Set `BR2_LINUX_KERNEL_CUSTOM_REPO_VERSION="feature/a733-e902-remoteproc"`.

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
git checkout feature/a733-e902-remoteproc
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
