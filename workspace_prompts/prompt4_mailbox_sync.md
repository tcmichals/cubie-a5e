# Blueprint 4: Bidirectional Mailbox Doorbell & Raw Shared Memory Link

## 1. Mandated Rules
* **LIGHTWEIGHT EXECUTION:** Do not include heavy, high-overhead frameworks like OpenAMP or RPMsg.
* **MAINLINE DRIVER FIRST:** Extend baseline driver models natively under standard directory hierarchies (`drivers/mailbox/`).
* **DETERMINISTIC BOUNDS:** Coordinate interactions using fixed memory pointer windows in shared SRAM.

## 2. Context & Origins
* **Where this comes from:** This low-overhead IPC model implements a hardware doorbell mechanism based on a raw ZynqMP OCM-to-R5 structural architecture. The low-level lane routing mechanics are reverse-engineered directly from the vendor's `drivers/mailbox/sunxi-mailbox.c` implementation.

## 3. Engineering Goals
* Build an out-of-tree driver patch enabling ultra-low-latency state-machine handshakes between the ARM Cortex-A55 Linux host and the XuanTie RISC-V real-time core.

## 4. Implementation Phases
### Phase 1: Kernel Mailbox Extension Patch
* Isolate the hardware-specific register management loops from the vendor code. Rewrite them into a clean, independent mainline-compliant extension driver mapped inside `drivers/mailbox/`.

### Phase 2: RISC-V Mailbox Doorbell Handler
* Implement an immediate interrupt service routine (`sunxi_mailbox_isr()`) pinned inside the RISC-V core's ITCM space. This handler must instantly parse incoming pointer address signals from fixed Message SRAM blocks without execution delay.

## 5. Trace Logging & Documentation Plan
* **MANDATORY LOG:** Generate `prompt4_mailbox_sync_trace.md`. This log must track data latency boundaries, provide a complete memory map of the shared SRAM window offsets, and document the state-machine handshake states.
* **ARTIFACT:** Output `.antigravity/patches/0003-mailbox-sunxi-t527-driver.patch`.
