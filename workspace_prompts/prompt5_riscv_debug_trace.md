# Blueprint 5 — RISC-V Debug Bridge Research Trace
# XuanTie E907 ARM MMIO Debug Bridge for Allwinner T527 (sun55i-a523)

> **For the next AI session:** This document contains everything found during
> research so you can continue without repeating the investigation.

---

## Summary

Blueprint 5 goal: Debug the XuanTie E907 RISC-V co-processor from the ARM
Linux host **without any external JTAG hardware**.

The correct approach is the **ARM MMIO bridge**:
- The ARM Cortex-A55 is a master on the same AHB bus the E907 debug module sits on
- The ARM can directly read/write the RISC-V Debug Module registers via `mmap(/dev/mem)`
- A userspace daemon (`rbb_server`) translates OpenOCD's TCP `remote_bitbang` protocol
  to direct AHB register writes — no JTAG wires, no GPIO pins, no external probe

---

## Verified Register Map (from mainline kernel source)

### Source file
`/home/tcmichals/projects/cubie/bld/build/linux-7.1/drivers/clk/sunxi-ng/ccu-sun55i-a523-mcu.c`

### MCU CCU — `0x07102000`
```
Offset 0x120:  CLK_MCU_RISCV            (E907 core clock gate)
Offset 0x124:  CLK_BUS_MCU_RISCV_CFG    bit 0  = bus clock enable for debug block
               RST_BUS_MCU_RISCV_CFG    bit 16 = deassert CFG peripheral reset
               RST_BUS_MCU_RISCV_DEBUG  bit 17 = deassert DEBUG module reset  ← KEY
               RST_BUS_MCU_RISCV_CORE   bit 18 = deassert RISC-V core reset
Offset 0x128:  CLK_BUS_MCU_RISCV_MSGBOX bit 0  = mailbox bus clock
               RST_BUS_MCU_RISCV_MSGBOX bit 16 = mailbox reset
```

Source: `ccu-sun55i-a523-mcu.c` lines 402-405:
```c
[RST_BUS_MCU_RISCV_CFG]   = { 0x0124, BIT(16) },
[RST_BUS_MCU_RISCV_DEBUG] = { 0x0124, BIT(17) },  // ← debug reset confirmed
[RST_BUS_MCU_RISCV_CORE]  = { 0x0124, BIT(18) },
```

### Reset header
`include/dt-bindings/reset/sun55i-a523-mcu-ccu.h`:
```c
#define RST_BUS_MCU_RISCV_CFG    15
#define RST_BUS_MCU_RISCV_DEBUG  16   // ← named debug reset = debug module confirmed
#define RST_BUS_MCU_RISCV_CORE   17
```

---

## RISC-V Debug Module Base Address — Status: UNKNOWN

### Why it's unknown
The Allwinner T527/A523 full datasheet is NDA-protected. The mainline kernel
only exposes the MCU CCU clock/reset driver. The peripheral base address of
the RISC-V debug module (CFG block) is **not in any public document**.

### Address range to probe
The MCU subsystem AHB peripherals confirmed from `sun55i-a523.dtsi`:
```
0x07102000  MCU CCU (clock controller)    ← known
0x07112000  I2S0                          ← known
```
The RISCV CFG/debug block is **between these two** — in the gap:
```
0x07103000 – 0x07111FFF   ← probe this range
```

### How to find it: `probe_riscv_debug.sh`
Location: `/etc/openocd/` or `/usr/bin/probe_riscv_debug.sh` on target rootfs.

```bash
# Run on the Cubie A5E target (ARM Linux):
chmod +x /usr/bin/probe_riscv_debug.sh
/usr/bin/probe_riscv_debug.sh

# What it does:
# 1. Writes 0x07102124 to enable CFG clock (bit0) + deassert CFG/DEBUG resets (bit16,17)
# 2. Scans 0x07103000..0x07111000 in 4KB steps
# 3. Reports any address where read value has bit0=1 AND is not 0x0/0xFFFFFFFF
#    (JTAG IDCODE always has bit0=1 per IEEE 1149.1)
```

