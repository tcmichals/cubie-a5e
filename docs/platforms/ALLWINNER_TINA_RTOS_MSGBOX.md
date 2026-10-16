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
| T527 (sun55iw3) Tina FreeRTOS for the E906 | Tina SDK (`rtos/lichee/rtos-hal/...`), distributed by Radxa/Allwinner as a large archive | **not obtained**: the 5 GB download in `../tina/` is incomplete |

Everything below is from the sun8iw20 copy. The message box block is the same IP as on the
T527 (same register map: read IRQ `0x20/0x24`, write IRQ `0x30/0x34`, per-channel status and FIFO),
but T527-specific names and IRQ numbers must still be checked in the T527 Tina source.

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
the FIFO. No write interrupt, no timeout, nothing dropped. Our E906 firmware does the same
(`notify_host()` waits for space and counts the waits).

## Open

- Obtain the T527 Tina RTOS source and confirm: the E906 bank clock/reset names, the PLIC
  interrupt number of the RV bank (25 or 48, unverified), and whether its HAL matches `msgbox_amp`.
