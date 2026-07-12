# How To Debug the XuanTie E906/E907 Co-Processor with GDB

This guide is your step-by-step reference for attaching GDB to the RISC-V
co-processor running on the Allwinner T527 (Radxa Cubie A5E) **without
needing JTAG hardware**. We use the Linux **remoteproc framework** for
firmware loading and the **ARM CoreSight Debug Access Port (DAP)** to
expose co-processor registers to OpenOCD running on the ARM host itself.

> [!NOTE]
> **No `iomem=relaxed` required.** All clock gating, reset sequencing,
> and ITCM firmware loading is handled entirely inside the kernel's
> `sunxi_t527_rproc.c` driver. The kernel driver has unrestricted physical
> memory access — userspace never touches hardware registers directly.
> `CONFIG_STRICT_DEVMEM` remains enabled for system security.

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

## Part 2 — Attach GDB Remotely from Your x86 Dev Host

This uses the **target-side GDB server** (compiled with `--enable-targets=all`)
combined with a **software MMIO OpenOCD bridge** that reads co-processor
registers through `/dev/mem`.

### Step 4: Start the OpenOCD software transport on the target

```bash
# On target (ARM Linux)
openocd -f /etc/openocd/openocd_t527_local.cfg &
# OpenOCD listens on TCP port 3333
```

The config file `openocd_t527_local.cfg` maps the debug registers at
physical address `0x07090000` (XuanTie debug module base). See
[Blueprint 5](../workspace_prompts/prompt5_riscv_debug_bridge.md) for the
full config.

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
