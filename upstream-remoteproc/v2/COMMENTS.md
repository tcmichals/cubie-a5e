# Upstream Review Feedback & Mailing List Archive: v2

- **Submission Date**: Sat, 26 Sep 2026 19:20:09 -0500 (2026-09-27 00:20 UTC)
- **Lore Master Thread**: [https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/)
- **Total Review Emails Received**: 8
- **Raw Email Archive Directory**: `cubie-a5e/upstream-remoteproc/v2/emails/`

This document contains the complete, unabridged record of all reviewer feedback, maintainer critiques, and automated bot findings received on the v2 patch series directly from the `lore.kernel.org` mailing list archive.

---

## Table of Contents
1. [Maintainer Reviews](#maintainer-reviews)
   - [Krzysztof Kozlowski (Patch 1/7 - Mailbox Binding & Threading Rule)](#1-krzysztof-kozlowski-krzkkernelorg---patch-17-mailbox-binding--threading-rule)
   - [Krzysztof Kozlowski (Patch 4/7 - RemoteProc Binding reg-names Critique)](#2-krzysztof-kozlowski-krzkkernelorg---patch-47-remoteproc-binding-reg-names-critique)
2. [Sashiko AI Bot Reviews](#sashiko-bot-automated-reviews)
   - [Patch 1/7: dt-bindings: mailbox](#3-sashiko-bot---patch-17-dt-bindings-mailbox)
   - [Patch 2/7: mailbox: sun55i driver](#4-sashiko-bot---patch-27-mailbox-driver)
   - [Patch 3/7: mailbox: sun55i KUnit test](#5-sashiko-bot---patch-37-mailbox-kunit-test)
   - [Patch 4/7: dt-bindings: remoteproc](#6-sashiko-bot---patch-47-dt-bindings-remoteproc)
   - [Patch 5/7: remoteproc: sunxi driver](#7-sashiko-bot---patch-57-remoteproc-driver)
   - [Patch 6/7: remoteproc: sunxi KUnit test](#8-sashiko-bot---patch-67-remoteproc-kunit-test)

---

## Maintainer Reviews

### 1. Krzysztof Kozlowski (`krzk@kernel.org`) - Patch 1/7 (Mailbox Binding & Threading Rule)
- **Date**: Thu, 1 Oct 2026 08:16:08 +0200
- **Message-ID**: `<20261001-sensible-marvellous-porpoise-3a8350@quoll>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/](https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/34_20261001-sensible-marvellous-porpoise-3a8350_quoll.eml`

#### Verbatim Maintainer Critique:
```text
On Sat, Sep 26, 2026 at 07:20:10PM -0500, Tim Michals wrote:
> Add Device Tree binding schema for the Allwinner 4-port hardware
> Message Box controller found on sun55i (A523, A527, T527) and
> sun60i (A733) SoCs.
> 
> The message box connects the ARM Cortex-A55 host cluster to the
> HiFi4 Audio DSP, Power Management Unit (CPUS), and XuanTie RISC-V
> co-processor across 12 logical channels with 8-entry hardware FIFOs.
> 
> Signed-off-by: Tim Michals <tcmichals@gmail.com>
> ---
>  .../mailbox/allwinner,sun55i-a523-msgbox.yaml | 102 ++++++++++++++++++
>  1 file changed, 102 insertions(+)
>  create mode 100644 Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml
>

Do not attach (thread) your patchsets to some other threads (unrelated
or older versions). This buries them deep in the mailbox and might
interfere with applying entire sets. See also:
https://elixir.bootlin.com/linux/v6.16-rc2/source/Documentation/process/submitting-patches.rst#L830

...

> +  interrupts:
> +    minItems: 1
> +    maxItems: 4
> +    description:
> +      One interrupt per processor port in port order (arm, dsp, cpus, rv).
> +      The ARM host port interrupt is required; remote port interrupts are
> +      optional. Use interrupt-names to identify which ports are present when
> +      providing a partial list.

No, this has to be constrained/specific. See writing bindings and any
existing examples. Also, just list the interrupts with minItems, instead
of free form text.


Best regards,
Krzysztof
```

#### Action Taken for v3:
1. **Thread Isolation**: Do **NOT** attach/thread v3 to v2 or v1 using `In-Reply-To`. Send v3 as a standalone top-level thread.
2. **Interrupts Schema**: Dropped freeform narrative text describing port order. Converted `interrupts` and `interrupt-names` into an explicit positional `items:` list with `minItems: 1` (`arm`, `dsp`, `cpus`, `rv`).

---

### 2. Krzysztof Kozlowski (`krzk@kernel.org`) - Patch 4/7 (RemoteProc Binding reg-names Critique)
- **Date**: Thu, 1 Oct 2026 08:17:09 +0200
- **Message-ID**: `<20261001-armored-wild-saiga-c5c3ae@quoll>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20261001-armored-wild-saiga-c5c3ae@quoll/](https://lore.kernel.org/linux-sunxi/20261001-armored-wild-saiga-c5c3ae@quoll/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/35_20261001-armored-wild-saiga-c5c3ae_quoll.eml`

#### Verbatim Maintainer Critique:
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
>  .../remoteproc/allwinner,sun55i-rproc.yaml    | 146 ++++++++++++++++++
>  1 file changed, 146 insertions(+)
>  create mode 100644 Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
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
> +  - Jernej Skrabec <jernej.skrabec@gmail.com>
> +  - Samuel Holland <samuel@sholland.org>
> +  - Tim Michals <tcmichals@gmail.com>
> +
> +description:
> +  The Allwinner T527, A527, and A523 (sun55i) SoCs integrate a T-Head
> +  (XuanTie) E906 or E907 RISC-V co-processor alongside the ARM Cortex-A55
> +  cluster. The co-processor runs bare-metal firmware loaded and lifecycle-
> +  managed by the Linux remoteproc framework.
> +
> +  The driver controls CCU-integrated clocks (bus, core) and resets
> +  (cfg, core), programs the hardware boot-vector register, maps
> +  internal SRAM (SRAM_A3) windows, and connects to the Allwinner
> +  hardware mailbox (CPUX_MSGBOX) for VirtIO RPMsg IPC.
> +
> +properties:
> +  compatible:
> +    const: allwinner,sun55i-a523-rproc
> +
> +  reg:
> +    minItems: 1
> +    maxItems: 4
> +    description:
> +      Memory-mapped register regions. The following named regions are
> +      supported (all optional except at least one of r_sram or r_sram1) -
> +      "cfg" for RISC-V core control and boot-vector registers,
> +      "r_sram" for dedicated MCU SRAM Space 0,
> +      "r_sram1" for switchable MCU SRAM Space 1,
> +      "remap" for the hardware remap control register.
> +
> +  reg-names:
> +    minItems: 1
> +    maxItems: 4
> +    items:
> +      enum:
> +        - cfg
> +        - r_sram
> +        - r_sram1
> +        - remap

So you completely ignored my feedback and sent exactly the same.

Best regards,
Krzysztof
```

#### Action Taken for v3:
1. Addressed the overlooked `enum: [cfg, r_sram, r_sram1, remap]` in `reg-names`.
2. Converted `reg` and `reg-names` to fixed, positional `items:` with descriptions and `- const:` entries, matching TI PRU and Amlogic ARC schemas.

---

## Sashiko Bot Automated Reviews

### 3. Sashiko Bot - Patch 1/7: dt-bindings: mailbox
- **Date**: Sun, 27 Sep 2026 00:30:14 +0000
- **Message-ID**: `<20260927003015.244211F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927003015.244211F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927003015.244211F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/29_20260927003015.244211F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 3 potential issue(s) to consider:
- [Medium] dt-bindings: incomplete schema constraints for interrupt-names
- [Medium] dt-bindings: A527 SoC compatible string missing from schema
- [Low] dt-bindings: register bank size mismatch between description and example
--

commit a5a15af0e6145674274afe29234b0c38ce4cb1b7
Author: Tim Michals <tcmichals@gmail.com>

    dt-bindings: mailbox: add Allwinner sun55i msgbox schema

This commit adds a Device Tree binding schema for the Allwinner 4-port hardware
Message Box controller found on sun55i (A523, A527, T527) and sun60i (A733)
SoCs. It defines the constraints for properties like registers, clocks, resets,
and interrupts.

> diff --git a/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml b/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml
> new file mode 100644
> index 0000000000000..6b0c7ff70c9fb
> --- /dev/null
> +++ b/Documentation/devicetree/bindings/mailbox/allwinner,sun55i-a523-msgbox.yaml
> @@ -0,0 +1,102 @@
[ ... ]
> +properties:
> +  compatible:
> +    enum:
> +      - allwinner,sun55i-a523-msgbox
> +      - allwinner,sun55i-t527-msgbox
> +      - allwinner,sun60i-a733-msgbox

[Severity: Medium]
The commit message explicitly claims to add support for the Message Box
controller on "sun55i (A523, A527, T527)". The compatible enum provides
strings for A523, T527, and A733, but entirely omits an entry for A527.

Device trees attempting to declare A527 support will fail validation or
must misleadingly use the A523 string without backing documentation.

Should "allwinner,sun55i-a527-msgbox" be included in this enum?

> +
> +  reg:
> +    maxItems: 4
> +    description:
> +      Register banks for each of the four processor ports. Each port has
> +      an independent 256-byte register bank.

[Severity: Low]
Does this description accurately reflect the register bank size?

The text states each port has a 256-byte register bank, but the example
later in the schema maps the hardware's 4KB-aligned register banks (0x1000
bytes):

        reg = <0x03003000 0x1000>,
              <0x07094000 0x1000>,
              <0x07120000 0x1000>,
              <0x07136000 0x1000>;

This creates a documentation mismatch. Should the description be updated
to match the 4K page-aligned size used in practice?

[ ... ]
> +  interrupts:
> +    minItems: 1
> +    maxItems: 4
> +    description:
> +      One interrupt per processor port in port order (arm, dsp, cpus, rv).
> +      The ARM host port interrupt is required; remote port interrupts are
> +      optional. Use interrupt-names to identify which ports are present when
> +      providing a partial list.
[ ... ]
> +required:
> +  - compatible
> +  - reg
> +  - reg-names
> +  - clocks
> +  - resets
> +  - interrupts
> +  - '#mbox-cells'

[Severity: Medium]
The schema description for interrupts explicitly states that interrupt-names
must be used to identify ports when providing a partial list of optional
interrupts.

However, interrupt-names is not marked as a required property here. As a
result, device tree nodes with ambiguous, partial interrupt lists can
silently pass validation, leaving dependent software unable to reliably
map the provided interrupts to their respective hardware ports.

Could this be enforced in the schema constraints?

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=1
```

---

### 4. Sashiko Bot - Patch 2/7: mailbox: sun55i driver
- **Date**: Sun, 27 Sep 2026 00:33:02 +0000
- **Message-ID**: `<20260927003302.D69591F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927003302.D69591F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927003302.D69591F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/32_20260927003302.D69591F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 4 potential issue(s) to consider:
- [High] mailbox: sun55i: out-of-bounds array write in probe due to unbounded DT irq_cnt
- [High] mailbox: sun55i: incorrect last_tx_done polling condition breaks TX semantics
- [High] mailbox: sun55i: NULL pointer dereference in IRQ handler during teardown
- [High] mailbox: sun55i: lockless multi-IRQ concurrency causes FIFO underflow
--

commit d294abbd2180fd12203dc082ff7e6d574533c041
Author: Tim Michals <tcmichals@gmail.com>

mailbox: sun55i: add Allwinner sun55i/sun60i 4-port Message Box driver

This commit adds a mailbox driver for the 4-port hardware Message Box
controller found on Allwinner sun55i and sun60i SoCs. It implements hardware
FIFO communication across 12 logical channels for multiple heterogeneous cores.

> diff --git a/drivers/mailbox/sun55i-msgbox.c b/drivers/mailbox/sun55i-msgbox.c
> new file mode 100644
> index 0000000000000..928e52e08bad4
> --- /dev/null
> +++ b/drivers/mailbox/sun55i-msgbox.c
> @@ -0,0 +1,375 @@
[ ... ]
> +irqreturn_t sun55i_msgbox_irq(int irq, void *dev_id)
> +{
> +	struct sun55i_msgbox *mbox = dev_id;
> +	irqreturn_t ret = IRQ_NONE;
> +	int i, local_n, p, chan_idx;
> +
> +	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++) {
> +		void __iomem *local_base = mbox->regs[0];

[Severity: High]
Does this lockless loop allow multiple concurrent handlers on different CPUs to
inspect and process the same hardware channels simultaneously?

Since sun55i_msgbox_probe() registers the exact same handler for multiple IRQs
using request_irq(), if two remote processors send messages concurrently, both
IRQs fire, executing the handler on different CPUs simultaneously.

Both CPUs would see the same pending bit and race to drain the exact same
channel, leading to a TOCTOU race where the second CPU reads from an empty FIFO,
resulting in a hardware underflow and delivery of a corrupt message.

> +		u32 en, stat, pending;
> +
> +		en = readl(local_base + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
> +		stat = readl(local_base + SUNXI_MSGBOX_READ_IRQ_STATUS(local_n));
> +		pending = en & stat;
[ ... ]
> +static bool sun55i_msgbox_last_tx_done(struct mbox_chan *chan)
> +{
> +	struct sun55i_msgbox *mbox = to_sun55i_msgbox(chan);
> +	int n = chan - mbox->controller.chans;
> +	int local_n, p, remote_id, remote_n;
> +	u32 count;
> +
> +	sun55i_chan_to_route(n, &local_n, &p, &remote_id, &remote_n);
> +
> +	count = readl(mbox->regs[remote_id] + SUNXI_MSGBOX_MSG_STATUS(remote_n, p)) & MSG_NUM_MASK;
> +	return count < SUN55I_FIFO_MAX;
> +}

[Severity: High]
Does returning true when the FIFO is not full falsely signal transmission
completion to the polling framework?

The mailbox framework polls last_tx_done() to know when a message has been
consumed by the remote processor. Returning true while the message is still in
the FIFO (count < SUN55I_FIFO_MAX) falsely signals completion.

The framework will then prematurely send subsequent messages, leading to a
hardware FIFO overflow and dropped IPC messages.

[ ... ]
> +static int sun55i_msgbox_probe(struct platform_device *pdev)
> +{
[ ... ]
> +	irq_cnt = platform_irq_count(pdev);
> +	if (irq_cnt < 0) {
> +		ret = irq_cnt;
> +		goto err_assert_reset;
> +	}
> +
> +	for (i = 0; i < irq_cnt; i++) {
> +		int irq = platform_get_irq(pdev, i);
> +
> +		if (irq < 0) {
> +			ret = irq;
> +			goto err_free_irqs;
> +		}
> +
> +		ret = request_irq(irq, sun55i_msgbox_irq,
> +				  IRQF_SHARED, dev_name(dev), mbox);
> +		if (ret) {
> +			dev_err(dev, "failed to request irq %d: %d\n", irq, ret);
> +			goto err_free_irqs;
> +		}
> +		mbox->irqs[i] = irq;
> +		mbox->num_irqs = i + 1;
> +	}

[Severity: High]
Can this write overflow the mbox->irqs array if irq_cnt exceeds 4?

The driver retrieves irq_cnt directly from the device tree via
platform_irq_count() without bounds checking. If a device tree provides 5 or
more interrupts, the unconditional loop will write out of bounds of the
fixed-size 4-element mbox->irqs array, corrupting memory of adjacent fields in
the sun55i_msgbox structure.

[ ... ]
> +static void sun55i_msgbox_remove(struct platform_device *pdev)
> +{
> +	struct sun55i_msgbox *mbox = platform_get_drvdata(pdev);
> +	int local_n, i;
> +
> +	mbox_controller_unregister(&mbox->controller);
> +
> +	/* Mask hardware interrupts and free IRQs before asserting reset and disabling clock */

[Severity: High]
Does unregistering the controller here before masking hardware interrupts and
freeing IRQs leave a race window open for a NULL pointer dereference?

During device removal, mbox_controller_unregister() calls mbox_free_channel(),
which invokes sun55i_msgbox_shutdown() and clears the chan->cl pointer.

While sun55i_msgbox_shutdown() masks the interrupt via MMIO, it fails to
synchronize the kernel IRQ subsystem via synchronize_irq().

If an interrupt handler is already running concurrently on another CPU, it will
proceed to read the FIFO and invoke mbox_chan_received_data() from
sun55i_msgbox_irq(), which dereferences the now-NULL chan->cl->rx_callback
pointer.

> +	for (local_n = 0; local_n < SUN55I_NUM_ROUTES; local_n++)
> +		writel(0, mbox->regs[0] + SUNXI_MSGBOX_READ_IRQ_ENABLE(local_n));
> +
> +	for (i = 0; i < mbox->num_irqs; i++)
> +		free_irq(mbox->irqs[i], mbox);
> +
> +	reset_control_assert(mbox->reset);
> +	clk_disable_unprepare(mbox->clk);
> +}

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=2
```

---

### 5. Sashiko Bot - Patch 3/7: mailbox: sun55i KUnit test
- **Date**: Sun, 27 Sep 2026 00:31:20 +0000
- **Message-ID**: `<20260927003121.14C351F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927003121.14C351F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927003121.14C351F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/30_20260927003121.14C351F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 3 potential issue(s) to consider:
- [Medium] test: MMIO endianness bug in mock registers causes test failures on BE
- [Low] build: missing SUN55I_MSGBOX dependency for KUnit test config
- [Low] test: mock bypass causes startup() flush test to silently succeed via failsafe
--

commit cd5e63e730e04c965eb8f6c0be2bc107e7e91214
Author: Tim Michals <tcmichals@gmail.com>

mailbox: sun55i: add KUnit test suite for sun55i msgbox driver

Adds a KUnit test suite for the Allwinner sun55i hardware Message Box
driver, validating channel routing, mock MMIO message sending, and
interrupt handling.

> diff --git a/drivers/mailbox/Kconfig b/drivers/mailbox/Kconfig
> index 9067f5161276a..af51794d260df 100644
> --- a/drivers/mailbox/Kconfig
> +++ b/drivers/mailbox/Kconfig
> @@ -342,6 +342,16 @@ config SUN55I_MSGBOX
>  	  provides 12 hardware FIFO channels for communication between the
>  	  Cortex-A55 host, CPUS, HiFi4 DSP, and XuanTie E907 RISC-V coprocessors.
>  
> +config SUN55I_MSGBOX_KUNIT_TEST
> +	tristate "KUnit tests for Allwinner sun55i msgbox" if !KUNIT_ALL_TESTS
> +	depends on KUNIT
> +	default KUNIT_ALL_TESTS

[Severity: Low]
Should this test configuration also depend on SUN55I_MSGBOX?

If the kernel is built with SUN55I_MSGBOX as a module and the test is
built-in, or if the driver is disabled completely, could this cause
undefined reference linker errors since the test links against symbols
exported by the driver?

> diff --git a/drivers/mailbox/sun55i_msgbox_test.c b/drivers/mailbox/sun55i_msgbox_test.c
> new file mode 100644
> --- /dev/null
> +++ b/drivers/mailbox/sun55i_msgbox_test.c
[ ... ]
> +static struct mock_msgbox_fixture *create_mock_fixture(struct kunit *test)
> +{
> +	struct mock_msgbox_fixture *fix;
> +	int i;
[ ... ]
> +	for (i = 0; i < SUN55I_MAX_PROCESSORS; i++)
> +		fix->mbox.regs[i] = (void __iomem *)fix->regs[i];

[Severity: Medium]
Will this cause the KUnit tests to fail unconditionally on big-endian
architectures?

The test fixture uses a host-endian u32 array for mock memory, but the
driver uses readl() and writel(). On a big-endian host, the device
accessors will perform a byte-swap, resulting in scrambled values when
reading from or writing to the mock registers.

[ ... ]
> +static void mock_rx_cb(struct mbox_client *cl, void *data)
> +{
[ ... ]
> +	/*
> +	 * Emulate hardware FIFO pop behavior: reading a word from the
> +	 * message FIFO decrements MSG_STATUS in real hardware.
> +	 */
> +	if (sink->status_reg && (*sink->status_reg & MSG_NUM_MASK) > 0)
> +		(*sink->status_reg)--;
> +}
[ ... ]
> +static void test_functional_startup_flushes_stale_fifo(struct kunit *test)
> +{
> +	struct mock_msgbox_fixture *fix = create_mock_fixture(test);
> +	u32 en_idx = SUNXI_MSGBOX_READ_IRQ_ENABLE(2) / 4;
> +	u32 stat_idx = SUNXI_MSGBOX_READ_IRQ_STATUS(2) / 4;
> +	u32 msg_stat_idx = SUNXI_MSGBOX_MSG_STATUS(2, 0) / 4;
> +	u32 fifo_idx = SUNXI_MSGBOX_MSG_FIFO(2, 0) / 4;
> +	int ret;
> +
> +	/* Simulate stale messages present in hardware FIFO before channel open */
> +	fix->regs[0][msg_stat_idx] = 4;
> +	fix->regs[0][fifo_idx] = 0xDEADBEEF;
> +
> +	ret = sun55i_msgbox_chan_ops.startup(&fix->chans[8]);
> +	KUNIT_EXPECT_EQ(test, ret, 0);

[Severity: Low]
Does test_functional_startup_flushes_stale_fifo() pass here solely due
to the driver's hardcoded failsafe boundary?

Since the readl() calls in the driver's startup() function do not trigger
the mock IRQ callback mock_rx_cb() (the only place where the mock MSG_STATUS
count is decremented), the status count never drops.

The test intends to simulate a 4-message flush, but it appears the driver
loop will spin until it hits the SUN55I_FIFO_MAX boundary cap instead.

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=3
```

---

### 6. Sashiko Bot - Patch 4/7: dt-bindings: remoteproc
- **Date**: Sun, 27 Sep 2026 00:27:58 +0000
- **Message-ID**: `<20260927002758.E65581F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927002758.E65581F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927002758.E65581F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/28_20260927002758.E65581F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 1 potential issue(s) to consider:
- [Medium] dt-bindings: missing constraint for required 'r_sram'/'r_sram1' in reg-names
--

commit cc7b4893d8220b754d0e23e60a9d997c33b1c0c3
Author: Tim Michals <tcmichals@gmail.com>
Subject: dt-bindings: remoteproc: add Allwinner sun55i-rproc schema

This commit adds a Device Tree binding schema for the Allwinner XuanTie
E906/E907 RISC-V remoteproc on sun55i SoCs. It defines resources such as
memory-mapped registers, clocks, resets, and mailboxes required to manage
the co-processor.

> diff --git a/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml b/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml
> new file mode 100644
> index 0000000000000..53e3f4faaa768
> --- /dev/null
> +++ b/Documentation/devicetree/bindings/remoteproc/allwinner,sun55i-rproc.yaml

[ ... ]

> +  reg:
> +    minItems: 1
> +    maxItems: 4
> +    description:
> +      Memory-mapped register regions. The following named regions are
> +      supported (all optional except at least one of r_sram or r_sram1) -
> +      "cfg" for RISC-V core control and boot-vector registers,
> +      "r_sram" for dedicated MCU SRAM Space 0,
> +      "r_sram1" for switchable MCU SRAM Space 1,
> +      "remap" for the hardware remap control register.
> +
> +  reg-names:
> +    minItems: 1
> +    maxItems: 4
> +    items:
> +      enum:
> +        - cfg
> +        - r_sram
> +        - r_sram1
> +        - remap

[Severity: Medium]
The description for reg explicitly states that at least one of r_sram or
r_sram1 is required, but the schema for reg-names allows any single item
from the enum (such as just cfg) to pass validation.

If a device tree node is created with only cfg or remap in reg-names, it
will silently pass offline device tree validation despite lacking the
required SRAM regions.

Should an additional constraint be added to enforce the presence of at least
one SRAM region so that incomplete device trees are caught during validation?

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=4
```

---

### 7. Sashiko Bot - Patch 5/7: remoteproc: sunxi driver
- **Date**: Sun, 27 Sep 2026 00:33:38 +0000
- **Message-ID**: `<20260927003338.CB0E21F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927003338.CB0E21F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927003338.CB0E21F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/33_20260927003338.CB0E21F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 10 potential issue(s) to consider:
- [High] remoteproc: sunxi: unbalanced disable_irq via crash_irq_enabled race
- [High] remoteproc: sunxi: race on kick_msg and immediate txdone corrupts mailbox
- [High] remoteproc: sunxi: double mapping of DT regions with conflicting attributes
- [High] remoteproc: sunxi: premature core execution due to broken reset fallback logic
- [High] remoteproc: sunxi: UAF of virtqueues due to late mailbox interrupts
- [High] remoteproc: sunxi: UAF of priv in probe error path due to workqueue teardown
- [High] remoteproc: sunxi: UAF of rproc in remove due to crash_irq_enabled data race
- [Medium] remoteproc: sunxi: da_to_va translates unmatched ATT addresses as host PAs
- [Medium] remoteproc: sunxi: missing teardown of crash IRQ on start failure leaks state
- [Medium] remoteproc: sunxi: missing write flush of boot address causes execution race
--

commit e007f2bf04fe297a7d71605516a9a9ee48e727ef
Author: Tim Michals <tcmichals@gmail.com>

remoteproc: sunxi: add Allwinner XuanTie RISC-V remoteproc driver

Add remoteproc driver for the auxiliary Alibaba T-Head XuanTie E907
RISC-V co-processor found on Allwinner A523, A527, and T527 SoCs.

> diff --git a/drivers/remoteproc/sunxi_rproc.c b/drivers/remoteproc/sunxi_rproc.c
> index 0000000000000..fbb0ee8a91105
> --- /dev/null
> +++ b/drivers/remoteproc/sunxi_rproc.c
[ ... ]
> +static irqreturn_t sunxi_rproc_crash_handler(int irq, void *data)
> +{
> +	struct sunxi_rproc *priv = data;
> +	struct rproc *rproc = priv->rproc;
> +
> +	dev_err(priv->dev, "Hardware crash event received from %s core!\n",
> +		priv->cfg ? priv->cfg->name : "remote");
> +	if (priv->crash_irq_enabled) {
> +		disable_irq_nosync(irq);
> +		priv->crash_irq_enabled = false;
> +	}
> +	rproc_report_crash(rproc, RPROC_FATAL_ERROR);
> +
> +	return IRQ_HANDLED;
> +}

[Severity: High]
Could this lockless check of priv->crash_irq_enabled race with
sunxi_rproc_stop() or sunxi_rproc_remove()?

If a hardware crash occurs concurrently with the core being stopped or removed,
both threads can evaluate priv->crash_irq_enabled as true. Both paths will
independently disable the IRQ, incrementing the disable depth to 2.

When the remote core is restarted via sunxi_rproc_start(), enable_irq() is
only called once, which leaves the crash IRQ permanently masked at the
hardware level.

Also, could this race result in a Use-After-Free during removal?

If the interrupt handler clears the flag before sunxi_rproc_remove() evaluates
it, the remove path skips disable_irq(). It then continues to rproc_del() and
frees the rproc structure while the interrupt handler is concurrently executing
rproc_report_crash(rproc, RPROC_FATAL_ERROR).

[ ... ]
> +int sunxi_rproc_prepare(struct rproc *rproc)
> +{
> +	struct sunxi_rproc *priv = rproc->priv;
> +	const struct sunxi_rproc_cfg *cfg = priv->cfg ? priv->cfg : &sun55i_riscv_cfg;
> +	int ret;
> +
> +	/* 1. Deassert configuration & SRAM bus resets */
> +	if (priv->rst_cfg) {
> +		ret = reset_control_deassert(priv->rst_cfg);

[Severity: High]
Does this cause the core to start executing prematurely if rst_cfg acts as the
execution reset?

In sunxi_rproc_prepare(), rst_cfg is unconditionally deasserted to allow
register access. Later in sunxi_rproc_start(), if the optional rst_core is
missing, the code falls back to deasserting rst_cfg again:

int sunxi_rproc_start(struct rproc *rproc)
{
    ...
    if (priv->rst_core) {
        ret = reset_control_deassert(priv->rst_core);
    ...
    } else if (priv->rst_cfg) {
        ret = reset_control_deassert(priv->rst_cfg);

Because rst_cfg was already deasserted in prepare(), the remote core actually
starts executing before the firmware segments are fully loaded and before
the boot address is programmed.

[ ... ]
> +int sunxi_rproc_start(struct rproc *rproc)
> +{
[ ... ]
> +	/*
> +	 * Program boot vector while the core execution reset is held.
> +	 * The CFG block bus was un-gated during prepare() via rst_cfg.
> +	 */
> +	if (priv->cfg_va) {
> +		writel((u32)rproc->bootaddr, priv->cfg_va + cfg->boot_reg_offset);
> +		dev_dbg(priv->dev, "STA_ADD set to 0x%08x\n", (u32)rproc->bootaddr);
> +	}
> +
> +	/* Release core execution reset so the core begins execution at bootaddr */
> +	if (priv->rst_core) {
> +		ret = reset_control_deassert(priv->rst_core);
> +		if (ret) {
> +			dev_err(priv->dev, "failed to release core reset: %d\n", ret);
> +			return ret;
> +		}

[Severity: Medium]
Is a read-back of the CFG register necessary to explicitly flush the posted
boot address write?

Without flushing the writel() before deasserting the core reset, the SoC
interconnect might deliver the reset deassertion before the boot address
reaches the CFG block. This could cause the core to begin execution from a
stale or zero address.

[Severity: Medium]
If reset_control_deassert() fails, does this error path leave the crash IRQ
enabled for an offline core?

Returning early bypasses the required IRQ cleanup, leaving the IRQ active
and the system vulnerable to spurious interrupts during retry attempts.

[ ... ]
> +void sunxi_rproc_kick(struct rproc *rproc, int vqid)
> +{
> +	struct sunxi_rproc *priv = rproc->priv;
> +	int ret;
> +
> +	if (!priv->tx_chan)
> +		return;
> +
> +	/*
> +	 * Use priv->kick_msg rather than a stack-local variable. The mailbox
> +	 * controller runs with tx_block=false, so mbox_send_message() may
> +	 * queue the pointer and return before the hardware reads the message.
> +	 * A stack-local vqid would be a use-after-return at that point.
> +	 */
> +	priv->kick_msg = (u32)vqid;
> +	ret = mbox_send_message(priv->tx_chan, &priv->kick_msg);
> +	if (ret < 0)
> +		dev_err_ratelimited(priv->dev, "failed to send mailbox kick: %d\n", ret);
> +
> +	mbox_client_txdone(priv->tx_chan, 0);
> +}

[Severity: High]
Does this create a data race on priv->kick_msg during concurrent kicks?

Different virtqueues on multiple CPUs can execute sunxi_rproc_kick()
concurrently. Since they both assign to the single shared priv->kick_msg
variable without a lock, a concurrent kick will overwrite it before the
hardware processes the queue, causing dropped notifications.

Also, does calling mbox_client_txdone() immediately after a non-blocking send
break the mailbox queue pacing?

By falsely signaling TX completion, the framework will submit the next
message immediately, potentially clobbering the controller's TX registers
while the first message is still physically transmitting.

[ ... ]
> +void *sunxi_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
> +{
> +	struct sunxi_rproc *priv = rproc->priv;
> +	u64 sys;
[ ... ]
> +	/*
> +	 * 1. Translate core-local device addresses (DA) to system bus
> +	 * addresses (Host PA) using the SoC address translation table (ATT).
> +	 */
> +	if (sunxi_rproc_da_to_sys(priv, da, len, &sys, is_iomem) == 0) {
> +		if (priv->r_sram_va && sys >= priv->r_sram_phys &&
> +		    (sys + len) <= (priv->r_sram_phys + priv->r_sram_size))
> +			return (__force void *)(priv->r_sram_va + (sys - priv->r_sram_phys));
[ ... ]
> +	}
> +
> +	/*
> +	 * 2. Device Tree Memory Regions (Trace buffer, DRAM carveout, or
> +	 * dynamically-assigned SRAM regions whose host PA is supplied via DT).
> +	 */
> +	if (priv->trace_va && da >= priv->trace_phys &&

[Severity: Medium]
Should there be an early return here if the ATT translation succeeds but fails
to match a valid mapped memory window?

If sunxi_rproc_da_to_sys() returns 0 but the resulting sys address isn't found
in any mapped window, the code falls through to the Device Tree fallback
checks.

In the fallback block, it mistakenly compares the original device address (da)
against the host physical addresses (e.g. priv->trace_phys). If a da happens
to overlap numerically with a host PA, it will improperly translate the
address and return an invalid virtual pointer.

[ ... ]
> +static int sunxi_rproc_parse_memory_regions(struct rproc *rproc)
> +{
[ ... ]
> +		if (name && (strstr(name, "trace") || of_node_name_eq(rmem_np, "trace"))) {
> +			priv->trace_phys = res.start;
> +			priv->trace_size = resource_size(&res);
> +			priv->trace_va = devm_memremap(dev, res.start, resource_size(&res),
> +						       MEMREMAP_WB);
[ ... ]
> +		} else if (name && (strstr(name, "dram") || strstr(name, "vram"))) {
> +			priv->dram_phys = res.start;
> +			priv->dram_size = resource_size(&res);
> +			priv->dram_va = devm_memremap(dev, res.start, resource_size(&res),
> +						      MEMREMAP_WB);
[ ... ]
> +		}
> +
> +		/* Reuse existing SRAM mapping if region overlaps, else ioremap */
> +		if (priv->r_sram1_va && res.start == priv->r_sram1_phys)
> +			va = (__force void *)priv->r_sram1_va;
> +		else if (priv->r_sram_va && res.start == priv->r_sram_phys)
> +			va = (__force void *)priv->r_sram_va;
> +		else
> +			va = (__force void *)devm_ioremap_wc(dev, res.start, resource_size(&res));

[Severity: High]
Does this establish conflicting memory mappings for the trace and dram regions?

After creating the initial mapping using devm_memremap() with Write-Back
cacheability (MEMREMAP_WB), the loop lacks a continue statement or an else
branch.

Execution falls through to the fallback block where it maps the exact same
physical memory again using devm_ioremap_wc() (Write-Combining). Mapping the
same physical address space concurrently with mismatched caching attributes
is undefined behavior on ARM64 and can cause silent cache corruption.

[ ... ]
> +static int sunxi_rproc_probe(struct platform_device *pdev)
> +{
[ ... ]
> +err_mbox_release:
> +	cancel_work_sync(&priv->vq_work);
> +	/*
> +	 * mbox_request_channel_byname() can return ERR_PTR on failure.
> +	 * Guard with IS_ERR() to avoid calling mbox_free_channel() with
> +	 * an invalid pointer, which would panic on the first dereference.
> +	 */
> +	if (priv->rx_chan && !IS_ERR(priv->rx_chan))
> +		mbox_free_channel(priv->rx_chan);

[Severity: High]
Does cancelling the workqueue before shutting down the mailbox channel
introduce a Use-After-Free risk?

Because mbox_free_channel() happens after cancel_work_sync(), the mailbox
remains active. If an RX interrupt fires immediately after the cancellation,
the callback can re-queue priv->vq_work.

When the probe function returns an error, devres will free the priv
structure. The workqueue would then execute the dangling work item and
dereference the freed pointer.

[ ... ]
> +static void sunxi_rproc_remove(struct platform_device *pdev)
> +{
> +	struct rproc *rproc = platform_get_drvdata(pdev);
> +	struct sunxi_rproc *priv = rproc->priv;
[ ... ]
> +	rproc_del(rproc);
> +
> +	if (priv->rx_chan) {
> +		mbox_free_channel(priv->rx_chan);
> +		priv->rx_chan = NULL;
> +	}

[Severity: High]
Does calling rproc_del() before tearing down the mailbox expose a window for
virtqueue Use-After-Free?

rproc_del() stops the remote processor and destroys all virtio
resources and virtqueues. However, the mailbox channel remains active.

If a late interrupt is received, the mailbox IRQ callback will queue vq_work,
which unconditionally calls rproc_vq_interrupt(). Executing this against the
already destroyed virtqueues will result in a crash.

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=5
```

---

### 8. Sashiko Bot - Patch 6/7: remoteproc: sunxi KUnit test
- **Date**: Sun, 27 Sep 2026 00:32:16 +0000
- **Message-ID**: `<20260927003216.F400B1F000FF@smtp.kernel.org>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260927003216.F400B1F000FF@smtp.kernel.org/](https://lore.kernel.org/linux-sunxi/20260927003216.F400B1F000FF@smtp.kernel.org/)
- **Raw Email File**: `cubie-a5e/upstream-remoteproc/v2/emails/31_20260927003216.F400B1F000FF_smtp.kernel.org.eml`

#### Verbatim Review Email:
```text
Thank you for your contribution! Sashiko AI review found 3 potential issue(s) to consider:
- [Medium] remoteproc: sunxi: KUnit test mock MMIO reads fail on big-endian
- [Medium] remoteproc: sunxi: false positive KUnit test for kick_msg
- [Low] remoteproc: sunxi: missing Kconfig dependency for KUnit tests
--

commit a4fcce16cf194ed8872772bf791e111976827604
Author: Tim Michals <tcmichals@gmail.com>

remoteproc: sunxi: add KUnit test suite for sunxi remoteproc driver
    
Add comprehensive in-kernel KUnit unit test suite (34 tests) for the
Allwinner sunxi remoteproc driver.

> diff --git a/drivers/remoteproc/Kconfig b/drivers/remoteproc/Kconfig
> index d35b033b9106d..c2bddfd955665 100644
> --- a/drivers/remoteproc/Kconfig
> +++ b/drivers/remoteproc/Kconfig
[ ... ]
> @@ -396,6 +396,16 @@ config SUNXI_REMOTEPROC
>  	  co-processor on Allwinner A523, A527, and T527 SoCs via the
>  	  remote processor framework.
>  
> +config SUNXI_REMOTEPROC_KUNIT_TEST
> +	tristate "KUnit tests for Allwinner sunxi remoteproc" if !KUNIT_ALL_TESTS
> +	depends on KUNIT

[Severity: Low]
Will this allow incompatible build configurations?

If the test is built-in (=y) but the driver is built as a module (=m) or
disabled (=n), the linker will fail with undefined references since vmlinux
cannot link against module-only symbols. Should this include a
depends on SUNXI_REMOTEPROC statement to enforce proper dependencies?

> +	default KUNIT_ALL_TESTS
> +	help
> +	  KUnit tests for the Allwinner sunxi_rproc driver, covering
> +	  da_to_va() address translation logic and boundary conditions
> +	  across dedicated SRAM spaces, DRAM carveouts, and trace regions.
> +	  Say Y here to run these tests during boot or via kunit.py.
> +
>  endif # REMOTEPROC

> diff --git a/drivers/remoteproc/sunxi_rproc_test.c b/drivers/remoteproc/sunxi_rproc_test.c
> new file mode 100644
> index 0000000000000..2546f850f30bd
> --- /dev/null
> +++ b/drivers/remoteproc/sunxi_rproc_test.c
[ ... ]
> +static void test_start_bootaddr_programming(struct kunit *test)
> +{
> +	struct test_context *ctx = create_test_ctx(test);
> +	int ret;
> +
> +	ctx->priv.cfg_va = (void __iomem *)ctx->mock_cfg_regs;
> +	ctx->rproc.bootaddr = 0x40014000;
> +
> +	ret = sunxi_rproc_start(&ctx->rproc);
> +	KUNIT_EXPECT_EQ(test, ret, 0);
> +
> +	/* Check that bootaddr was written to STA_ADD_REG (offset 0x204) */
> +	KUNIT_EXPECT_EQ(test, ctx->mock_cfg_regs[E906_STA_ADD_REG / 4], 0x40014000U);

[Severity: Medium]
Could this direct array read cause spurious test failures on big-endian
architectures?

The driver code uses writel() for I/O register programming, which mandates a
little-endian memory layout and performs automatic byte-swapping on
big-endian CPUs. Directly reading the raw u32 array elements bypasses this.
On a big-endian architecture, the direct read will interpret the
little-endian bytes in memory as a big-endian integer, returning a
byte-swapped value instead of the expected 0x40014000. Should this test use
readl() to retrieve the value in CPU-endian format?

[ ... ]
> +static void test_prepare_and_unprepare_remap(struct kunit *test)
> +{
> +	struct test_context *ctx = create_test_ctx(test);
> +	int ret;
[ ... ]
> +	/* prepare() should set SUNXI_REMAP_SRAMA3_2_BIT (bit 1) */
> +	ret = sunxi_rproc_prepare(&ctx->rproc);
> +	KUNIT_EXPECT_EQ(test, ret, 0);
> +	KUNIT_EXPECT_EQ(test, ctx->mock_remap_reg & SUNXI_REMAP_SRAMA3_2_BIT,
> +			SUNXI_REMAP_SRAMA3_2_BIT);

[Severity: Medium]
Does this bitwise check need to account for endianness as well?

Because writel() wrote the bit in little-endian order, this native bitwise
check against mock_remap_reg will also fail on big-endian systems.

[ ... ]
> +static void test_kick_stores_vqid(struct kunit *test)
> +{
> +	struct test_context *ctx = create_test_ctx(test);
> +
> +	/* Verify kick_msg stores the passed vqid to avoid stack UAF */
> +	ctx->priv.kick_msg = 0xDEADBEEF;
> +	ctx->priv.tx_chan = NULL; /* Avoid mbox_send_message dispatch */
> +
> +	sunxi_rproc_kick(&ctx->rproc, 1);
> +	/* Without tx_chan, returns before writing kick_msg */
> +	KUNIT_EXPECT_EQ(test, ctx->priv.kick_msg, 0xDEADBEEFU);

[Severity: Medium]
Is this test validating the intended assignment?

The test intentionally sets ctx->priv.tx_chan to NULL to prevent dispatching.
However, looking at sunxi_rproc_kick(), this triggers an early return guard:

void sunxi_rproc_kick(struct rproc *rproc, int vqid)
{
        ...
        if (!priv->tx_chan)
                return;

        priv->kick_msg = (u32)vqid;
        ...
}

Consequently, the assignment to priv->kick_msg never occurs. The test then
asserts against the unmodified initial 0xDEADBEEFU value, which passes
spuriously without actually exercising the assignment logic.

-- 
Sashiko AI review · https://sashiko.dev/#/patchset/20260927002021.797069-1-tcmichals@gmail.com?part=6
```

---