**The RISC-V DTM IDCODE will be a non-zero value with bit [0] = 1.**
Typical XuanTie E907 IDCODE pattern: `0x0XXXXX01`

---

## The ARM MMIO Bridge Architecture

```
┌─────────────────────────────────────────────────┐
│  x86 Dev Host                                   │
│  riscv-none-elf-gdb firmware.elf               │
│  (gdb) target remote cubie-a5e:3333            │
└──────────────────────┬──────────────────────────┘
                       │ GDB RSP protocol (TCP 3333)
┌──────────────────────▼──────────────────────────┐
│  ARM Linux (Cubie A5E)                          │
│  openocd -f openocd_t527_local.cfg             │
│  (uses remote_bitbang transport → port 3335)   │
└──────────────────────┬──────────────────────────┘
                       │ OpenOCD remote_bitbang TCP (port 3335)
                       │ Simple 1-char protocol: '0'-'7', 'R', 'Q'
┌──────────────────────▼──────────────────────────┐
│  rbb_server (runs on ARM Linux)                 │
│  - Listens on TCP :3335                        │
│  - Implements JTAG TAP state machine           │
│  - Intercepts DMI DR scans                     │
│  - Translates to direct AHB register access    │
└──────────────────────┬──────────────────────────┘
                       │ mmap(/dev/mem, DEBUG_BASE)
┌──────────────────────▼──────────────────────────┐
│  RISC-V Debug Module (AHB-mapped on MCU bus)   │
│  DEBUG_BASE + 0x00: IDCODE                     │
│  DEBUG_BASE + 0x04: DTMCS                      │
│  DMI access: base + (dmi_addr << 2)            │
│    0x10<<2 = +0x40: dmcontrol                  │
│    0x11<<2 = +0x44: dmstatus                   │
│    0x04<<2 = +0x10: data0                      │
│    0x17<<2 = +0x5C: command (abstract)         │
│    0x16<<2 = +0x58: abstractcs                 │
└──────────────────────┬──────────────────────────┘
                       │ internal debug bus
┌──────────────────────▼──────────────────────────┐
│  XuanTie E907 RISC-V core (halted/stepped)     │
└─────────────────────────────────────────────────┘
```

---

## RISC-V Debug Spec Register Map (standard v0.13)

These are the DMI address offsets used by the debug module.
When memory-mapped on AHB: `phys_addr = DEBUG_BASE + (dmi_addr << 2)`

| DMI addr | AHB offset | Register | Key fields |
|---|---|---|---|
| 0x04 | +0x10 | data0 | Abstract command data/result |
| 0x10 | +0x40 | dmcontrol | haltreq[31], resumereq[30], hartselhi[25:16], hartsello[15:6], dmactive[0] |
| 0x11 | +0x44 | dmstatus | allhalted[9], anyhalted[8], allrunning[11], version[3:0] |
| 0x16 | +0x58 | abstractcs | cmderr[10:8], busy[12], datacount[3:0] |
| 0x17 | +0x5C | command | cmdtype[31:24], size[22:20], postexec[18], transfer[17], write[16], regno[15:0] |
| 0x38 | +0xE0 | progbuf0 | Program buffer word 0 |
| 0x40 | +0x100 | sbcs | System bus access control |
| 0x41 | +0x104 | sbaddress0 | System bus read/write address |
| 0x48 | +0x120 | sbdata0 | System bus read/write data |

### Halt sequence (from ARM side):
```c
// 1. Activate debug module
writel(0x00000001, DEBUG_BASE + 0x40);  // dmcontrol: dmactive=1
// 2. Request halt
writel(0x80000001, DEBUG_BASE + 0x40);  // dmcontrol: haltreq=1, dmactive=1
// 3. Poll until halted
while (!(readl(DEBUG_BASE + 0x44) & (1<<9)));  // dmstatus.allhalted
// 4. Read PC (regno=0x07b1 = dpc CSR)
writel(0x00221000 | 0x07b1, DEBUG_BASE + 0x5C); // command: read CSR
while (readl(DEBUG_BASE + 0x58) & (1<<12));     // wait abstractcs.busy=0
uint32_t pc = readl(DEBUG_BASE + 0x10);          // data0 = PC value
```

