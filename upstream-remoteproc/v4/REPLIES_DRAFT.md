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

Owner 2026-11-08: option A (keep `minItems`, description fixed). This one is a disagreement,
so it gets a reply. **Open (follow-up discussion):** send it on v3 before v4 (so he can say no
before v4) or after v4 with the link, as below.

```text
On Mon, Nov 02, 2026 at 11:19:54AM +0100, Krzysztof Kozlowski wrote:
> and here, same comments as for v2.

Sorry, I missed these again in v3.

I should have said why the regions are optional, and the binding did
not say it either. They depend on the firmware, not on the SoC:
firmware that runs only from the two SRAMs and does not use virtio
needs none. When the property is present, the first region is always
the vring and buffer pool; a DRAM region for firmware code and data,
and then a trace buffer, may follow in that order. v4 says this in the
binding. xlnx,zynqmp-r5fss does the same for its optional virtio
regions. If you prefer dram and trace gone from the binding, the
firmware can request both through its resource table instead.

The crash interrupt is gone: the A523 has no line for it.

<v4 lore link>

Thanks,
Tim
```
