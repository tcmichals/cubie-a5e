# Bringing Up Heterogeneous RISC-V on Allwinner SoCs (Part 2): Building the Linux `remoteproc` Driver and Hardware Verification Suite

In **[Part 1](part1_heterogeneous_riscv_intro_architecture.md)**, we laid the architectural foundation for the **Allwinner T527 / A527** (`sun55i`) SoC, derived the physical memory map from the Technical Reference Manual (TRM), established the dedicated on-chip SRAM architecture (no ITCM/DTCM), and explored the on-chip memory-mapped debugging paradigm.

In this article (**Part 2**), we move directly into the code and system bring-up:
1. **Building the Linux 7.1 `sunxi_rproc.c` RemoteProc driver** with multi-segment Address Translation Table (ATT) memory routing across Dedicated MCU SRAM (Space 0), Shared PubSRAM (Space 1), and dynamic DDR carveouts.
2. **Exposing live debugfs trace logs** (`/sys/kernel/debug/remoteproc/remoteproc0/trace0`) via `.resource_table` without dedicated UART cables.
3. **Deploying the all-new `riscv-firmware/apps` verification suite** across three distinct hardware profiles to systematically prove co-processor boot, memory subsystems, hardware FPU, exception handling, and high-performance IPC paradigms.

---

## 1. Building the Linux `remoteproc` Driver (`sunxi_rproc.c`)

The Linux Remote Processor (`remoteproc`) framework is the standard kernel subsystem for managing auxiliary microcontrollers on heterogeneous SoCs. It provides standardized lifecycle management, coordinates clock and reset domains, parses standard ELF binaries, and configures IPC.

```text
┌─────────────────────────────────────────────────────────────────┐
│                   Linux User Space Interface                    │
│                                                                 │
│   echo "testBasic.elf" > /sys/class/remoteproc/rproc0/firmware  │
│   echo start           > /sys/class/remoteproc/rproc0/state     │
│   cat /sys/kernel/debug/remoteproc/rproc0/trace0 (Live logs)    │
└────────────────────────────────┬────────────────────────────────┘
                                 │
                                 ▼
┌─────────────────────────────────────────────────────────────────┐
│           Linux Kernel Driver: drivers/remoteproc/sunxi_rproc.c │
│  - struct rproc_ops sunxi_rproc_ops                             │
│  - sunxi_rproc_prepare() (CCF Clocks, Resets, SRAMA3_2 Remap)   │
│  - sunxi_rproc_da_to_va() (ATT Multi-segment translation)       │
└────────────────────────────────┬────────────────────────────────┘
                                 │
       ┌─────────────────────────┼─────────────────────────┐
       ▼                         ▼                         ▼
┌──────────────┐          ┌──────────────┐          ┌──────────────┐
│ SRAM Space 0 │          │ SRAM Space 1 │          │ DDR DRAM     │
│  Dedicated   │          │Shared PubSRAM│          │ Carveouts    │
│ (reg: r_sram)│          │(reg: r_sram1)│          │ (reg: dram)  │
│  256 KB      │          │  256 KB      │          │ (/vdev)      │
└──────────────┘          └──────────────┘          └──────────────┘
```

### 1.1 Multi-Segment Memory Routing (`da_to_va`) via Address Translation Tables (ATT)
The XuanTie E907 RISC-V core on Allwinner A523/A527/T527 SoCs manages complex memory topologies requiring explicit address translation between the co-processor's Device Addresses (DA) and the ARM Host's Physical Addresses (PA). 

Earlier vendor drivers suffered from interconnect shift bugs when translating `0x40000000`, leading to illegal instruction fetches (`0x00000000`) and silicon lockups. To permanently eliminate this class of bug, the driver implements an Address Translation Table (ATT) structure (`sun55i_rproc_att`):

