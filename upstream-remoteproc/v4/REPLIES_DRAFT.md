# v4 Reply Drafts to v3 Review (for the owner to send)

Send after v4 is posted, as replies to the original v3 emails (reply-all, plain text, inline
quoting). Replace `<v4 lore link>` with the v4 cover letter's lore URL. Sashiko (bot) reviews
get no reply; their points are answered in each patch's "Changes in v4".

`TBD` paragraphs depend on the owner's answers to Q1/Q2 in
[V3_TO_V4_RECONCILIATION_CHECKLIST.md](V3_TO_V4_RECONCILIATION_CHECKLIST.md).

---

## 1. Krzysztof, v3 1/7 (mailbox binding)
In-Reply-To: `<20261102-hot-glistening-cassowary-c1da4e@quoll>`

```text
On Mon, Nov 02, 2026 at 11:17:39AM +0100, Krzysztof Kozlowski wrote:
> Sashiko comment looks valid, rest seems ok.

Agreed. v4 drops allwinner,tx-done-irq: the driver always uses the
receiver bank's write interrupt for TX done, and the poll mode is gone:
<v4 lore link>

Thanks,
Tim
```

---

## 2. Krzysztof, v3 3/7 (remoteproc binding)
In-Reply-To: `<20261102-warping-victorious-shrew-7cee1e@quoll>`

```text
On Mon, Nov 02, 2026 at 11:19:54AM +0100, Krzysztof Kozlowski wrote:
> and here, same comments as for v2.

Sorry, I missed these again in v3.

I should have said why dram and trace are optional, and the binding
did not say it either. They depend on the firmware, not on the SoC:
firmware linked to run from the two SRAMs has no DRAM sections, and
firmware without a trace buffer needs no trace region. The vring pool
is always needed and is required. v4 says this in the binding. If you
prefer them gone from the binding, the firmware can request both
through its resource table instead.

The crash interrupt is gone: the A523 has no line for it.

<v4 lore link>

Thanks,
Tim
```
