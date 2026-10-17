# Allwinner A523 / A527 / T527 (sun55i) Message Box — Hardware Reference

The one place for how the message box hardware works. Driver rules live in
[LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md](../architecture/LINUX_REMOTEPROC_AND_MAILBOX_DRIVER_GUIDE.md);
decisions in [DESIGN_DECISIONS.md](../../upstream-remoteproc/DESIGN_DECISIONS.md).

Every fact is tagged with where it comes from:

| Tag | Source |
|---|---|
| **[silicon]** | measured or read back on our Cubie A5E |
| **[vendor]** | Allwinner's driver in Radxa's BSP: `github.com/radxa/allwinner-bsp`, branch `cubie-aiot-v1.4.6`, `drivers/msgbox/sunxi-msgbox.c` (`sun55iw3` hardware data) |
| **[ours]** | our drivers: `linux-cubie` `drivers/mailbox/sun55i-msgbox.{c,h}`, firmware `firmware/common/hal/msgbox.cpp` |
| **[datasheet]** | A733 Datasheet V0.93 §3.9.5 (overview only, no register text) |
| **[unverified]** | stated elsewhere, not yet checked here |

The T527 User Manual V0.92 (register chapter) has not been checked yet; where it disagrees with
this page, the manual and a silicon readback win.

---

## 1. Model

- Four processors: **ARM** (Cortex-A55 host), **DSP** (HiFi4), **CPUS** (power core), **RV**
  (XuanTie E906). Processor IDs ARM 0, DSP 1, CPUS 2, RV 3. **[vendor]**
- Each processor has **its own register bank**. A sender writes the message into the
  **receiver's** bank; the receiver reads it from its own bank. **[vendor] [ours] [silicon]**
- Between two processors there are **4 channels**, each with an **8 × 32-bit FIFO** in each
  direction. **[datasheet] [vendor]**
- A bank is split into **ports** `n` (one per remote processor, `0x100` apart) and channels `p`
  (0–3) within a port. **[vendor] [ours]**

| Bank | Base | GIC line | Notes |
|---|---|---|---|
| ARM | `0x03003000` | SPI 0 | Linux's bank. SPI 0 verified on silicon. **[silicon]** |
| DSP | `0x07120000` | SPI 1 **[vendor DT]** | **Never reaches the A55** (§6b) **[silicon]** |
| CPUS | `0x07094000` | SPI 181 **[vendor DT]** | **Never reaches the A55** (§6b) **[silicon]** |
| RV | `0x07136000` | SPI 174 **[vendor DT]** | **Never reaches the A55** (§6b) **[silicon]**. E906 side: Tina requests `IRQ_MSGBOX = MAKE_IRQn(17, 0)` on the E906's own controller **[Tina T527]** |

## 2. Routing from the ARM side (Linux channel numbers)

| Linux channel | ARM port `n` | Remote | Remote port | **[vendor] = [ours]** |
|---|---|---|---|---|
| 0–3 | 0 | CPUS | 0 | |
| 4–7 | 1 | DSP | 0 | |
| 8–11 | 2 | RV (E906) | 2 | remoteproc uses channel 9 (`p = 1`) |

The vendor `sun55iw3` routing table (`to_coef_n` / `to_remote_id`) and our
`sun55i_msgbox_arm_routes[]` give the same mapping.

On the E906, the ARM link is its bank's port 2 (`RV base + 0x200`); its `MsgBox::Channel0..3`
are `p` 0–3 of that port. **[ours]**

## 3. Registers (per bank; `n` = port, `p` = channel)

Offsets from the bank base; `OFF(n) = 0x100 * n`. **[vendor] [ours]**