```text
static const struct sunxi_rproc_att sun55i_rproc_att[] = {
	/* dev addr (remote)    , sys addr (host PA)    , size                   , flags */
	/* Space 0 Core Aliases -> Space 0 Host PA */
	{ E907_SRAM_SPACE0_DA,     SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ E907_SRAM_SPACE0_DA_ALT, SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ E907_SRAM_C_DA,          SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },
	{ SUN55I_SRAM_SPACE0_SYS,  SUN55I_SRAM_SPACE0_SYS, SUN55I_SRAM_SPACE0_SIZE, ATT_IOMEM },

	/* Space 1 Core Aliases -> Space 1 Host PA */
	{ E907_SRAM_SPACE1_DA,     SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
	{ E907_SRAM_SPACE1_DA_ALT, SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
	{ SUN55I_SRAM_SPACE1_SYS,  SUN55I_SRAM_SPACE1_SYS, SUN55I_SRAM_SPACE1_SIZE, ATT_IOMEM },
};
```

During ELF firmware loading, `sunxi_rproc_da_to_va()` resolves device addresses declared in ELF headers into mapped host virtual addresses (`va`):
1. **ATT Lookup**: Calls `sunxi_rproc_da_to_sys()` to translate core-local DAs into host system bus PAs based on table bounds.
2. **Mapped Window Match**: Resolves host PAs against mapped Device Tree resources (`r_sram`, `r_sram1`, `dram`, `trace`).
3. **Core Carveout Delegation**: Returns `NULL` for unmapped DDR addresses, cleanly delegating dynamic DMA allocations (such as VirtIO vrings) directly to the framework's internal `rproc->carveouts` list.

```text
void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct sunxi_rproc *priv = rproc->priv;
	u64 sys;

	/* Reject overflow and 0-length mapping requests */
	if (len == 0 || da > U64_MAX - len)
		return NULL;

	/* 1. Translate core-local DA to system bus PA using ATT */
	if (sunxi_rproc_da_to_sys(priv, da, len, &sys, is_iomem) == 0) {
		if (priv->r_sram_va && sys >= priv->r_sram_phys &&
		    (sys + len) <= (priv->r_sram_phys + priv->r_sram_size))
			return (__force void *)(priv->r_sram_va + (sys - priv->r_sram_phys));

		if (priv->r_sram1_va && sys >= priv->r_sram1_phys &&
		    (sys + len) <= (priv->r_sram1_phys + priv->r_sram1_size))
			return (__force void *)(priv->r_sram1_va + (sys - priv->r_sram1_phys));

		if (priv->dram_va && sys >= priv->dram_phys &&
		    (sys + len) <= (priv->dram_phys + priv->dram_size))
			return (__force void *)(priv->dram_va + (sys - priv->dram_phys));

		if (priv->trace_va && sys >= priv->trace_phys &&
		    (sys + len) <= (priv->trace_phys + priv->trace_size))
			return (__force void *)(priv->trace_va + (sys - priv->trace_phys));
	}

	/* 2. Direct Device Tree Memory Regions (Carveout / Fallbacks) */
	if (priv->trace_va && da >= priv->trace_phys &&
	    (da + len) <= (priv->trace_phys + priv->trace_size)) {
		if (is_iomem) *is_iomem = false;
		return (__force void *)(priv->trace_va + (da - priv->trace_phys));
	}

	/* Dynamic DDR carveouts delegated to remoteproc core's rproc->carveouts */
	return NULL;
}
```

### 1.2 Two-Stage CCF Clock, Reset Lifecycle & PubSRAM Remap
Clock gating and reset release are tied directly into the Linux Common Clock Framework (CCF) using a two-stage sequencing model:

1. **`.prepare()`**: Deasserts bus resets (`rst_cfg`, `rst_sram`, `rst_msgbox`) and gates on CCU clocks. It enables the shared secondary SRAM bank (`SRAMA3_2`) for RISC-V MCU access by setting `SUNXI_REMAP_SRAMA3_2_BIT` in the remap control register. Finally, it zeroes the SRAM regions (`memset_io`) to clear ECC/parity noise and initialize `.bss`.
2. **`.start()`**: Enables the crash notification IRQ, writes the boot vector entry point into `STA_ADD_REG` (`0x07130204`) while the core execution reset remains held, and deasserts `rst_core` to begin instruction fetching.

