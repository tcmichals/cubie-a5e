# Cubie A5E / A7A mainline Linux

Mainline Linux (7.1, PREEMPT_RT) bring-up for the Radxa Cubie A5E (Allwinner A527/T527) and
Cubie A7A (A733): the message box and XuanTie E906 remoteproc drivers being upstreamed, the
E906 firmware, and the tests that run them on the board.

## Pages

- [Full Sweep Tests](Full-Sweep-Tests): every full sweep on the board since 2026-10-02, the
  current IPC performance, kernel flavors (PREEMPT_RT, PREEMPT, lockdep) and trends.

## In the repository

- [README](https://github.com/tcmichals/cubie-a5e#readme): boards, status, build.
- [Upstream patch series](https://github.com/tcmichals/cubie-a5e/tree/main/upstream-remoteproc):
  v1, v2, v3 patches, cover letters and review records.
- [Design decisions](https://github.com/tcmichals/cubie-a5e/blob/main/upstream-remoteproc/DESIGN_DECISIONS.md)
- [Firmware tests](https://github.com/tcmichals/cubie-a5e/blob/main/firmware/tests.md):
  the E906 test apps the sweep runs.
- [TODO](https://github.com/tcmichals/cubie-a5e/blob/main/TODO.md): task tracker.