| Offset | Name | Bits | Meaning |
|---|---|---|---|
| `0x20 + OFF(n)` | READ_IRQ_ENABLE | bit `2p` | enable "message waiting" interrupt for channel `p` |
| `0x24 + OFF(n)` | READ_IRQ_STATUS | bit `2p`, write 1 to clear | message waiting. Can only be cleared once the FIFO is empty **[vendor comment]** |
| `0x30 + OFF(n)` | WRITE_IRQ_ENABLE | bit `2p + 1` | enable the write interrupt for channel `p` |
| `0x34 + OFF(n)` | WRITE_IRQ_STATUS | bit `2p + 1`, write 1 to clear | write interrupt pending |
| `0x50 + OFF(n) + 4p` | FIFO_STATUS | | defined by both drivers, used by neither |
| `0x60 + OFF(n) + 4p` | MSG_STATUS | `[3:0]` | number of messages in the FIFO (0–8) |
| `0x70 + OFF(n) + 4p` | MSG_FIFO | 32 | write = send (into the receiver's bank), read = pop |
| `0x80 + OFF(n) + 4p` | WRITE_IRQ_THRESHOLD | `[1:0]` | write IRQ fires when **free** slots reach: `0` = 1, `1` = 2, `2` = 4, `3` = 8 **[vendor] [silicon §6a]**. Our 7.1 driver writes `0` (`WR_IRQ_THR_ONE_FREE`) in `allwinner,tx-done-irq` mode. **[ours]** |

## 4. Interrupts

- **Read interrupt:** raised in the **receiver's** bank when a message is waiting, on that
  bank's GIC line. Linux takes the ARM bank's (SPI 0). **[silicon]**
- **Write interrupt:** its enable, status and threshold live in the **receiver's** bank too, so
  for messages Linux sends to the E906 it would be raised on the **RV bank's line**, not on
  SPI 0. The vendor driver requests all four bank interrupts for this reason. **[vendor]**
  **Measured 2026-10-17: no remote bank's interrupt reaches the A55, read or write (§6b).** A
  bank interrupts only the processor that owns it. **[silicon]**
  Our 7.1 driver requests SPI 0, plus the three remote bank lines (SPI 1, 181, 174) only when
  the DT has `allwinner,tx-done-irq`. **[ours]**
- Vendor comment on the write interrupt: *"triggered if the empty level in FIFO reaches the write
  IRQ threshold. It means that there is empty space in FIFO."* **[vendor]**
- Both are level interrupts (`IRQ_TYPE_LEVEL_HIGH`). The vendor enables the write interrupt only
  at channel startup and disables it in its handler and at shutdown; its send path does not
  re-enable it. As we read it, after the first TX-done interrupt it stays off, so later sends
  would never be reported done — possibly why the option is off by default. **[vendor, our
  reading, not tested]** An interrupt-driven TX-done would have to enable it on every send.
  Re-read 2026-10-17 (§5a): `sunxi_msgbox_send_data()` only checks for a full FIFO and writes;
  the only `WRITE_IRQ_ENABLE` set is in `sunxi_msgbox_startup()`.

## 5. "Was my message read?" — TX completion

There is no interrupt named "message read". The options:

| Method | Meaning | Who uses it |
|---|---|---|
| Poll the receiver's `MSG_STATUS` until 0 | receiver has read everything | **[ours, v2 / v3 draft]**: `last_tx_done()` returned `count == 0`, mailbox core polls every **1 ms**. Replaced 2026-10-16 (next row). Upstream `sun6i-msgbox` (older Allwinner) polls every 5 ms with the same idea (receiver's IRQ status). |
| Poll the receiver's `MSG_STATUS` until `< 8` | FIFO has room for the next message | **[vendor default]**: poll every **5 ms**. **[ours, 7.1, 2026-10-16]**: poll every 1 ms; owner decision, DESIGN_DECISIONS "Message box TX completion" |
| Write IRQ, threshold 1 | FIFO has room for the next message (*not* "read") | **[vendor]**, optional: `CONFIG_AW_MAILBOX_SUPPORT_TXDONE_IRQ`, no default (off), see §5a. **[ours, 7.1]**: `allwinner,tx-done-irq`, enabled only while the FIFO is full. **Cannot work from the A55: the write IRQ of the E906's bank never reaches the GIC (§6b) [silicon].** |
| Write IRQ, threshold 8 | all 8 slots free = receiver has read everything — the same meaning as our poll | nobody yet. Behaviour on our silicon **[unverified]** |

