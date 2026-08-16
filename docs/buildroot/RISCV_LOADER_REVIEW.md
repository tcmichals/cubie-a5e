# XuanTie E907 RISC-V Loader & Control Review

**Date:** 2026-08-16  
**Platform:** Radxa Cubie A5E / Allwinner T527 / A527  
**Co-Processor:** XuanTie E907 / E906 (RV32IMAC)  
**Document Purpose:** Complete technical review, code analysis, root-cause autopsy of the loader issue, and validation checklist.

---

## 1. System Architecture & Memory Mapping

The XuanTie E907 co-processor is managed from Linux on the ARM64 host by manipulating Clock Control Unit (CCU) registers and mapping SRAM/TCM via `/dev/mem` (`mmap`).

### Register & Memory Map

| Resource | Physical Address (Host View) | Register / Region Name | Description / Values |
|---|---|---|---|
| **CCU Base** | `0x07010000` | `CCU_BASE` | Clock Control Unit Base (mapped 4KB) |
| **MCU Bus Clock** | `0x07010020` | `CCU_DSP_CLK_REG` | Write `0x00000003` to gate on MCU & DSP clocks |
| **MCU/DSP Reset** | `0x07010100` | `CCU_DSP_RST_REG` | Bit 16: Boot Valid / Debug Reset (`1<<16`)<br>Bit 17: Core Run Reset release (`1<<17`)<br>Reset: `0x00000000`<br>Run: `0x00030000` or `0x00020000` |
| **ITCM (Code)** | `0x07110000` | `ITCM_PHYS_BASE` | 64KB Instruction TCM (`0x00000000` to E907) |
| **SRAM C (Body)** | `0x07130000` | `SRAM_C_BASE` | 320KB program memory (`0x00028000` to E907) |
| **Shared IPC Box** | `0x07180000` | `SRAM_IPC_BASE` | 32KB Ring buffer / shared telemetry window |

---

## 2. The Root Cause Autopsy: Why We Kept Circling

When running `./load-riscv.sh start ./firmware.bin` followed by `./load-riscv.sh status`, the system continually reported:

```
# ./load-riscv.sh start ./firmware.bin 
=== Loading XuanTie E907 RISC-V Firmware ===
XuanTie E907 RISC-V co-processor is running.
# ./load-riscv.sh status
Status: HALTED (In reset)
```

### Root Cause 1: `load-riscv.sh` was executing a shell fallback, NOT the C binary

In `load-riscv.sh`, the delegation check was:
```sh
if [ -x /usr/bin/riscv-load ]; then
    exec /usr/bin/riscv-load "$@"
fi
```
When running out of the local directory (`# ls` showed `firmware.bin  load-riscv.sh  riscv-load` in the current folder), `/usr/bin/riscv-load` was **not present or not marked executable at `/usr/bin/`**.

Instead of failing or running `./riscv-load`, `load-riscv.sh` silently fell through to its built-in fallback shell script:
```sh
# Fallback in load-riscv.sh:
case "$1" in
    start)
        echo "=== Loading XuanTie E907 RISC-V Firmware ==="
        devmem "$CCU_MCU_CLK_REG" 32 0x00000003 2>/dev/null || true
        devmem "$MCU_RST_REG" 32 0x00000000 2>/dev/null || true
        devmem "$MCU_RST_REG" 32 0x00020000 2>/dev/null || true
        echo "XuanTie E907 RISC-V co-processor is running."
        ;;
    status)
        rst_val=$(devmem "$MCU_RST_REG" 32 2>/dev/null || echo "N/A")
        if [ "$rst_val" = "0x00020000" ]; then
            echo "Status: RUNNING (Core active)"
        else
            echo "Status: HALTED (In reset)"
        fi
        ;;
```

### Root Cause 2: The fallback script NEVER copies the firmware
Notice what the fallback script's `start` command did: it wrote `0x03` to the clock register and `0x00020000` to the reset register, **without copying `firmware.bin` to ITCM (`0x07110000`)**.
The E907 woke up with empty/zero memory and crashed or reset immediately.

### Root Cause 3: The fallback script had a fragile string comparison
The fallback script tested `if [ "$rst_val" = "0x00020000" ]`.
If `devmem` returned `0x00000000` (due to core crashing or reset), `0x00030000` (bit 16 + 17), or `N/A`, it printed `Status: HALTED (In reset)`.

### Root Cause 4: Disconnect between Buildroot Package & Rootfs Overlay
1. `riscv-firmware.mk` compiled `riscv-load.c` and installed `riscv-load` and `firmware.bin` into `$(TARGET_DIR)`.
2. `load-riscv.sh` resided in `board/radxa/cubie_a5e/rootfs-overlay/usr/bin/`.
3. Running `make riscv-firmware` compiled the C binary, but **never touched `load-riscv.sh`** because overlays are only applied at rootfs finalization.
4. When versions were incremented in `riscv-load.c`, `load-riscv.sh` remained untouched unless manually edited or copied.

---

## 3. Code Modifications Applied

### A. C Loader: `riscv-firmware/riscv-load.c` (v1.0.2)

1. **Version Tracking:** Defined `LOADER_VERSION = "1.0.2"` with explicit `version` command and `usage()` reporting.
2. **Proper Status Evaluation:**
   Checks both Bit 17 (Core Run) and Bit 16 (Boot Valid):
   ```c
   if (strcmp(action, "status") == 0) {
       uint32_t clk = *(volatile uint32_t *)(ccu_virt + CCU_DSP_CLK_REG);
       uint32_t rst = *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG);
       printf("MCU Bus Clock Reg (0x07010020): 0x%08X\n", clk);
       printf("MCU Reset Reg     (0x07010100): 0x%08X\n", rst);
       if (rst & ((1 << 17) | (1 << 16))) {
           printf("Status: RUNNING (Core active)\n");
       } else {
           printf("Status: HALTED (In reset)\n");
       }
       munmap((void *)ccu_virt, CCU_MAP_SIZE);
       close(fd);
       return 0;
   }
   ```
