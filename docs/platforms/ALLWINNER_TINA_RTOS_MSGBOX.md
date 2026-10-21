# Allwinner Tina RTOS — how the remote core drives the message box

What Allwinner's own RTOS (the FreeRTOS/RT-Thread HAL shipped in Tina for the DSP and RISC-V
cores) does with the message box: clocks, resets and the receive interrupt. Used to check our
bare-metal E906 firmware and the Linux driver against the vendor's model. Register-level facts
are in [ALLWINNER_SUN55I_MSGBOX_REFERENCE.md](ALLWINNER_SUN55I_MSGBOX_REFERENCE.md).

## Source

| What | Where | Status |
|---|---|---|
| Allwinner `sunxi-hal`, `hal/source/msgbox/` | RT-Thread `bsp/allwinner/libraries/sunxi-hal` (RT-Thread master, read 2026-10-16) | read |
| Platforms in that copy | `msgbox-sun8iw20.h` (D1 / T113: ARM, DSP, RISC-V banks), `msgbox-sun50iw11.h` (older IP) | read |
| T527 (sun55iw3) Tina RTOS HAL for the E906 | `https://gitlab.com/tina5.0_aiot/product/tina/rtos-hal.git` `cc63790c369b` and `.../product/rtos-components.git` `190e301e2ef6` (2026-07-15), cloned to `~/projects/cubie/tina-source/` | **read 2026-10-17**: `hal/source/msgbox/msgbox_amp/msgbox_amp.c`, `platform/msgbox-sun55iw3.h` |

The sections below were first written from the sun8iw20 copy and are now checked against the
T527 tree; differences are marked **[T527]**.

## T527 (sun55iw3) platform data [T527]

`platform/msgbox-sun55iw3.h`: banks ARM `0x03003000` (0), DSP `0x07120000` (1), CPUS
`0x07094000` (2), RISCV `0x07136000` (3) — the same as ours. On the E906 (`CONFIG_ARCH_RISCV`):

| | Value |
|---|---|
| Own bank | `MSGBOX_RISCV` |
| Interrupt | `IRQ_MSGBOX = MAKE_IRQn(17, 0)` (the E906's own controller; the numeric value depends on the build's `MAKE_IRQn` variant) |
| Reset / clock | `RST_BUS_MCU_RV_MSG` / `CLK_BUS_MCU_RISCV_MSG` (MCU CCU) |

The DSP uses its own bank, `IRQ_MSGBOX = 3`, `RST_BUS_DSP_MSG` / `CLK_BUS_DSP_MSG`.
`msgbox_amp` is the driver that matches the T527 (`DRIVERS_MSGBOX_AMP`); the Kconfig also has
`msgbox_sx` and a "soft" `msgbox_irq` variant (raises GIC pending bits directly), neither for this IP.

## Write (TX FIFO) interrupt: not used [T527]

`msgbox_amp.c` defines `MSGBOX_WR_IRQ_EN_REG()` / `MSGBOX_WR_IRQ_STA_REG()` (`0x30` / `0x34`) but
**never calls them**. The only interrupt it requests is `IRQ_MSGBOX` for its own bank's *read*
interrupt. TX completion on the RTOS side is a busy-wait on the receiver's `MSG_STATUS` (next
sections). The hardware could interrupt the sender: each bank's write IRQ has a line to each
sending core (to the A55: DSP SPI 181, CPUS SPI 174, RV SPI 205, measured; T527 manual
Table 2-15), see [reference §1, §6b](ALLWINNER_SUN55I_MSGBOX_REFERENCE.md). Tina simply does not
use it.

## Driver variants

- `msgbox_amp` (`CONFIG_DRIVERS_MSGBOX_AMP`): the driver for the sun8iw20-style message box, the
  one that matches the T527.
- `msgbox_sx` (`CONFIG_DRIVERS_MSGBOX_SX`): an older message box with a different layout (IRQ
  status at `0x50`, FIFO status at `0x100`). Not relevant for the T527.

## Clocks and resets — each core enables its own bank

`hal_msgbox_init()` runs on the remote core and:

1. deasserts the reset of **its own bank** (`RST_MSGBOX`, defined as `RST_BUS_MSGBOX1` on the
   sun8iw20 DSP),
2. enables the bus clock of **its own bank** (`CLK_MSGBOX` = `CLK_BUS_MSGBOX1`),
3. requests and enables its bank's interrupt (`IRQ_MSGBOX`).

Linux, in the vendor BSP, enables only the ARM bank (`CLK_MSGBOX0`, `RST_BUS_MSGBOX0`). So in
Allwinner's model the remote's bank is powered by the remote firmware, and Linux's writes into it
work because that firmware has already run `hal_msgbox_init()`.

**Consequence for us:** our E906 firmware is bare metal and never enabled the RV bank
(MCU CCU `0x128`), and our Linux DT described only the ARM bank, so nobody did. That is the most
likely reason the vendor stack never showed the problem we measured (gated RV bank, Linux kicks
lost). Fix on 7.1: Linux enables all four banks (binding lists `arm`, `dsp`, `cpus`, `rv`).

## Receive interrupt — drain, then clear

`irq_msgbox_channel_handler()`:

```c
while (readl(msg_sts))            /* MSG_STATUS: messages waiting */
        rec(readl(msg_reg));      /* pop from MSG_FIFO */
writel(1 << (read_ch * 2), msg_irq_s);   /* then clear the read pending bit */
```

The pending bit is cleared only after the FIFO is empty, the same order as the vendor Linux driver
and `sun6i-msgbox`. Our Linux driver cleared first and took two interrupts per message; fixed on
7.1 (2026-10-16).

Other details:
- The receive interrupt is enabled per channel at `hal_msgbox_alloc_channel()` (`RD_IRQ_EN`,
  bit `2 * read_ch`).
- Port index `n` is computed as `remote < local ? remote : remote - 1`, the same routing as
  ours.

## Send — busy-wait on a full FIFO

`msgbox_channel_send_data()` spins while the remote's `MSG_STATUS` equals 8 (full), then writes
the FIFO. No write interrupt. **[T527]** The spin is bounded: after `MAX_MSGBOX_TRY_SEND_CNT`
(10,000,000) reads it logs "message queue is full for a long time!!!" and returns `-1`, dropping
that word (the sun8iw20 copy read earlier had no limit). Our E906 firmware waits without a limit
(`notify_host()` waits for space and counts the waits).

## Open

- ~~Obtain the T527 Tina RTOS source~~ done 2026-10-17 (see the T527 sections above).
- ~~The numeric E906 interrupt for `MAKE_IRQn(17, 0)`~~ done: it is T-Head CLIC line 17 (`mcause`
  id 17), proven on silicon by `testFreeRTOS` (`CLIC_IRQ_MSGBOX = 17`, ISR wake in the sweep) and
  listed as `RISCV_MSGBOX_RISCV` in the T527 manual, Table 2-16. 25 and 48 were wrong guesses.