The poll only delays a message that is queued behind one the receiver has not read yet; an idle
channel sends at once. Measured after the 2026-10-15 firmware fix: one message at a time, RTT
jitter 7–8 µs, no 1 ms stalls **[silicon]**. With 16 messages in flight each takes ~1.3 ms end to
end (`stress_rpmsg`), which may or may not be the poll — not measured yet (the board kernel has
no tracing).

### 5a. Vendor TX completion, read from source (2026-10-17) [vendor]

Source: `old-kernels/linux-aw2501/.git/modules/bsp` (Radxa `allwinner-bsp`, branch
`cubie-aiot-v1.4.6`, commit `2045a3ca2a01`), `drivers/msgbox/sunxi-msgbox.c`. One driver serves
the T527 (`allwinner,sun55iw3-msgbox`) and the A733 (`allwinner,sun60iw2-msgbox`, §9).

```c
#if IS_ENABLED(CONFIG_AW_MAILBOX_SUPPORT_TXDONE_IRQ)
	chip->controller.txdone_irq	= true;
#else
	chip->controller.txdone_irq	= false;
	chip->controller.txdone_poll	= true;
	chip->controller.txpoll_period	= 5;
#endif
```

- **Default (option off): timer.** The mailbox core polls `last_tx_done()` every **5 ms**; done
  means `count < 8` ("the FIFO is not full"). No TX interrupt.
- **Option on: ISR only**, no timer (`last_tx_done = NULL`). The write IRQ (threshold 1) is
  enabled once in `startup()`; `sunxi_msgbox_write_handler()` disables it, calls
  `mbox_chan_txdone()`, then clears the pending bit. Nothing re-enables it (§4), so as we read it
  the channel stalls on the first message that has to wait for TX done.
- `send_data()` refuses a full FIFO (`-EBUSY`, "FIFO is full"), like ours.
- The handler serves every bank line and always returns `IRQ_HANDLED`, so a line it does not
  own (open question 2) would never trip the kernel's spurious-interrupt check. Ours returns
  `IRQ_NONE` when no enabled write bit is pending.
- **Nobody ships the option on.** No vendor defconfig sets `CONFIG_AW_MAILBOX_SUPPORT_TXDONE_IRQ`
  or even `CONFIG_AW_MSGBOX` (`configs/linux-{5.15,6.1,6.6}/sun55iw3p1_*`, `sun60iw2p1_*`).
  Radxa's shipped A733 kernel has `# CONFIG_AW_MSGBOX is not set`
  (`vendor-a733-reference/boot_configs/config-5.15.147-21-a733`). The vendor ISR path has no
  known silicon run on either SoC.

| | Vendor default | Vendor ISR option | Ours, 7.1 timer | Ours, 7.1 `allwinner,tx-done-irq` |
|---|---|---|---|---|
| Done when | `count < 8` | write IRQ (1 slot free) | `count < 8` | `count < 8`, or write IRQ when full |
| Timer | 5 ms | none | 1 ms | 1 ms, kept |
| Write IRQ enabled | never | once, at startup | never | only while the FIFO is full |
| Lines requested | all banks | all banks | SPI 0 | SPI 0 + remote banks |

## 6. Clocks and resets — every bank has its own

| Bank | Clock | Reset | Clock controller |
|---|---|---|---|
| ARM | `CLK_BUS_MSGBOX` | `RST_BUS_MSGBOX` | main CCU |
| RV (E906) | `CLK_BUS_MCU_RISCV_MSGBOX` | `RST_BUS_MCU_RISCV_MSGBOX` | MCU CCU, register `0x128` (gate bit 0, reset bit 16) |
| DSP | `CLK_BUS_MCU_DSP_MSGBOX` | `RST_BUS_MCU_DSP_MSGBOX` | MCU CCU, register `0x068` |
| CPUS | `CLK_BUS_R_MSGBOX` | `RST_BUS_R_MSGBOX` | R CCU |

**[ours: clock driver tables]**

**Finding (2026-10-16) [silicon]:** our DT gives the message box only the ARM bank's clock and
reset (decision D12). Nobody enables the others, and the kernel turns unused clocks off at boot,
so the RV, DSP and CPUS banks are gated and held in reset: from the A55 every register of those
banks reads 0 (MCU CCU `0x128` = 0; clock summary: `bus-riscv-msgbox` enable count 0). With
`0x128` set to `0x00010001` by hand the RV bank answers like the ARM bank (`0x010 = 0x00020000`,
`0x034 = 0xAA`). Consequences:

