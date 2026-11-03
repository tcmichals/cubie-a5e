# v4 patch series workspace

v4 of "Add Allwinner A523 message box and XuanTie E906 remoteproc support".
Same layout as [../v3/](../v3/).

| | |
|:--|:--|
| Previous version | v3, sent 2026-11-01: https://lore.kernel.org/linux-sunxi/20261101185735.762559-1-tcmichals@gmail.com/ |
| v3 reviews | [../v3/COMMENTS.md](../v3/COMMENTS.md) (6 emails, raw in [../v3/emails/](../v3/emails/)) |
| Status | **In prep**: 2 items open for the owner (see the checklist) |
| Work branch | `linux-cubie`: v4 fixes on `cubie-linux-7.1` first (board test), then folded onto a fresh linux-next branch (name to be chosen when created) |
| Patches | 7, same order as v3: 1 mailbox binding, 2 mailbox driver, 3 rproc binding, 4 rproc driver, 5 DTS, 6 mailbox KUnit, 7 rproc KUnit |

## Files

| File | What |
|:--|:--|
| [V3_TO_V4_RECONCILIATION_CHECKLIST.md](V3_TO_V4_RECONCILIATION_CHECKLIST.md) | Every v3 review comment → v4 change, status, evidence. Also the v1/v2 items that came back |
| [PRESEND_CHECKLIST.md](PRESEND_CHECKLIST.md) | Build, test and review gates before sending, including the checks v3 missed (sparse, PM=n, concurrency pass) |
| [COVER_LETTER_DRAFT.txt](COVER_LETTER_DRAFT.txt) | v4 cover letter draft; `TBD` marks text that depends on the open items or on test results |
| [REPLIES_DRAFT.md](REPLIES_DRAFT.md) | Replies to Krzysztof's v3 emails, sent by the owner after v4 is posted |
| [review/](review/) | Our own reviews of the v4 code |
| `v4-*.patch` | Exported series (not yet; `git format-patch -v4 --cover-letter --base=...`) |

## Rules (from the v3 lessons)

- New top-level thread, no `--in-reply-to` (Krzysztof, v2).
- Nothing is sent without the owner's go-ahead; the owner adds his own Signed-off-by.
- Every High/Medium finding is fixed or put to the owner as a yes/no. Nothing is closed as
  "left as is" in a table.
- Changelogs say how each v3 comment was handled; the items that repeated v2 say so plainly.
