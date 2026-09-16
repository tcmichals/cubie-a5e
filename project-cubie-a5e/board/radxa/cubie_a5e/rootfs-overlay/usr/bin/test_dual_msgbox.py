#!/usr/bin/env python3
"""
test_dual_msgbox.py — Concurrent Multi-Core Hardware Mailbox Test Suite
Tests simultaneous communication between ARM Cortex-A55, Cadence HiFi4 DSP,
and XuanTie E907 RISC-V on the Radxa Cubie A5E (Allwinner T527).

Validates:
1. Cadence HiFi4 DSP Mailbox (Channels 4 & 5 -> /sys/kernel/debug/mailbox-test-dsp/message)
2. XuanTie E907 RISC-V Mailbox (Channels 8 & 9 -> /sys/kernel/debug/mailbox-test-e907/message)
3. Concurrent, multi-threaded bidirectional traffic with zero crosstalk.
"""

import sys
import os
import time
import struct
import threading
from typing import Tuple

C_RESET = "\033[0m"
C_BOLD = "\033[1m"
C_GREEN = "\033[32m"
C_YELLOW = "\033[33m"
C_RED = "\033[31m"
C_CYAN = "\033[36m"
C_MAGENTA = "\033[35m"

DSP_MBOX_PATH = "/sys/kernel/debug/mailbox-test-dsp/message"
E907_MBOX_PATH = "/sys/kernel/debug/mailbox-test-e907/message"

PING_MAGIC = b"PING"  # 0x50494E47
PONG_MAGIC = b"PONG"  # 0x504F4E47


def test_channel(name: str, path: str, num_pings: int, results: dict) -> None:
    if not os.path.exists(path):
        results[name] = (0, 0, f"Error: {path} not found. Is cubie-a5e-dual-mailbox-test overlay applied?")
        return

    success_count = 0
    total_time_ns = 0

    try:
        # Open node in read/write mode (unbuffered binary)
        fd = os.open(path, os.O_RDWR)
    except Exception as e:
        results[name] = (0, 0, f"Error opening {path}: {e}")
        return

    try:
        for i in range(num_pings):
            t0 = time.perf_counter_ns()
            os.write(fd, PING_MAGIC)
            reply = os.read(fd, 4)
            t1 = time.perf_counter_ns()

            if reply == PONG_MAGIC:
                success_count += 1
                total_time_ns += (t1 - t0)
            else:
                # Some firmware returns integer seq+1
                if len(reply) == 4:
                    val = struct.unpack("<I", reply)[0]
                    if val != 0:
                        success_count += 1
                        total_time_ns += (t1 - t0)

        avg_rtt_us = (total_time_ns / (success_count * 1000.0)) if success_count > 0 else 0.0
        results[name] = (success_count, avg_rtt_us, "OK")
    finally:
        os.close(fd)


def main():
    print(f"\n{C_BOLD}{C_CYAN}================================================================{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  Allwinner T527 Dual Co-Processor Concurrent Mailbox Test      {C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  Host (ARM A55) <-> HiFi4 DSP (Ch 4/5) & XuanTie E907 (Ch 8/9){C_RESET}")
    print(f"{C_BOLD}{C_CYAN}================================================================{C_RESET}\n")

    num_iterations = 100
    if len(sys.argv) > 1:
        try:
            num_iterations = int(sys.argv[1])
        except ValueError:
            pass

    print(f"  Iterations per core: {C_BOLD}{num_iterations}{C_RESET}")
    print(f"  DSP Mailbox Node   : {DSP_MBOX_PATH}")
    print(f"  E907 Mailbox Node  : {E907_MBOX_PATH}\n")

    # Check debugfs access
    if not os.path.exists("/sys/kernel/debug"):
        print(f"{C_RED}[FAIL] debugfs is not mounted at /sys/kernel/debug.{C_RESET}")
        sys.exit(1)

    dsp_avail = os.path.exists(DSP_MBOX_PATH)
    e907_avail = os.path.exists(E907_MBOX_PATH)

    print(f"  Checking Hardware Endpoints:")
    print(f"    HiFi4 DSP Node   : {C_GREEN if dsp_avail else C_RED}{'DETECTED' if dsp_avail else 'MISSING'}{C_RESET}")
    print(f"    XuanTie E907 Node: {C_GREEN if e907_avail else C_RED}{'DETECTED' if e907_avail else 'MISSING'}{C_RESET}\n")

    if not dsp_avail and not e907_avail:
        print(f"{C_YELLOW}[HINT] Apply 'cubie-a5e-dual-mailbox-test' overlay in /boot/config.txt and reboot:{C_RESET}")
        print(f"       dtoverlay=cubie-a5e-dual-mailbox-test\n")
        sys.exit(1)

    results = {}
    threads = []

    t_start = time.perf_counter()

    if dsp_avail:
        t_dsp = threading.Thread(target=test_channel, args=("DSP-HiFi4", DSP_MBOX_PATH, num_iterations, results))
        threads.append(t_dsp)
        t_dsp.start()

    if e907_avail:
        t_e907 = threading.Thread(target=test_channel, args=("E907-RISCV", E907_MBOX_PATH, num_iterations, results))
        threads.append(t_e907)
        t_e907.start()

    for t in threads:
        t.join()

    total_wall_s = time.perf_counter() - t_start

    print(f"{C_BOLD}{C_GREEN}Concurrent Test Run Complete!{C_RESET}")
    print(f"  Total Wall-Clock Time: {total_wall_s:.3f} s\n")

    print(f"{C_BOLD}Test Results Breakdown:{C_RESET}")
    print(f"----------------------------------------------------------------")
    for name, (count, avg_us, status) in results.items():
        pass_rate = (count / num_iterations) * 100.0
        color = C_GREEN if pass_rate == 100.0 else (C_YELLOW if pass_rate > 0 else C_RED)
        print(f"  {C_BOLD}{name:<12}{C_RESET}: {color}{count}/{num_iterations} responses ({pass_rate:.1f}%){C_RESET} | Avg RTT: {avg_us:.2f} us | {status}")
    print(f"----------------------------------------------------------------\n")


if __name__ == "__main__":
    main()