- **Linux's doorbells to the E906 never arrived.** The kicks are written into the gated bank.
  rpmsg still worked because the E906 firmware polls the vrings and never waits for a kick.
- **`last_tx_done()` always reported "done" at once**: it reads the remote bank's count, and a
  gated bank reads 0. The 1 ms TX-done poll never actually waited.
- **Why the vendor kernel did not hit this** (most likely, not proven for the T527):
  - **In Allwinner's design each processor's RTOS enables its own bank.** The vendor RTOS HAL's
    `hal_msgbox_init()` deasserts the reset and enables the clock of the bank it lives in
    (`RST_BUS_MSGBOX1` / `CLK_BUS_MSGBOX1` on the sun8iw20 DSP), then requests that bank's
    interrupt. Linux only ever enables the ARM bank. Our E906 firmware is bare metal and never
    did this, so the RV bank stayed off. **[vendor: Allwinner `sunxi-hal`
    `msgbox_amp/msgbox_amp.c`, as carried in RT-Thread; sun8iw20 (D1) copy, the T527 Tina
    FreeRTOS source has not been obtained]** See
    [ALLWINNER_TINA_RTOS_MSGBOX.md](ALLWINNER_TINA_RTOS_MSGBOX.md).
  - The vendor T527 DT (`allwinner-bsp` `configs/linux-5.15/sun55iw3p1.dtsi`) also gives the
    message box only the ARM bank's clock and reset (`CLK_MSGBOX0`, `RST_BUS_MSGBOX0`), and its
    `e906_rproc` node only the SRAM, core and config clocks. **[vendor]**
  - The vendor's RV message box gate (`riscv-msg`, MCU CCU `0x128` bit 0, reset bit 16) has no
    flags, so nothing protects it from being switched off as unused. **[vendor:
    `drivers/clk/sunxi-ng/ccu-sun55iw3-mcu.c`]**
  - The vendor kernel boots with **`clk_ignore_unused`** (Radxa A733 image `extlinux.conf`), which
    skips the switch-off of unused clocks, so gates the boot chain enabled stay on. The T527
    image's command line and boot chain have not been checked. On our board even the reset bit of
    `0x128` was 0, i.e. nothing in our boot chain ever touched it.
  - The vendor A733 DT (`allwinner,sun60iw2-msgbox`, two banks) likewise lists one clock.

D12 has to be revisited: the message box needs the remote banks' clocks and resets too (at least
the RV bank, for the E906). Two ways: Linux enables every bank (the 7.1 fix, 2026-10-16: the
binding lists four clocks and resets, `arm`, `dsp`, `cpus`, `rv`), or each remote firmware
enables its own bank as the vendor RTOS does. The Linux way does not depend on the firmware;
the owner decides which goes into v3.
- With the bank bound to `generic-uio`, nothing enables the clock: writes are dropped and every
  register reads 0, so no interrupt ever fires (`bus-msgbox` enable count 0). **[silicon]**
  The project-only `sun55i-msgbox-uio` driver keeps it on.

## 6a. Write-IRQ status, measured step by step (2026-10-16) [silicon]

Test: `firmware/riscv-firmware/apps/testMsgboxStep/` (E906 pops exactly one message per command)
plus `msgbox_step_test.sh` on the board. Linux channel 11 (E906 bank, port 2, channel 3), RV bank
clock enabled by hand, write-IRQ enable left off (status bits only).

| Threshold field | Status set when | Measured |
|---|---|---|
| 0 (1 free) | count ≤ 7 | ✓ |
| 1 (2 free) | count ≤ 6 | ✓ |
| 2 (4 free) | count ≤ 4 | ✓ |
| 3 (8 free) | count = 0 | ✓ |

- Status = 1 exactly when free slots (8 − count) ≥ threshold, both while filling and draining.
- **Level, not edge:** writing 1 to clear has no effect while the condition holds (it reads 1 again
  at once); it clears once the condition is false. A driver must mask it in its handler.