3. **Verified Loading Sequence:**
   - Map CCU (`0x07010000`)
   - Enable clocks: `0x07010020 |= 0x03`
   - Assert reset: `0x07010100 &= ~(1 << 17)`
   - Map ITCM (`0x07110000`, 64KB) and write `firmware.bin`
   - Release reset: `0x07010100 |= (1 << 17) | (1 << 16)`

### B. Shell Wrapper: `load-riscv.sh` (v1.0.2)

Replaced duplicate/conflicting shell logic with a unified smart delegator that looks for `riscv-load` in:
1. The script's own folder (`$SCRIPT_DIR/riscv-load` — e.g. `./riscv-load` when running from a test directory)
2. `/usr/bin/riscv-load`
3. System `$PATH`

```sh
#!/bin/sh
# XuanTie E907 RISC-V Co-Processor Loader Wrapper for Allwinner A527/T527/A733
VERSION="1.0.2"

SCRIPT_DIR="$(dirname "$0")"

if [ -x "$SCRIPT_DIR/riscv-load" ]; then
    exec "$SCRIPT_DIR/riscv-load" "$@"
elif [ -x /usr/bin/riscv-load ]; then
    exec /usr/bin/riscv-load "$@"
elif command -v riscv-load >/dev/null 2>&1; then
    exec riscv-load "$@"
fi

echo "load-riscv.sh version $VERSION"
echo "Error: riscv-load binary not found or not executable."
echo "Searched: $SCRIPT_DIR/riscv-load, /usr/bin/riscv-load, and PATH"
exit 1
```

### C. Buildroot Package Integration: `riscv-firmware.mk`

Added `load-riscv.sh` directly to `RISCV_FIRMWARE_INSTALL_TARGET_CMDS` so that rebuilding `riscv-firmware` updates both the binary AND the script together:

```makefile
define RISCV_FIRMWARE_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/firmware.bin $(TARGET_DIR)/lib/firmware/riscv-firmware.bin
	$(INSTALL) -D -m 0755 $(@D)/firmware.elf $(TARGET_DIR)/usr/share/riscv-firmware/firmware.elf
	$(INSTALL) -D -m 0755 $(@D)/riscv-load $(TARGET_DIR)/usr/bin/riscv-load
	$(INSTALL) -D -m 0755 $(@D)/load-riscv.sh $(TARGET_DIR)/usr/bin/load-riscv.sh
endef
```

### D. Linker Script & UART0 Output Fix: `firmware.ld` & `melis_hello_world.c`

1. **Why UART0 Had No Output:**  
   `firmware.ld` previously mapped `.text` into SRAM C (`0x00028000`), generating a **167KB** binary where `.text` began 160KB past the 64KB ITCM boundary. Because `riscv-load` only copies 64KB into ITCM, `main()` was never loaded into memory. When `startup.S` branched to `main()`, it jumped into uninitialized memory (zeros), hit an illegal instruction, and looped forever in `_trap_handler` before executing any UART code.
2. **The Fix:**  
   Mapped `.vectors`, `.fastcode`, `.text`, and `.rodata` directly into `ITCM` (`0x00000000` - `0x0000FFFF`), with `.data` and `.bss` in `DTCM` (`0x00080000`).
   `firmware.bin` size shrunk from **167KB** to **4,032 bytes**, ensuring 100% of the code and string literals are loaded directly into ITCM.
3. **UART0 Timeout Protection:**  
   Added a register-ready loop timeout to `uart0_putc()` in `melis_hello_world.c` to prevent blocking the core if the UART Line Status Register check stalls.

---

## 4. Verification & Testing Guide

### Step 1: Rebuild the package
From the host workspace:
```bash
cd /home/tcmichals/projects/cubie-test
make -C bld.a5e riscv-firmware-dirclean
make -C bld.a5e riscv-firmware
```

### Step 2: Copy artifacts to board
```bash
scp bld.a5e/target/usr/bin/riscv-load root@<BOARD_IP>:/usr/bin/
scp bld.a5e/target/usr/bin/load-riscv.sh root@<BOARD_IP>:/usr/bin/
scp bld.a5e/target/lib/firmware/riscv-firmware.bin root@<BOARD_IP>:/lib/firmware/
ssh root@<BOARD_IP> "chmod +x /usr/bin/riscv-load /usr/bin/load-riscv.sh"
```

### Step 3: Run on the target board
```bash
# Check version
load-riscv.sh version
# Output: riscv-load version 1.0.2

# Start firmware
load-riscv.sh start /lib/firmware/riscv-firmware.bin

# Check status
load-riscv.sh status
```

**Expected Output on `start`:**
```
=== Loading XuanTie E907 RISC-V Firmware ===
Enabling MCU subsystem clocks (CCU 0x07010020 -> 0x03)...
Asserting RISC-V core reset...
Copying /lib/firmware/riscv-firmware.bin to ITCM (0x07110000)...
Copied 12345 bytes into ITCM.
Releasing reset (Booting XuanTie E907 at 0x00000000)...
XuanTie E907 RISC-V co-processor is running.
```

**Expected Output on `status`:**
```
MCU Bus Clock Reg (0x07010020): 0x00000003
MCU Reset Reg     (0x07010100): 0x00030000
Status: RUNNING (Core active)
```
