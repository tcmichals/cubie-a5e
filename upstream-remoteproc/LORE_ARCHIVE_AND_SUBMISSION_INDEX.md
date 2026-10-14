# Kernel Lore Submissions & Review Archive

This document indexes all public patch series submissions, maintainer review threads on `lore.kernel.org`, and their corresponding code resolutions for the **Allwinner sun55i (A523/A527/T527) XuanTie E906 RemoteProc and Message Box drivers**.

---

## 1. Submission Series Master Index

| Version | Submission Date | Lore Master Thread | Status | Review Notes & Feedback |
| :--- | :--- | :--- | :---: | :--- |
| **RFC v1** | 2026-09-22 | [lore.kernel.org/linux-sunxi/20260922034711.190253-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20260922034711.190253-1-tcmichals@gmail.com/) | Superseded | [v1/COMMENTS.md](v1/COMMENTS.md) (11 review emails; raw in `v1/emails/`) |
| **v2** | 2026-09-27 | [lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com](https://lore.kernel.org/linux-sunxi/20260927002021.797069-1-tcmichals@gmail.com/) | Superseded | [v2/COMMENTS.md](v2/COMMENTS.md) (8 review emails; raw in `v2/emails/`) |
| **v3** | Pending | Target: `linux-remoteproc@vger.kernel.org`, `linux-sunxi@lists.linux.dev` | In Prep | [v3/COVER_LETTER_DRAFT.txt](v3/COVER_LETTER_DRAFT.txt) |

---

## 2. Key Maintainer Lore Threads & Resolutions

### Chen-Yu Tsai (`wens@kernel.org`) — RFC v1
- **Message-ID**: `<CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com>`
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/](https://lore.kernel.org/linux-sunxi/CAGb2v66_AaPnwErV72eF=KQ2k15spXA2UJcugnOcGcp5PAKVXw@mail.gmail.com/)
- **Critique 1**: Use one compatible string for the SoC family die (A523, A527, T527).
  - **Resolution**: Unified under `allwinner,sun55i-a523-msgbox` and `allwinner,sun55i-a523-rproc`.
- **Critique 2**: Send the full series to all involved parties (do not fragment Cc lists).
  - **Resolution**: Submission recipient list unified in `SUBMISSION_TRACKER.md`.

### Krzysztof Kozlowski (`krzk@kernel.org`) — RFC v1 & v2
- **v1 Message-ID**: `<20260922091444.GA12345@quoll>`
  - **Critique (KK1–KK9)**: Rigid positional `items:`, drop `enum:` in `reg-names`, drop redundant "schema" words, list `memory-region` items explicitly.
  - **Resolution**: Fully rewritten YAML schemas matching fixed hardware definitions.
- **v2 Message-ID**: `<20261001-sensible-marvellous-porpoise-3a8350@quoll>`
  - **Lore Link**: [https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/](https://lore.kernel.org/linux-sunxi/20261001-sensible-marvellous-porpoise-3a8350@quoll/)
  - **Critique**: *"Do not thread new versions under old threads... Each version should be a standalone top-level thread."*
  - **Resolution**: Enforced rule: never use `--in-reply-to` for major patch revisions.

### Rob Herring (`robh@kernel.org`) — RFC v1
- **Lore Link**: [https://lore.kernel.org/linux-sunxi/20260922-robh-dt-check/](https://lore.kernel.org/linux-sunxi/)
- **Critique**: DT binding example failed `dt_binding_check` due to undefined clock/reset header macros.
- **Resolution**: Include correct `<dt-bindings/...>` headers in all YAML examples.

---

## 3. Local Reference Caches in `upstream-remoteproc/`

- **[peer-cache/](peer-cache/)**: Live copies of accepted mainline peer drivers (`imx_rproc.c`, `stm32_rproc.c`, `ti_k3_common.c`, `sun6i-msgbox.c`, and subsystem headers) for instant offline comparison.
- **[PEER_DRIVER_REFERENCE_GUIDE.md](PEER_DRIVER_REFERENCE_GUIDE.md)**: Architectural analysis of how our implementation maps to accepted upstream peers.
- **[v1/](v1/)**, **[v2/](v2/)**, **[v3/](v3/)**: Complete submission patch sets, cover letters, diffs, and raw `.eml` email files received from lore.
