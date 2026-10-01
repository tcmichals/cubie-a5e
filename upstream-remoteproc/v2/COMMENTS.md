# Upstream Review Feedback & Mailing List Comments: v2

- **Submission Date**: 2026-09-26 19:20 UTC
- **Lore Master Thread**: [https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/)
- **Series Subject**: `[PATCH v2 0/7] remoteproc: sunxi: Add Allwinner XuanTie E907 RemoteProc and Message Box support`

This document preserves the raw comments and reviewer feedback received on the v2 series so you never have to re-scrape the mailing list.

---

## 1. Krzysztof Kozlowski (`krzk@kernel.org`) - Patch 4/7 (`dt-bindings: remoteproc`)
- **Date**: Thu, 1 Oct 2026 08:17:09 +0200
- **Message-ID**: `<20261001-armored-wild-saiga-c5c3ae@quoll>`
- **Link**: [https://lore.kernel.org/linux-sunxi/20261001-armored-wild-saiga-c5c3ae@quoll/](https://lore.kernel.org/linux-sunxi/20261001-armored-wild-saiga-c5c3ae@quoll/)

### Verbatim Quoted Review:
```text
On Sat, Sep 26, 2026 at 07:20:13PM -0500, Tim Michals wrote:
> Add Device Tree binding schema for the Allwinner XuanTie E907 RISC-V
> remote processor subsystem integrated into Allwinner A523, A527, and
> T527 (sun55i) SoCs.
>
> The binding defines the memory resources (configuration registers,
> SRAM Space 0, SRAM Space 1, and optional remap controller), clocks,
> resets, reserved memory regions, and hardware mailbox channels.
>
> Signed-off-by: Tim Michals <tcmichals@gmail.com>
> ---
> .../remoteproc/allwinner,sun55i-rproc.yaml | 146 ++++++++++++++++++
> 1 file changed, 146 insertions(+)
> create mode 100644 Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
>
> diff --git a/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml b/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
> new file mode 100644
> index 000000000000..53e3f4faaa76
> --- /dev/null
> +++ b/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
> @@ -0,0 +1,146 @@
> +# SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause
> +%YAML 1.2
> +---
> +$id: http://devicetree.org/schemas/remoteproc/allwinner,sun55i-rproc.yaml#
> +$schema: http://devicetree.org/meta-schemas/core.yaml#
> +
> +title: Allwinner XuanTie E906/E907 RISC-V Remoteproc
> +
> +maintainers:
> + - Jernej Skrabec <jernej.skrabec@gmail.com>
> + - Samuel Holland <samuel@sholland.org>
> + - Tim Michals <tcmichals@gmail.com>
> +
> +description:
> + The Allwinner T527, A527, and A523 (sun55i) SoCs integrate a T-Head
> + (XuanTie) E906 or E907 RISC-V co-processor alongside the ARM Cortex-A55
> + cluster. The co-processor runs bare-metal firmware loaded and lifecycle-
> + managed by the Linux remoteproc framework.
> +
> + The driver controls CCU-integrated clocks (bus, core) and resets
> + (cfg, core), programs the hardware boot-vector register, maps
> + internal SRAM (SRAM_A3) windows, and connects to the Allwinner
> + hardware mailbox (CPUX_MSGBOX) for VirtIO RPMsg IPC.
> +
> +properties:
> + compatible:
> + const: allwinner,sun55i-a523-rproc
> +
> + reg:
> + minItems: 1
> + maxItems: 4
> + description:
> + Memory-mapped register regions. The following named regions are
> + supported (all optional except at least one of r_sram or r_sram1) -
> + "cfg" for RISC-V core control and boot-vector registers,
> + "r_sram" for dedicated MCU SRAM Space 0,
> + "r_sram1" for switchable MCU SRAM Space 1,
> + "remap" for the hardware remap control register.
> +
> + reg-names:
> + minItems: 1
> + maxItems: 4
> + items:
> + enum:
> + - cfg
> + - r_sram
> + - r_sram1
> + - remap

So you completely ignored my feedback and sent exactly the same.

Best regards,
Krzysztof
```

### Analysis & Resolution for v3:
- In v2, `clock-names` and `reset-names` had been converted to positional lists, but `reg-names` was accidentally left with `enum: [cfg, r_sram, r_sram1, remap]`.
- Converted `reg` and `reg-names` to fixed, positional `items:` with descriptions and `- const:` entries (`cfg`, `r_sram`, `r_sram1`, `remap`).

---

## 2. Krzysztof Kozlowski (`krzk@kernel.org`) - Patch 1/7 (`dt-bindings: mailbox`)
- **Date**: Thu, 1 Oct 2026 08:16:08 +0200
- **Message-ID**: `<20261001-sensible-marvellous-porpoise-3a8350@quoll>`
- **Link**: [https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/](https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/)

### Verbatim Quoted Review:
```text
Do not attach (thread) your patchsets to some other threads (unrelated
or older versions). This buries them deep in the mailbox and might
interfere with applying entire sets. See also:
https://elixir.bootlin.com/linux/v6.16-rc2/source/Documentation/process/submitting-patches.rst#L830

...

> + interrupts:
> + minItems: 1
> + maxItems: 4
> + description:
> + One interrupt per processor port in port order (arm, dsp, cpus, rv).
> + The ARM host port interrupt is required; remote port interrupts are
> + optional. Use interrupt-names to identify which ports are present when
> + providing a partial list.

No, this has to be constrained/specific. See writing bindings and any
existing examples. Also, just list the interrupts with minItems, instead
of free form text.

Best regards,
Krzysztof
```

### Analysis & Resolution for v3:
1. **Patch Threading**:
   - Do **NOT** use `git send-email --in-reply-to` to thread v3 as a reply to v2 or v1.
   - Send v3 as a fresh, standalone top-level thread with its own cover letter `[PATCH v3 0/7]`.
2. **Interrupts Schema**:
   - Replaced free-form text with explicit positional `items:` list and `minItems: 1`:
     ```yaml
     interrupts:
       minItems: 1
       items:
         - description: ARM Cortex-A55 host port interrupt
         - description: HiFi4 Audio DSP port interrupt
         - description: CPUS power management port interrupt
         - description: XuanTie RISC-V port interrupt
     interrupt-names:
       minItems: 1
       items:
         - const: arm
         - const: dsp
         - const: cpus
         - const: rv
     ```

---

## 3. Sashiko AI Review Bot (`sashiko-bot`) & Maintainer Audits (Drivers & Tests)

The automated adversarial audit on v2 flagged two high/medium issues that were resolved in the codebase for v3:

### Finding 1: Broken `last_tx_done` Polling Condition (`M2` - High)
* **File**: `drivers/mailbox/sun55i-msgbox.c`
* **Issue**: `last_tx_done()` returned `count < SUN55I_FIFO_MAX`. In the mailbox framework, `last_tx_done()` must return `true` only when the remote processor has consumed the message (`count == 0`), not merely when there is space in the FIFO.
* **Resolution**: Updated `last_tx_done()` to return `count == 0`.

### Finding 2: Direct Mock Register Array Access on Big-Endian (`K2` - Medium)
* **File**: `drivers/remoteproc/sunxi_rproc_test.c`
* **Issue**: KUnit test assertions read directly from `ctx->mock_cfg_regs[offset / 4]`. Because `writel()` byte-swaps on Big-Endian architectures, reading the raw mock array causes unit test failures on Big-Endian systems.
* **Resolution**: Replaced direct array indexing with endian-safe `readl(ctx->priv.cfg_va + offset)`.

### Finding 3: Teardown Ordering & Race Hazards (`R5`, `R7` - High)
* **File**: `drivers/remoteproc/sunxi_rproc.c`
* **Issue**: Calling `rproc_del()` before `mbox_free_channel()` in `sunxi_rproc_remove()` allowed late incoming mailbox interrupts to trigger callbacks on destroyed virtqueues.
* **Resolution**: Reordered teardown to strict LIFO symmetry: `devm_free_irq()` $\rightarrow$ `mbox_free_channel()` $\rightarrow$ `cancel_work_sync()` $\rightarrow$ `rproc_del()`.

### Finding 4: Multi-CPU SMP Hardirq Race (`M4` - High)
* **File**: `drivers/mailbox/sun55i-msgbox.c`
* **Issue**: Draining the shared MMIO message FIFO in `sun55i_msgbox_irq()` without a spinlock risked TOCTOU underflows if multiple CPUs handled the shared interrupt concurrently.
* **Resolution**: Enclosed status check and FIFO popping inside `spin_lock_irqsave(&mbox->lock, flags)`.
