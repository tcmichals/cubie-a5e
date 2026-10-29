## Phases

| Phase | Runs | What changed |
|---|---|---|
| Early drivers (10-02 to 10-03) | #1–4 | All PASS, but the rpmsg round trip swung between 190 µs and 1,242 µs (jitter up to 600 µs, 720–3,000 msg/s): with TX done by the 1 ms poll timer, a message sometimes waited a full poll period. |
| Invariant fixes (10-14) | #5–7 | The sweep's source checks caught three rule breaks: the remoteproc driver reported TX done itself (`knows_txdone`), the FIFO pacing was wrong, and the benchmark slept. All fixed. |
| TX done = FIFO has room (10-15 to 10-20) | #8–20 | A kick is a doorbell, so it is done once the receiver's FIFO has room (DESIGN_DECISIONS "Message box TX completion"). Round trip ~200 → 167 µs, rate about doubled to ~6,000 msg/s, jitter 600 → ~8 µs. Stress, SRAM-vring and UIO-interrupt metrics added from #10. The Profile 3 UIO numbers changed meaning at #8 (benchmark method, `6a3eb45`, `6779369`): earlier values are not comparable. |
| v3 hardening (10-27 on) | #21– | Final v3 code: 63 KUnit cases (37 message box, 26 remoteproc), message box latency per TX-done mode (#22 on), extra-RAM test (E906 uses DSP RAM, the DRAM carveout and SRAM A3_2), PREEMPT and lockdep kernels. |

## Why runs failed

- **#5, #6**: the invariant breaks above.
- **#8, #9, #13**: a functional row failed (#9 did not run Profile 1). The history keeps metrics, not which row failed.
- **#14–16**: the TX-completion check, while that rule was being reworked (10-16/17).
- **#21**: 2 KUnit cases failed under PREEMPT_RT (the TX-done IRQ ran in its IRQ thread, outside the test's stubs) and Profile 3 did not run. Fixed in `97f6e462ba87`.
- **#23**: the new extra-RAM step ran before the FreeRTOS pings and broke them; moved after them (#24 PASS).

No driver regression in the v3 runs. Note: from #21 to #27 two source checks (TX completion, drain bound) reported FAIL on correct code, because they matched text the 10-26 refactor changed; they do not count toward the result and were fixed on 10-29 (`b44a031`).

## Reading the numbers

- Profile 1: rpmsg with vrings in DRAM. Profile 2: vrings in SRAM. Profile 3: userspace UIO straight to the mailbox and SRAM, no rpmsg.
- RT = PREEMPT_RT, the board's normal kernel. On PREEMPT (non-RT) the interrupt paths are faster, because RT runs interrupt handlers in threads. Lockdep costs 2–3x on every lock-heavy path, as expected.
- The range check compares each run with the median of the last 10 PASS runs of the same kernel flavor; a SLOW result fails the sweep.