- At threshold 8 the bit was set by the E906 reading the last message (raw read 1 without a
  clear in between) — the "receiver has read everything" event.
- The A55 cannot pop the E906's FIFO: reading `MSG_FIFO` of the remote bank returns 0 and leaves
  the count unchanged.
- Writing into a full FIFO corrupts it: the count went 9 … 15, then wrapped to 0. Never write a
  full FIFO (our driver and firmware check first).
- The interrupt line itself: see §6b — it does not reach the A55.

## 6b. Remote bank interrupts never reach the A55 (2026-10-17) [silicon]

Board: Cubie A5E, 7.1 kernel from the 2026-10-17 `bld.a5e` build, overlay `cubie-a5e-msgbox-txirq`
(driver requested SPI 1, 181, 174 as `3003000.mailbox-tx1/2/3`, GIC IDs 33, 213, 206), all four
bank clocks on (MCU CCU `0x128` = `0x00010001`). Scripts:
`firmware/riscv-firmware/apps/testMsgboxStep/msgbox_{txirq_test,bank_irq_probe,gic_pending_probe}.sh`.

1. **Write IRQ, E906 bank, channel 11** (port 2, `p` = 3; E906 running `testMsgboxStep`):
   FIFO filled to 8, threshold 0, write status bit set (`WR_STATUS = 0xAA`), enable set
   (`WR_EN = 0x80`), then the E906 popped one (8 → 7). `mailbox-tx3`: **0 interrupts**; the
   enable was never cleared, so the handler never ran.
2. **Read IRQ of each remote bank** (message waiting, `RD_STATUS` bit set, read enable pulsed):
   DSP, CPUS and RV banks — **0 interrupts** on SPI 1, 181 and 174; `/proc/irq/*/spurious` 0.
3. **GIC distributor** (`0x03400000`, `GICD_ISPENDR1..9`, GIC IDs 32–319) read before, while
   and after the RV bank's read interrupt was asserted: **no line went pending**.

So a bank's interrupt goes only to the processor that owns the bank (the RV bank to the E906),
matching the vendor RTOS model in which each core requests its own bank's interrupt
([ALLWINNER_TINA_RTOS_MSGBOX.md](ALLWINNER_TINA_RTOS_MSGBOX.md)). The SPI 1 / 181 / 174 entries
in the vendor DT are never delivered to the A55 on this board. Consequences:
- **TX done from the A55 can only be polled.** A write-IRQ ("ISR") TX-done mode cannot work for
  messages Linux sends; this includes `allwinner,tx-done-irq` and the vendor's compiled-out option.
- The E906's own read interrupt cannot disturb Linux (open question 2 is closed).
- Not checked against the T527 User Manual (register chapter not on hand).

### Timer mode on the same board (2026-10-17)

`ping_rpmsg -n 10000 -D 0`, Profile 1, `last_tx_done()` = `count < 8`, 1 ms poll, all banks clocked:

| E906 firmware | Result | Avg / jitter | Queue overflows (`MBOX_TX_QUEUE_LEN`) | SPI 0 interrupts | E906 FIFO count during the run |
|---|---|---|---|---|---|
| `testPingRpmsg.elf` from 2026-10-12 (stale, on the board's rootfs) | 10000/10000 | 786 / 766 µs | 1,598 | 71,845 | stuck at 8 |
| current build (2026-10-16, drains every doorbell) | 10000/10000, integrity PASS | **171 / 10.9 µs** | **0** | **10,000** (1 per message) | 0 or 1 |

The read-pending fix (drain, then clear) gives exactly one interrupt per message. With the current
firmware the E906's FIFO never fills, so the poll never waits. The rootfs still ships the 10-12
firmware, which leaves kicks in the FIFO; it must be rebuilt into the image.

## 7. How each user drives it

| User | Interrupts | TX completion | RX |
|---|---|---|---|
| Linux `sun55i-msgbox` (v3 series as of 2026-10-15) | ARM bank only (SPI 0) | poll `count == 0`, 1 ms | clear pending, then drain ≤ 8 under the lock |
| Linux `sun55i-msgbox` (7.1, 2026-10-16, uncommitted) | SPI 0; + remote bank lines with `allwinner,tx-done-irq` | poll `count < 8`, 1 ms; write IRQ while full (optional) | drain ≤ 8, then clear pending |
| Vendor `sunxi-msgbox` | all four bank lines | poll 5 ms, or write IRQ threshold 1 | drain until empty (10 ms time limit), then clear pending |
| E906 firmware HAL | none (polls) | doorbell waits for FIFO space | drains every queued doorbell each pass |
| UIO profile (`sun55i-msgbox-uio`) | ARM bank (SPI 0) to userspace | E906 sends a doorbell only when Linux's FIFO is empty | userspace drains, clears pending, unmasks |

## 8. Open questions

1. ~~Does the write IRQ status at threshold 8 follow the receiver draining the FIFO?~~ Yes, §6a.
   ~~Does the A55 actually receive it on SPI 174 once enabled?~~ **No** (2026-10-17, §6b): no
   remote bank interrupt, read or write, reaches the A55 GIC.
2. ~~Does SPI 174 also fire for the E906's *read* interrupt if the E906 enables it?~~ **No**
   (§6b): the RV bank's read interrupt does not reach the A55 either.
3. Is the 1 ms poll the limit for 16-in-flight rpmsg throughput? Needs kernel tracing
   (`txdone_hrtimer` count).
4. ~~D13: clear the read pending bit before or after draining?~~ Measured 2026-10-16: our driver
   clears it **before** draining, which the hardware ignores while the FIFO is not empty, so the
   interrupt fires a second time: **20,000 SPI 0 interrupts for 10,000 messages** (ping and stress
   alike). `sun6i-msgbox`, OMAP, the vendor Linux driver and the vendor RTOS HAL all drain first,
   then clear ("The IRQ can be cleared only once the FIFO is empty"). Fixed on 7.1 (2026-10-16). A message arriving after the clear sets the bit again,
   so draining first loses nothing.
5. `FIFO_STATUS` (`0x50`): meaning unknown.

## 9. A733 (sun60i, Cubie A7A): same block, two banks (2026-10-17) [vendor]

Same message box IP and register map as above (FIFO depth 8, 4 channels per pair, the same read
and write IRQ registers and threshold field), driven by the same vendor driver. The wiring differs:

| | T527 / A523 (A5E) | A733 (A7A) |
|---|---|---|
| Vendor compatible | `allwinner,sun55iw3-msgbox` | `allwinner,sun60iw2-msgbox` (hardware data shared with `sun55iw5`) |
| Banks | 4: ARM `0x03003000`, DSP `0x07120000`, CPUS `0x07094000`, RV `0x07136000` | 2: ARM `0x03004000`, CPUS `0x07094000` |
| Linux channels | 12 | 4 |
| Bank interrupts | SPI 0, 1, 181, 174 | SPI 0, 211 |
| Coprocessor on the other end | E906 (RV bank) | E902, which is the CPUS core (it runs `scp.fex`), so the CPUS bank |
| Clocks in vendor DT | one (`CLK_MSGBOX0`) | one (`CLK_MSGBOX0`) |
| Vendor DT status | enabled | `status = "disabled"` |

Sources: vendor `configs/linux-5.15/sun55iw3p1.dtsi` and `sun60iw2p1.dtsi`, `sun55iw5_hwdata`
in `sunxi-msgbox.c`; Radxa's shipped A733 kernel does not build the driver (§5a).

For our tree:
- On the A733, TX done for messages to the E902 would come on **SPI 211** (CPUS bank line).
- `sun55i-msgbox.c` has the 4-bank T527 routing table built in. The A733 needs its own compatible
  and per-SoC data (2 banks, 4 channels, its own clock/reset list), in line with the "A733
  placeholders stay" decision.
- The current `sun60i-a733-cubie-a7a.dts` node is an untested stub: 2 `reg` entries without
  `reg-names`, only SPI 211 (missing SPI 0, the ARM bank line where Linux receives), no
  `clock-names`/`reset-names`, and a `sun6i-a31-msgbox` fallback. It must be redone from the
  vendor dtsi before use.