```text
int sunxi_rproc_start(struct rproc *rproc)
{
	struct sunxi_rproc *priv = rproc->priv;
	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
	int ret;

	if (rproc->bootaddr > U32_MAX)
		return -EINVAL;

	/* Enable crash IRQ now that core will execute */
	if (priv->crash_irq > 0 && !priv->crash_irq_enabled) {
		enable_irq(priv->crash_irq);
		priv->crash_irq_enabled = true;
	}

	/* Program boot entry into STA_ADD_REG while core execution reset is held */
	if (priv->cfg_va)
		writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);

	/* Release core execution reset */
	if (priv->rst_core) {
		ret = reset_control_deassert(priv->rst_core);
		if (ret)
			return ret;
	}

	dev_info(priv->dev, "Starting %s core at entry 0x%llx\n",
		 cfg->name ? cfg->name : "remote", (u64)rproc->bootaddr);
	return 0;
}
```

Because this driver executes inside kernel space with native `ioremap_wc()`, **we permanently removed `iomem=relaxed` from our U-Boot `bootargs`**, restoring strict physical memory security (`CONFIG_STRICT_DEVMEM`).

### 1.3 Mainline Invariants & Race Condition Elimination

Upstream kernel maintainers and static analysis bots enforce strict lifecycle and concurrency invariants that were resolved in `sunxi_rproc.c`:

* **Workqueue Initialization vs Mailbox Requests**: `INIT_WORK(&priv->vq_work, ...)` is initialized ahead of all mailbox channel requests to avoid jumping to uninitialized work items on deferred probe.
* **Crash IRQ Teardown Order**: In `sunxi_rproc_remove()`, `disable_irq(priv->crash_irq)` is called *before* `rproc_del()`. This ensures in-flight crash alerts cannot trigger on a destroyed `rproc` pointer.
* **Stack Use-After-Free Prevention**: With asynchronous mailbox delivery (`tx_block = false`), passing local stack pointers risks use-after-return. We assign kicks to `priv->kick_msg = (u32)vqid` inside `struct sunxi_rproc`.
* **PREEMPT_RT Safe VirtIO Dispatch**: Rather than invoking `rproc_vq_interrupt()` directly inside the mailbox interrupt context (which triggers "scheduling while atomic" warnings on real-time kernels), the driver schedules work via `schedule_work(&priv->vq_work)`.

---

## 2. Automatic Trace Logging via `.resource_table`

Needing dedicated serial cables and terminals just to inspect early boot and runtime debug output adds unnecessary friction.

The resource table implementation in `riscv-firmware/common/arch_riscv/resource_table.c` uses a compile-time macro to toggle between trace-only mode and full RPMsg + trace mode:

```text
/* Trace buffer in .trace_buffer section (mapped to on-chip SRAM by linker script) */
__attribute__((used, section(".trace_buffer"), aligned(4)))
char g_rproc_trace_buffer[CONFIG_RPROC_TRACE0_LEN];

#ifdef CONFIG_RPROC_RPMSG
/* Full Resource Table: RSC_TRACE + VirtIO VDev (for /dev/rpmsg0) */
__attribute__((used, section(".resource_table"), aligned(4)))
const struct rpmsg_resource_table global_resource_table = {
    .ver = 1, .num = 2,
    .offset = {
        offsetof(struct rpmsg_resource_table, trace),
        offsetof(struct rpmsg_resource_table, vdev),
    },
    .trace = {
        .type = RSC_TRACE,
        .da   = (uint32_t)&g_rproc_trace_buffer[0],
        .len  = sizeof(g_rproc_trace_buffer),
        .name = CONFIG_RPROC_TRACE0_NAME,  /* "trace0" */
    },
    .vdev = {
        .type          = RSC_VDEV,
        .id            = VIRTIO_ID_RPMSG,
        .num_of_vrings = 2,
        /* da = 0: Linux kernel allocates vrings dynamically */
        .vring = { {.da=0,.align=VRING_ALIGN,.num=VRING_NUM_DESCS},
                   {.da=0,.align=VRING_ALIGN,.num=VRING_NUM_DESCS} },
    },
};
#else
/* Trace-Only Resource Table (default: no RPMsg overhead) */
__attribute__((used, section(".resource_table"), aligned(4)))
const struct standard_resource_table global_resource_table = {
    .ver = 1, .num = 1,
    .offset = { offsetof(struct standard_resource_table, trace) },
    .trace = {
        .type = RSC_TRACE,
        .da   = (uint32_t)&g_rproc_trace_buffer[0],
        .len  = sizeof(g_rproc_trace_buffer),
        .name = CONFIG_RPROC_TRACE0_NAME,
    },
};
#endif
```

