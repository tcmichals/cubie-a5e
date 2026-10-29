## Phases

| Phase | Runs | What changed |
|---|---|---|
| Early drivers (10-02 to 10-03) | #1–4 | All PASS, but the rpmsg round trip swung between 190 µs and 1,242 µs (jitter up to 600 µs, 720–3,000 msg/s): with TX done by the 1 ms poll timer, a message sometimes waited a full poll period. |
| Strict tests (10-14) | #5–7 | Ping tests made strict (no pacing, timeouts and lost data fail). The source checks also flagged three rule breaks: the remoteproc driver reported TX done itself (`knows_txdone`), the FIFO pacing, and a sleeping benchmark. All fixed. |
| TX done = FIFO has room (10-15 to 10-20) | #8–20 | A kick is a doorbell, so it is done once the receiver's FIFO has room (DESIGN_DECISIONS "Message box TX completion"). Round trip ~200 → 167 µs, rate about doubled to ~6,000 msg/s, jitter 600 → ~8 µs. Stress, SRAM-vring and UIO-interrupt metrics added from #10. The Profile 3 UIO numbers changed meaning at #8 (benchmark method, `6a3eb45`, `6779369`): earlier values are not comparable. |
| v3 hardening (10-27 on) | #21– | Final v3 code: 63 KUnit cases (37 message box, 26 remoteproc), message box latency per TX-done mode (#22 on), extra-RAM test (E906 uses DSP RAM, the DRAM carveout and SRAM A3_2), PREEMPT and lockdep kernels. |

## Why runs failed

Every row must pass. Up to #27 the result counted the functional, KUnit, dmesg and history rows,
while the source "invariant" checks were only reported; since 10-29 they are rows too and fail the
sweep. Causes from the sweep output kept in the session logs:

| Run | Date | Failed rows | Cause | Fixed by |
|---|---|---|---|---|
| #5 | 10-14 | Profile 3: `ping_shm`, `ping_uio.py`, UIO suite | New strict ping checks (`deedec4`) showed the UIO path never worked: the message box bank was clock-gated under `generic-uio` and the "doorbell" went to the wrong bank (DebugLog case study 13) | UIO driver and `-uio` DTB |
| #6 | 10-14 | `run_tests.py` in Profiles 1 and 2 | Test harness: `run_ping` took the last argument instead of the script path, so the Python tools were "not installed" | `37fe2db` (#7 PASS) |
| #8 | 10-15 | Profile 1: `ping_dram`, 16 in flight | The new 16-in-flight DRAM test got a malformed reply descriptor (the single-slot protocol had two writers per flag, case study 13) | firmware DRAM SPSC rings |
| #9 | 10-15 | every Profile 1 rpmsg row | Test harness: the sweep's 45 s SSH timeout dropped `run_tests.py` but left it running on the board, restarting the E906 under the next steps | timeout reported and killed, 300 s (`6779369`) |
| #13 | 10-16 | Kernel Health (dmesg) in Profiles 1 and 2 | Stress tests overflowed the mailbox core's 20-entry queue: `Try increasing MBOX_TX_QUEUE_LEN` (no data lost, the data is in the vrings) | VirtIO kick suppression in the firmware (`8f8c07b`, case study 15) |
| #14–16 | 10-17 | not in the logs | Most likely the same `MBOX_TX_QUEUE_LEN` warnings (case study 15 is from that day; #17 passed once kick suppression was on the board) — not confirmed | `8f8c07b` |
| #21 | 10-27 | KUnit, Profile 3 | 2 KUnit cases failed under PREEMPT_RT (the TX-done handler ran in its IRQ thread, outside the test's stubs); the dmesg check flagged a KUnit case that fills the mailbox queue on purpose; Profile 3 found no `remoteproc0` | `97f6e462ba87`; dmesg check ignores `sunxi-rproc-test` |
| #23 | 10-28 | Profile 1: FreeRTOS ping and stress | The new extra-RAM step ran before the FreeRTOS pings and left the E906 on the wrong firmware | step moved after them (#24 PASS) |

No run failed because of a driver regression. Five of the ten were the tests or the harness
(#6, #9, #21's dmesg check, #23, and #5's checks finding a path that had never worked); the
others found real firmware or integration problems that were then fixed.

From #21 to #27 two source checks (TX completion, drain bound) reported FAIL on correct code: they
matched text the 10-26 refactor changed and did not count toward the result then. Fixed on 10-29
(`b44a031`), and they now fail the sweep like every other row.

## Reading the numbers

- Profile 1: rpmsg with vrings in DRAM. Profile 2: vrings in SRAM. Profile 3: userspace UIO straight to the mailbox and SRAM, no rpmsg.
- RT = PREEMPT_RT, the board's normal kernel. On PREEMPT (non-RT) the interrupt paths are faster, because RT runs interrupt handlers in threads. Lockdep costs 2–3x on every lock-heavy path, as expected.
- The range check compares each run with the median of the last 10 PASS runs of the same kernel flavor; a SLOW result fails the sweep.
