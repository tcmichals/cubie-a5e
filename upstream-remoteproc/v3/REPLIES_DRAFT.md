# v3 Reply Drafts to v2 Review (for the owner to send)

Send after v3 is posted, as replies to the original v2 emails (reply-all, plain text,
inline quoting). Replace `<v3 lore link>` with the v3 cover letter's lore URL.
Sashiko (bot) reviews get no reply; their points are listed per patch in the v3
notes ("Changes in v3").

---

## 1. Krzysztof, v2 1/7 (mailbox binding)
In-Reply-To: `<20261001-sensible-marvellous-porpoise-3a8350@quoll>`

```text
On Thu, Oct 01, 2026 at 08:16:08AM +0200, Krzysztof Kozlowski wrote:
> Do not attach (thread) your patchsets to some other threads (unrelated
> or older versions). This buries them deep in the mailbox and might
> interfere with applying entire sets.

Sorry about that. v3 is a new thread:
<v3 lore link>

> No, this has to be constrained/specific. See writing bindings and any
> existing examples. Also, just list the interrupts with minItems, instead
> of free form text.

Done in v3: interrupts and interrupt-names list the four bank
interrupts in order (arm, dsp, cpus, rv) with minItems: 1, and the
free-form text is gone.

Thanks,
Tim
```

---

## 2. Krzysztof, v2 4/7 (remoteproc binding)
In-Reply-To: `<20261001-armored-wild-saiga-c5c3ae@quoll>`

```text
On Thu, Oct 01, 2026 at 08:17:09AM +0200, Krzysztof Kozlowski wrote:
> So you completely ignored my feedback and sent exactly the same.

You are right, I missed it in v2, sorry. In v3 reg and reg-names are
a fixed list of four (cfg, r_sram, r_sram1, remap), all required, and
clocks and resets are fixed lists as well. The file is renamed to
allwinner,sun55i-a523-rproc.yaml to match the compatible:
<v3 lore link>

Thanks,
Tim
```