When Linux boots the ELF, it parses the table and automatically creates a live debugfs interface on the ARM host:
```bash
# Read live diagnostic logs directly from the running RISC-V core:
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```

---

## 3. The `riscv-firmware/apps` Verification Suite

Under `riscv-firmware/apps/`, seven progressive test applications validate core boot, memory mapping, telemetry, exception handling, and inter-processor communication paradigms:

```text
+------------------------+--------------------------------+-----------+
| App                    | Feature Verified               | Tool      |
+------------------------+--------------------------------+-----------+
| testBasic              | Boot 0x3FFC0000, MISA probe    | trace0    |
| testStringBinaryTrace0 | HW FPU, packed binary telemetry| mon_trace |
| testCrash              | mtvec trap, insn autopsy       | trace0    |
| testPing               | SPSC SRAM + Mailbox Doorbell   | ping_uio  |
| testPingRpmsg          | VirtIO RPMsg /dev/rpmsg0       | ping_rpmsg|
| testDRAMMsg            | SRAM ctrl + 1MB DDR pool + PMP | ping_dram |
| exampleRiscv           | Flight stack telemetry         | trace0    |
+------------------------+--------------------------------+-----------+
```

---

### 3.1 Step 1: Sanity Boot & Memory Writes (`testBasic`)
The `testBasic` application boots into SRAM Space 0 (`0x3FFC0000`), writes initial signatures to memory, reads the hardware `MISA` register, verifies single-precision hardware float multiplication, and runs an incrementing counter loop:

```text
/* apps/testBasic/main.cpp */
int main(void) {
    uint32_t misa = 0;
    asm volatile ("csrr %0, misa" : "=r"(misa));

    sram_c_loc1[0] = 0xDEADBEEF;
    sram_c_loc1[1] = misa;
    sram_c_loc2[0] = 0x52495343; // "RISC"

    hal::Trace::init();
    hal::Timer::init();

    volatile float f_test1 = 12.5f;
    volatile float f_test2 = 4.0f;
    volatile float f_res = f_test1 * f_test2; // Executed on hardware FPU

    uint32_t count = 0;
    while (1) {
        count++;
        sram_c_loc2[1] = count;
        hal::Trace::printf("[testBasic] Heartbeat #%u | MISA=0x%08x | count=%u\n",
                           count, misa, count);
        hal::Timer::delay_ms(1000);
    }
}
```

Reading `/sys/kernel/debug/remoteproc/remoteproc0/trace0` confirms execution on physical silicon without bus hang:
```text
[testBasic] Heartbeat #1 | MISA=0x40901125 | count=1
[testBasic] Heartbeat #2 | MISA=0x40901125 | count=2
```

---

### 3.2 Step 2: Hardware Single FPU & Packed Binary Telemetry (`testStringBinaryTrace0`)
The XuanTie E907 features a hardware single-precision (`F`) floating-point unit (`MISA = 0x40901125`). `testStringBinaryTrace0` computes trigonometric sine values on the FPU and serializes a 32-byte packed binary `TelemetryPacket` alongside formatted ASCII logs:

```text
/* apps/testStringBinaryTrace0/main.cpp */
struct __attribute__((packed)) TelemetryPacket {
    uint32_t header_magic;  // 0x54454C4D ("TELM")
    uint32_t sequence;
    uint32_t uptime_ms;
    float    accel_x;       // Hardware float
    float    accel_y;
    float    accel_z;
    float    sine_wave;     // Hardware float
    uint16_t checksum;
    uint16_t tail_magic;    // 0x55AA
};
```

Running `monitor_trace.py` validates the stream and decodes the packed binary structures in real time.

---

### 3.3 Step 3: Hardware Exception Trapping & Autopsy (`testCrash`)
To debug faults without a JTAG probe, `testCrash` registers a machine-mode exception handler in `mtvec`. After emitting three heartbeats, it deliberately triggers an illegal instruction (`.word 0x00000000`):