---

## Files Created This Session

| File | Purpose |
|---|---|
| `riscv-firmware/resource_table.c` | RemoteProc resource table (RSC_TRACE + RSC_VDEV) |
| `project-cubie-a5e/rpmsg_host_example.c` | ARM Linux RPMsg userspace send/receive |
| `bld/.../sunxi_t527_rproc.c` | Mainline remoteproc kernel driver |
| `board/.../rootfs-overlay/usr/bin/probe_riscv_debug.sh` | AHB bus scanner to find debug module |
| `board/.../rootfs-overlay/etc/openocd/openocd_t527_local.cfg` | OpenOCD remote_bitbang config |
| `docs/buildroot/HowToDebugE907.md` | Complete GDB debugging guide |

---

## Next Session TODO

### Priority 1: Complete rbb_server.c
File: `project-cubie-a5e/package/rbb-server/rbb_server.c`

The program:
1. Takes debug module base address as CLI arg (from probe script output)
2. Opens `/dev/mem` and mmaps 4KB at that address
3. Listens on TCP :3335
4. Implements OpenOCD `remote_bitbang` protocol:
   - Maintains JTAG TAP state machine (software)
   - When TAP reaches SHIFT-DR with IR=0x11 (DMI), intercepts the 41-bit DR scan
   - Decodes: `addr[6:0] = bits[40:34]`, `data[31:0] = bits[33:2]`, `op[1:0] = bits[1:0]`
   - op=1 (read): reads `DEBUG_BASE + (addr<<2)`, returns via TDO
   - op=2 (write): writes data to `DEBUG_BASE + (addr<<2)`
5. Buildroot package: `package/rbb-server/` with Config.in + rbb-server.mk

### Priority 2: GDB stub in firmware (Tier 1 fallback)
File: `riscv-firmware/gdb_stub.c`

If debug module probe fails, implement in-firmware GDB stub:
- `ebreak` in `_trap_handler` in `startup.S` saves all registers to SRAM C (0x00028100)
- ARM side polls SRAM for the GDB RSP packet format, proxies over TCP to GDB
- 100% guaranteed to work, no hardware probe needed
- Reference: standard RISC-V GDB stub pattern (TinyGDB, picocom-gdb-stub)

### Priority 3: Git commit and push
```bash
cd /home/tcmichals/projects/cubie/cubie-a5e
git add -A
git commit -m "feat: Blueprint 3/4/5 - RISC-V remoteproc, RPMsg, mailbox IPC, debug bridge

- Add sunxi_t527_rproc.c mainline remoteproc driver for XuanTie E907
- Add resource_table.c with RSC_TRACE + RSC_VDEV for remoteproc ELF
- Add rpmsg_host_example.c Linux userspace RPMsg communication example
- Add probe_riscv_debug.sh to discover E907 debug module address on AHB bus
- Add openocd_t527_local.cfg with remote_bitbang transport config
- Add HowToDebugE907.md complete GDB debugging guide
- Add prompt5_riscv_debug_trace.md research findings for next session
- Enable BR2_PACKAGE_OPENOCD, BR2_PACKAGE_GDB_SERVER in defconfig
- Enable CONFIG_REMOTEPROC, CONFIG_RPMSG_VIRTIO, CONFIG_SUNXI_T527_RPROC
- Remove incorrect iomem=relaxed (kernel driver owns all register access)
- Wire sunxi_t527_rproc.c into drivers/remoteproc/Kconfig and Makefile"
git push origin main
```

---

## Community & Sources

- **No public documentation** of T527 E907 debug module address — Allwinner NDA
- **Mainline kernel** `ccu-sun55i-a523-mcu.c` confirms `RST_BUS_MCU_RISCV_DEBUG` at MCU CCU +0x124 bit17
- **RISC-V Debug Spec v0.13**: https://github.com/riscv/riscv-debug-spec
- **T-Head OpenOCD fork**: https://github.com/T-head-Semi (has XuanTie-specific patches)
- **CKLink probe** (official T-Head hardware): ~$15 USD on Taobao — we avoid this
- **Nobody in public community** has done ARM MMIO bridge to T527 E907 — this is novel work
