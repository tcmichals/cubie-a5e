# How To Debug XuanTie RISC-V Co-Processors (T527 E906 / A733 E902)

> [!CAUTION]
> **CRITICAL HARDWARE REALITY: NO ON-CHIP DMEM / OPENOCD SUPPORT**
> Direct memory-mapped debugging via `/dev/mem` (OpenOCD `dmem` or `rbb_server`) is **UNSUPPORTED** on Allwinner T527 and A733 silicon.
> The RISC-V hardware Debug Module (DM) is not routed to the non-secure ARM interconnect. Any attempt to access debug registers from Linux userspace fails with a bus error.
>
> **We operate "blind" without interactive GDB/OpenOCD capabilities**:
> - There are **no breakpoints**, **no single-stepping**, and **no interactive GDB register inspection via Linux**.
> - Part 2 below is a theoretical architecture and **does not function on production silicon**.
> - **All practical debugging must strictly rely on**:
>   1. **RemoteProc Trace Buffer** (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`).
>   2. **Dedicated Serial Console** (`S_UART0` @ `0x07080000` / 115200 baud).
>   3. **Direct Memory Probing** in Shared SRAM A2 (`0x00040000`–`0x00073FFF`).
>   4. **Hardware Mailbox Doorbell IRQ & IPC Rings**.

---

## Prerequisites

| Tool | Where to get it |
|---|---|
| `firmware.elf` | Built by `make -C riscv-firmware` on your x86 dev host |
| `gdb` (multi-arch) | Compiled into target rootfs via Buildroot (`BR2_PACKAGE_GDB=y`) |
| `openocd` | Buildroot package (see Phase 4 below) |
| Serial terminal | `minicom` / `screen` / VS Code serial monitor at 115200 baud |

---

## Part 1 — Load the Firmware via RemoteProc (recommended)

Using the mainline `remoteproc` kernel driver is the cleanest approach.
It handles clock gating, reset sequencing, and ELF loading automatically.

### Step 1: Copy the firmware ELF to the target

```bash
# On x86 dev host — SCP the ELF to the Cubie A5E over SSH
scp cubie-a5e/riscv-firmware/firmware.elf root@cubie-a5e:/lib/firmware/riscv-firmware.elf
```

### Step 2: Boot the co-processor from Linux

```bash
# On the target (ARM Linux shell)

# Point the remoteproc subsystem to our ELF
echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware

# Start execution
echo start > /sys/class/remoteproc/remoteproc0/state

# Verify it is running
cat /sys/class/remoteproc/remoteproc0/state
# Expected output: running
```

### Step 3: Read the co-processor trace log

The resource table declares a 4 KB trace buffer at SRAM C offset `0x7A000`.
The remoteproc subsystem automatically maps it to debugfs:

```bash
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
# Prints anything written via trace_puts() from the RISC-V firmware
```

---

## Part 2 — Theoretical On-Chip GDB Debugging (UNSUPPORTED on Allwinner Hardware)

> [!WARNING]
> **UNSUPPORTED ON ALLWINNER HARDWARE**:
> The steps below describe a theoretical on-chip MMIO bridge concept. In physical reality on Allwinner T527 and A733, the RISC-V Debug Module is **not mapped to the non-secure host bus**. Address `0x07090000` is the SoC RTC controller, not a RISC-V debug module.
> Attempting to run `dmi_test.py` or attach OpenOCD over `/dev/mem` will fail. Do not rely on this method; use Part 1 (RemoteProc Trace Buffer + Serial) for all active development.

This uses the **target-side `rbb_server` bridge** combined with a **software MMIO OpenOCD bridge** that reads co-processor registers through `/dev/mem` at physical address `0x07090000`.

### Step 4: Verify Hardware Debug Module Accessibility (The 3-Step Proof)

Before attaching GDB, verify that the physical Debug Module registers are mapped, clocked, and responding:

```bash
# 1. Start OpenOCD in the background
openocd -f /etc/openocd/openocd_t527_local.cfg &

# 2. Run the automated Python DMI verification tool
dmi_test.py
```

The script automatically executes the **3-Step Hardware Proof**:
1. **`dmstatus` Signature Check (DMI 0x11)**: Confirms `version = 2` (RISC-V Debug Spec 0.13.2) and `authenticated = 1` (returns valid hex like `0x00000482`, proving the bus is not floating/clock-gated).
2. **`dmactive` Loopback Flip Test (DMI 0x10)**: Writes `1` then `0` to bit 0 to verify true bidirectional read/write access.
3. **Core Halt & Resume Transitions**: Asserts `haltreq` (bit 31) and `resumereq` (bit 30), verifying that `dmstatus` toggles between `allhalted` and `allrunning`.

```bash
# Or verify base address directly via devmem:
devmem 0x07090000 32
# Expected response: 0x00000482 or 0x00004010 (dmstatus active)
```

### Step 5: Start rbb_server and OpenOCD on target
```bash
# Start the On-Chip Direct MMIO Debug Bridge daemon
rbb_server 0x07090000 &

# Start OpenOCD (listens on TCP port 3333 for GDB and 4444 for Telnet/Python)
openocd -f /etc/openocd/openocd_t527_local.cfg &
```

### Step 5: Start GDB server on the target

```bash
# On target — serve the firmware ELF as a gdbserver process
gdbserver :2345 --attach $(pidof none)   # attach mode without a process
# OR use the rproc sysfs to pause the core first:
echo stop > /sys/class/remoteproc/remoteproc0/state
```

### Step 6: Connect from your x86 development host

```bash
# On x86 dev host
riscv-none-elf-gdb cubie-a5e/riscv-firmware/firmware.elf

# Inside GDB:
(gdb) set arch riscv:rv32
(gdb) target remote cubie-a5e:3333
# OR connect to GDB server on port 2345:
(gdb) target remote cubie-a5e:2345
```

---

## Part 3 — Single-Stepping and Inspecting State

Once connected to the remote target, GDB commands work exactly as you
would expect on any embedded target:

### Breakpoints

```gdb
# Set a breakpoint at the firmware main() function entry
(gdb) break main
Breakpoint 1 at 0x00000050: file main.c, line 14.

# Set a breakpoint at the mailbox poll check
(gdb) break mailbox_has_new_msg
Breakpoint 2 at 0x000000a8: file mailbox.c, line 11.

# Continue execution
(gdb) continue
```

### Inspecting RISC-V Registers

```gdb
# Print all 32 integer registers
(gdb) info registers

# Print a specific register
(gdb) print $a0
(gdb) print/x $sp
(gdb) print/x $pc

# Print all CSR registers (XuanTie extension)
(gdb) info all-registers
```

### Inspecting Memory

```gdb
# Read 4 words from the mailbox FIFO register
(gdb) x/4xw 0x03003180

# Read 16 bytes from the ring buffer shared window
(gdb) x/16xb 0x00078000

# Read the SPI status register
(gdb) x/1xw 0x05010000
```

### Watching the Trace Buffer (live log)

```gdb
# Display the RISC-V firmware trace buffer as a string
(gdb) x/s 0x0007A000
```

### Single-Step Execution

```gdb
# Step over one source line
(gdb) next

# Step into a function (source level)
(gdb) step

# Step exactly one machine instruction
(gdb) stepi

# Print the current source location
(gdb) frame
(gdb) list
```

### Reading the Stack Frame

```gdb
# Print the backtrace call stack
(gdb) backtrace

# Print local variables in current frame
(gdb) info locals

# Print function arguments
(gdb) info args
```

---

## Part 4 — Stopping and Restarting the Co-Processor

```bash
# On target — stop the co-processor (ARM host asserts reset)
echo stop  > /sys/class/remoteproc/remoteproc0/state

# Reload a new firmware ELF (hot-reload during development)
echo "riscv-firmware.elf" > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
```

---

## Part 5 — Quick Reference Card

| Task | Command |
|---|---|
| Load firmware | `echo start > /sys/.../state` |
| Read trace log | `cat /sys/kernel/debug/remoteproc/remoteproc0/trace0` |
| Connect GDB | `target remote cubie-a5e:3333` |
| Set breakpoint | `break main` |
| Step one line | `next` |
| Step one instruction | `stepi` |
| Print registers | `info registers` |
| Read memory | `x/4xw 0x03003180` |
| Print variable | `print my_var` |
| Show backtrace | `backtrace` |
| Detach GDB | `detach` |
| Stop co-processor | `echo stop > /sys/.../state` |

---

## Part 6 — Common Problems & Fixes

| Symptom | Cause | Fix |
|---|---|---|
| `target remote` times out | OpenOCD not running or wrong IP | Check `openocd` process on target |
| `Cannot access memory at 0x0` | Core not booted / still in reset | `echo start > /sys/.../state` first |
| `No symbol "main"` | GDB loaded `.bin` not `.elf` | Point GDB at `firmware.elf`, not `firmware.bin` |
| Trace buffer shows garbage | Resource table not in `.resource_table` section | Check `firmware.elf` with `readelf -S firmware.elf` |
| RPMsg `/dev/rpmsg0` missing | `CONFIG_RPMSG_CHAR=y` not set | Rebuild kernel with the config fragment |

---

## Related Files

| File | Purpose |
|---|---|
| [riscv-firmware/](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/) | Co-processor bare-metal firmware source |
| [firmware.elf](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/firmware.elf) | ELF with debug symbols (use this with GDB) |
| [resource_table.c](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/resource_table.c) | RemoteProc resource table (trace + RPMsg vdev) |
| [sunxi_t527_rproc.c](file:///home/tcmichals/projects/cubie/bld/build/linux-7.1/drivers/remoteproc/sunxi_t527_rproc.c) | Kernel remoteproc driver |
| [rpmsg_host_example.c](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/rpmsg_host_example.c) | Linux userspace RPMsg send/receive example |
| [melis_hello_world.c](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/melis_hello_world.c) | UART "Hello World" from co-processor |
| [melis_sdk_example.c](file:///home/tcmichals/projects/cubie/cubie-a5e/riscv-firmware/melis_sdk_example.c) | Full peripheral driver reference (PLIC/DMA/timer) |
| [host_coprocessor_example.c](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/host_coprocessor_example.c) | Raw `/dev/mem` loader (no remoteproc) |