```text
/* apps/testCrash/main.cpp */
for (uint32_t i = 1; i <= 3; i++) {
    hal::Trace::printf("[testCrash] Normal Heartbeat #%u / 3\n", i);
    hal::Timer::delay_ms(1000);
}

hal::Trace::puts("[testCrash] >>> Triggering intentional Illegal Instruction fault NOW <<<\n");
asm volatile(".word 0x00000000"); // Unimplemented opcode
```

When the trap triggers, the core dumps all 31 General Purpose Registers and CSRs (`mcause = 0x30000002`) to `trace0` and logs fatal signature `0xDEADF00D` into SRAM (`0x3FFFFF00`) before parking in a clean `wfi` loop. The ARM Linux host remains completely stable.

---

### 3.4 Step 4: Ultra-Low-Latency Shared Memory IPC & UIO Doorbell (`testPing`)
For high-frequency control loops, traditional kernel abstractions introduce scheduling latency. `testPing` implements a zero-copy Single Producer Single Consumer (SPSC) queue in SRAM Space 0 synchronized via **Hardware Mailbox Doorbell interrupts**:

```text
/* apps/testPing/main.cpp */
bool ping_ready = (SHM_CHANNEL->host_doorbell == 1);
if (hal::MsgBox::is_rx_pending(hal::MsgBox::Channel::Channel1)) {
    (void)hal::MsgBox::receive(hal::MsgBox::Channel::Channel1);
    ping_ready = true;
}

if (ping_ready) {
    SHM_CHANNEL->pong_pkt.riscv_cycles = hal::Timer::get_ticks();
    SHM_CHANNEL->riscv_doorbell = 1;
    hal::MsgBox::send(hal::MsgBox::Channel::Channel0, 0x01); // Trigger Linux interrupt
}
```

The host companion tool (`ping_uio`) maps the mailbox through `/dev/uio0` and blocks in `epoll_wait()`, achieving round-trip latency of **13.91 µs** with **0% idle CPU burn**. Direct memory polling via `ping_shm` reaches **13.72 µs**.

---

### 3.5 Step 5: Standard Linux VirtIO RPMsg (`testPingRpmsg`)
When standard Linux networking or terminal abstractions are required, `testPingRpmsg` connects the XuanTie E907 to the mainline `virtio_rpmsg_bus` subsystem:
1. The core advertises `"rpmsg-ping-channel"` over VirtIO vrings.
2. The Linux kernel initializes the channel and exposes `/dev/rpmsg0`.
3. Companion tools (`ping_rpmsg` and `ping_rpmsg.py`) exchange frames using standard file descriptor operations (`open`, `read`, `write`).

---

### 3.6 Step 6: High-Bandwidth Hybrid SRAM / DDR Streaming (`testDRAMMsg`)
For high-throughput payloads (camera frames, point clouds, logging), `testDRAMMsg` demonstrates a **hybrid architecture**:
* Control queues and descriptor rings reside in **zero-wait-state SRAM Space 0**.
* Bulk payload buffers reside in a **1 MB DDR DRAM carveout (`0x48000000`)**.
* The co-processor configures its Physical Memory Protection (PMP) unit for non-cacheable DDR access, maintaining coherency with Linux DMA.
* The `ping_dram` companion tool sustains **4.39 MB/s bidirectional throughput**.

---

## 4. Hardware Overlays & The Autonomous SSH Test Harness

In **Part 1**, we compiled several custom Device Tree Overlays (`.dtbo`) to dynamically reconfigure the T527’s memory map and hardware mailbox routing. Because Linux `remoteproc` strictly relies on the active Device Tree to allocate DMA carveouts (like VirtIO vrings) and bind hardware mailboxes, **you cannot test different memory topologies just by swapping `.elf` files.** 

To execute the full verification suite autonomously, we built `run_full_sweep.py`. This Python script runs on your host development PC and uses `ssh` and `sshpass` to orchestrate the entire multi-profile validation loop on the live Radxa Cubie A5E hardware. Furthermore, it operates a parallel `SerialLogger` thread that continuously records the target's serial console to diagnose if the board ever hangs during a reboot or encounters an early kernel panic.

Here is how the automated harness bridges our custom Device Trees with the `remoteproc` verification suite:

