# v4 Reply Drafts to v3 Review (for the owner to send)

Send after v4 is posted, as replies to the original v3 emails (reply-all, plain text, inline
quoting). Replace `<v4 lore link>` with the v4 cover letter's lore URL. Sashiko (bot) reviews
get no reply; their points are answered in each patch's "Changes in v4".

`TBD` paragraphs depend on the owner's answers to Q1/Q2 in
[V3_TO_V4_RECONCILIATION_CHECKLIST.md](V3_TO_V4_RECONCILIATION_CHECKLIST.md).

---

No reply to Krzysztof's v3 1/7 comment ("Sashiko comment looks valid"): it is accepted and
answered by the 1/7 and 2/7 changelogs (owner, 2026-11-07: no list reply unless we disagree).
The kernel test robot's v3 7/7 report (PM=n build error) gets no reply either: v4 7/7 fixes it.

## 1. Krzysztof, v3 3/7 (remoteproc binding)
In-Reply-To: `<20261102-warping-victorious-shrew-7cee1e@quoll>`

Owner 2026-11-08: option A (keep `minItems`, description fixed). **Not a disagreement:** v3 did
not make the reason clear and v4 fixes that. The reply restates the why, so his comment is
visibly answered and we do not get "you are not listening". No alternatives offered.

```text
On Mon, Nov 02, 2026 at 11:19:54AM +0100, Krzysztof Kozlowski wrote:
> and here, same comments as for v2.

Sorry, v3 did not explain why memory-region can have fewer than three
entries. v4 fixes that.

The regions depend on the firmware loaded into the E906, not on the
SoC. Firmware that runs only from the two SRAMs and does not use virtio
needs none. Otherwise the first region is always the vring and buffer
pool, followed by a DRAM region for firmware code and data and then a
trace buffer, in that order. v4 says this in the binding description,
the same way xlnx,zynqmp-r5fss describes its optional virtio regions.

The crash interrupt is dropped: the A523 has no line for it.

<v4 lore link>

Thanks,
Tim
```
