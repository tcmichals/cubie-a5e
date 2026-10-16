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
| DSP | `0x07120000` | SPI 1 | **[vendor DT]** |
| CPUS | `0x07094000` | SPI 181 | **[vendor DT]** |
| RV | `0x07136000` | SPI 174 | E906 side: PLIC IRQ 25 **[unverified]** |

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
| `0x80 + OFF(n) + 4p` | WRITE_IRQ_THRESHOLD | `[1:0]` | write IRQ fires when **free** slots reach: `0` = 1, `1` = 2, `2` = 4, `3` = 8 **[vendor]**. Not defined in our driver. |

## 4. Interrupts

- **Read interrupt:** raised in the **receiver's** bank when a message is waiting, on that
  bank's GIC line. Linux takes the ARM bank's (SPI 0). **[silicon]**
- **Write interrupt:** its enable, status and threshold live in the **receiver's** bank too, so
  for messages Linux sends to the E906 it is raised on the **RV bank's line (SPI 174)**, not on
  SPI 0. The vendor driver requests all four bank interrupts for this reason. **[vendor]**
  Our driver requests only SPI 0. **[ours]**
- Vendor comment on the write interrupt: *"triggered if the empty level in FIFO reaches the write
  IRQ threshold. It means that there is empty space in FIFO."* **[vendor]**
- Both are level interrupts (`IRQ_TYPE_LEVEL_HIGH`). The vendor enables the write interrupt only
  at channel startup and disables it in its handler and at shutdown; its send path does not
  re-enable it. As we read it, after the first TX-done interrupt it stays off, so later sends
  would never be reported done — possibly why the option is off by default. **[vendor, our
  reading, not tested]** An interrupt-driven TX-done would have to enable it on every send.

## 5. "Was my message read?" — TX completion

There is no interrupt named "message read". The options:

| Method | Meaning | Who uses it |
|---|---|---|
| Poll the receiver's `MSG_STATUS` until 0 | receiver has read everything | **[ours]**: `last_tx_done()` returns `count == 0`, mailbox core polls every **1 ms** (`txpoll_period = 1`). Upstream `sun6i-msgbox` (older Allwinner) polls every 5 ms with the same idea (receiver's IRQ status). |
| Write IRQ, threshold 1 | FIFO has room for the next message (*not* "read") | **[vendor]**, optional: `CONFIG_AW_MAILBOX_SUPPORT_TXDONE_IRQ`, no default (off); otherwise the vendor polls every **5 ms** |
| Write IRQ, threshold 8 | all 8 slots free = receiver has read everything — the same meaning as our poll | nobody yet. Behaviour on our silicon **[unverified]** |

The poll only delays a message that is queued behind one the receiver has not read yet; an idle
channel sends at once. Measured after the 2026-10-15 firmware fix: one message at a time, RTT
jitter 7–8 µs, no 1 ms stalls **[silicon]**. With 16 messages in flight each takes ~1.3 ms end to
end (`stress_rpmsg`), which may or may not be the poll — not measured yet (the board kernel has
no tracing).

## 6. Clocks and resets

- One bus clock and one reset for the ARM bank (`CLK_BUS_MSGBOX`, `RST_BUS_MSGBOX`). The other
  banks are clocked and reset from their own processors' domains. **[ours]** (TODO D12)
- With the bank bound to `generic-uio`, nothing enables the clock: writes are dropped and every
  register reads 0, so no interrupt ever fires (`bus-msgbox` enable count 0). **[silicon]**
  The project-only `sun55i-msgbox-uio` driver keeps it on.

## 7. How each user drives it

| User | Interrupts | TX completion | RX |
|---|---|---|---|
| Linux `sun55i-msgbox` (v3 series) | ARM bank only (SPI 0) | poll `count == 0`, 1 ms | clear pending, then drain ≤ 8 under the lock |
| Vendor `sunxi-msgbox` | all four bank lines | poll 5 ms, or write IRQ threshold 1 | drain until empty (10 ms time limit), then clear pending |
| E906 firmware HAL | none (polls) | doorbell waits for FIFO space | drains every queued doorbell each pass |
| UIO profile (`sun55i-msgbox-uio`) | ARM bank (SPI 0) to userspace | E906 sends a doorbell only when Linux's FIFO is empty | userspace drains, clears pending, unmasks |

## 8. Open questions

1. Does the write IRQ at threshold 8 fire exactly when the receiver's FIFO becomes empty on our
   silicon? (Test: enable it on channel 9 with threshold 8, watch WRITE_IRQ_STATUS as the E906 reads.)
2. Does SPI 174 also fire for the E906's *read* interrupt if the E906 enables it? Linux would
   then see those too and must ignore them.
3. Is the 1 ms poll the limit for 16-in-flight rpmsg throughput? Needs kernel tracing
   (`txdone_hrtimer` count).
4. D13: we clear the read pending bit before draining, the vendor after; measure whether it
   matters.
5. `FIFO_STATUS` (`0x50`): meaning unknown.