1. **Remote Device Tree Reconfiguration:** Using `sed` over SSH, the script dynamically injects the appropriate `dtoverlay=` and `cmdline=` statements into the target's `/boot/config.txt` for each specific test profile.
2. **Autonomous Reboots:** After swapping the overlay, it issues a `reboot` command and continuously polls the target over SSH until the OS is back online.
3. **Firmware Execution:** Once the board is online, it interacts with the `sysfs` remoteproc interface to `stop` the core, load the correct `.elf` firmware, and `start` execution.
4. **Benchmarking & Parsing:** Finally, it launches the host-side benchmarking tools (`ping_rpmsg`, `ping_shm`, etc.), parses the stdout latency metrics, and generates the quantitative JSON and Markdown summaries.

The script cycles through three primary overlay profiles:

```text
+----------+----------------------------------+---------------------------+
| Profile  | dtoverlay=                       | Firmware Apps             |
+----------+----------------------------------+---------------------------+
| P1: DDR  | cubie-a5e-flight-stack           | testBasic, testCrash,     |
|          |                                  | testPingRpmsg, testDRAMMsg|
| P2: SRAM | ...-flight-stack ...-rpmsg-sram  | testPingRpmsgSram         |
| P3: UIO  | ...-flight-stack ...-testPing    | testPing (ping_shm+uio)   |
+----------+----------------------------------+---------------------------+
```

**💡 Pro-Tip (Switching Topologies):**  
Switching firmware *within* the same topology profile requires **zero reboots**. However, to run the complete 3-profile sweep, you must edit `/boot/config.txt` and reboot between profiles so the kernel initializes the distinct DMA pools.

---

## 5. Measured Silicon Benchmarks (Autonomous 3-Profile Sweep)

The entire suite was executed against physical silicon on the Radxa Cubie A5E (`192.168.1.33`, Linux 7.1 PREEMPT_RT). All 1,000-packet runs recorded 100% success with zero data corruption:

```text
Latency & Throughput:
+------------------+--------------------+-------------+----------------+
| Profile          | Tool               | Avg RTT     | Throughput     |
+------------------+--------------------+-------------+----------------+
| P1: DDR VirtIO   | ping_rpmsg (C++)   | 191.45 us   | 2,996 msgs/s   |
| P1: DDR VirtIO   | ping_rpmsg.py      | ~124 us     | 5,710 msgs/s   |
| P1: Hybrid DDR   | ping_dram (C++)    | 202.10 us   | 4,499 msgs/s   |
| P2: SRAM VirtIO  | ping_rpmsg (C++)   | 135.62 us   | 7,338 msgs/s   |
| P2: SRAM VirtIO  | ping_rpmsg.py      | ~98 us      | 6,400 msgs/s   |
| P3: SPSC  (BEST) | ping_shm (C++)     | 13.72 us    | 66,317 msgs/s  |
| P3: UIO   (BEST) | ping_uio (C++)     | 13.91 us    | 63,215 msgs/s  |
| P3: UIO          | ping_uio.py        | 180.49 us   | 4,467 msgs/s   |
+------------------+--------------------+-------------+----------------+

Bandwidth & Integrity:
+------------------+--------------------+------------+----------------+
| Profile          | Tool               | Bandwidth  | Result         |
+------------------+--------------------+------------+----------------+
| P1: DDR VirtIO   | ping_rpmsg (C++)   | 2.84 MB/s  | PASS 0 errors  |
| P1: DDR VirtIO   | ping_rpmsg.py      | 713.8 KB/s | PASS 0 errors  |
| P1: Hybrid DDR   | ping_dram (C++)    | 4.39 MB/s  | PASS 0 errors  |
| P2: SRAM VirtIO  | ping_rpmsg (C++)   | 6.94 MB/s  | PASS 0 errors  |
| P2: SRAM VirtIO  | ping_rpmsg.py      | 800.0 KB/s | PASS 0 errors  |
| P3: SPSC  (BEST) | ping_shm (C++)     | 64.76 MB/s | PASS 0 errors  |
| P3: UIO   (BEST) | ping_uio (C++)     | Doorbell   | PASS 0 errors  |
| P3: UIO          | ping_uio.py        | Doorbell   | PASS 0 errors  |
+------------------+--------------------+------------+----------------+
```


Moving the VirtIO vrings and buffers from external DDR (Profile 1) to on-chip SRAM Space 1 (Profile 2) drops average round-trip latency from **191.45 µs down to 135.62 µs** and doubles bidirectional throughput. For ultra-low latency loops, the direct SRAM SPSC engine (Profile 3) hits **13.72 µs** at over 66,000 messages per second.

---

## 6. In-Kernel Unit Testing (KUnit): 68/68 Tests Passing

Upstream kernel acceptance demands verifying edge cases and error unwinding paths that physical hardware cannot easily trigger.

We implemented exhaustive KUnit test suites directly in the Linux tree under `CONFIG_SUNXI_REMOTEPROC_KUNIT_TEST` and `CONFIG_SUN55I_MSGBOX_KUNIT_TEST`:

```text
┌────────────────────────────────────────────────────────────────────────┐
│                        Linux Kernel KUnit Framework                    │
├──────────────────────────────────┬─────────────────────────────────────┤
│  sunxi_rproc_test.c (34 Tests)   │  sun55i_msgbox_test.c (34 Tests)    │
├──────────────────────────────────┼─────────────────────────────────────┤
│ • DA -> VA Address Translation   │ • 12-Channel Routing Table Sweep    │
│ • 64-bit Integer Overflow Guards │ • Invalid Index Clamping (-1, 12)   │
│ • Mock MMIO Lifecycle (prepare)  │ • Register Offset & Bitmask Formulas│
│ • Mock MMIO Lifecycle (start)    │ • Mock MMIO send_data (All Channels)│
│ • Mock MMIO Lifecycle (stop)     │ • FIFO Status Full Sweep (0..15)    │
│ • Workqueue & Mailbox kick() UAF │ • Stale FIFO Purging on Startup     │
│ • NULL is_iomem pointer safety   │ • Bounded Loop Hardirq Anti-Lockup  │
│ • Cross-Space Memory Isolation   │ • Channel Crosstalk Isolation       │
│ • Complete rproc_ops Integrity   │ • Simultaneous 3-Route Concurrency  │
└──────────────────────────────────┴─────────────────────────────────────┘
Total: 68 Test Cases across In-Kernel Drivers (100% PASS)
```

Running the test suite on target silicon validates all 68 assertions cleanly:
```bash
cat /sys/kernel/debug/kunit/sunxi_rproc/results
# 1..34
# ok 1 sunxi_rproc_da_to_va_sram0
# ...
# [PASS] 34/34 passed

cat /sys/kernel/debug/kunit/sun55i_msgbox/results
# 1..34
# ok 1 sun55i_msgbox_send_data
# ...
# [PASS] 34/34 passed
```

---

## 7. What's Next in Part 3

With `sunxi_rproc.c`, `sun55i-msgbox.c`, and our multi-profile test infrastructure proven on silicon:
1. The Linux host reliably loads multi-segment ELF binaries across Dedicated SRAM, Shared PubSRAM, and DDR carveouts via Address Translation Tables.
2. Live debugfs trace streaming eliminates the need for serial cables.
3. 68 in-kernel KUnit tests protect address translation, bounded interrupt loops, and teardown ordering.
4. The co-processor delivers proven latencies ranging from **191 µs** (standard VirtIO) down to **13.7 µs** (direct SRAM SPSC).

In **[Part 3](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**, we dive into the co-processor firmware implementation:
* **The Shared Memory SPSC Queue** (`testPing`): Zero-copy ring buffers and UIO signaling.
* **VirtIO RPMsg Implementation** (`testPingRpmsg`): Structuring `.resource_table` for automatic Linux character device bindings.
* **Hybrid SRAM/DDR Streaming** (`testDRAMMsg`): Managing descriptor rings and cache coherency for high-bandwidth payloads.

---

### Series Navigation
* **[Part 1: Architecture and Memory-Mapped Debugging](part1_heterogeneous_riscv_intro_architecture.md)**
* **Part 2: Building the Linux `remoteproc` Driver and Hardware Verification Suite** *(You are here)*
* **[Part 3: Inter-Processor Communication (IPC) Deep Dive](part3_baremetal_firmware_ipc_and_coroutines_intro.md)**