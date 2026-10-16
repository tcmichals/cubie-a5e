#!/usr/bin/env python3
"""
ping_uio.py - Event-Driven Lite-libmetal UIO Doorbell Ping-Pong Benchmark (Python)

Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E906
Pattern: Direct Shared SRAM + UIO Hardware Mailbox Doorbell ISR via select.epoll()

Features:
 - Pure scalar ctypes memory access (safe for ARM64 PROT_DEVICE_nGnRnE UIO mappings)
 - Userspace memory-mapped access to Mailbox registers (map0) and SRAM C (map1)
 - Precise Round-Trip Time (RTT) measurements with nanosecond precision
 - Data Integrity Verification (Middle Ground: PONG magic prefix & sequence counter matching)
 - Statistics: Min, Average, Max, Jitter, Percentiles (p50, p90, p99, p99.9), Throughput
 - Zero /dev/mem or root privilege requirement when /dev/uio0 permissions are granted
"""

import os
import sys
import time
import mmap
import ctypes
import select
import argparse
import math
from typing import List

# ANSI Escape Colors
C_RESET   = "\033[0m"
C_BOLD    = "\033[1m"
C_RED     = "\033[31m"
C_GREEN   = "\033[32m"
C_YELLOW  = "\033[33m"
C_BLUE    = "\033[34m"
C_CYAN    = "\033[36m"

# Protocol Constants (shm_ping_protocol.h)
SHM_PING_MAGIC = 0x50494E47  # "PING"
SHM_PONG_MAGIC = 0x504F4E47  # "PONG"
SHM_ERROR_MAGIC = 0x4552524F # "ERRO": the E906 rejected a malformed ping
PAGE_SIZE      = 4096

# ARM message box bank (UIO map 0), port 2 = the E906 link, slot 0 = its doorbell
MSGBOX_RD_IRQ_EN     = 0x220  # READ_IRQ_ENABLE(2)
MSGBOX_RD_IRQ_STATUS = 0x224  # READ_IRQ_STATUS(2), write 1 to clear
MSGBOX_MSG_STATUS    = 0x260  # MSG_STATUS(2, 0): entries queued
MSGBOX_MSG_FIFO      = 0x270  # MSG_FIFO(2, 0): a read pops one entry
MSGBOX_RD_IRQ_BIT    = 0x1


def pattern_byte(seq: int, i: int) -> int:
    return (seq * 31 + i * 7) & 0xFF

# CTypes layout for ShmPingChannel (MCU SRAM C)
class ShmPingPacket(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32),
        ("seq", ctypes.c_uint32),
        ("host_tx_ts_ns", ctypes.c_uint64),
        ("riscv_cycles", ctypes.c_uint64),
        ("payload_len", ctypes.c_uint32),
        ("payload", ctypes.c_uint8 * 484),
    ]

class ShmPingChannel(ctypes.Structure):
    _fields_ = [
        ("ping_seq", ctypes.c_uint32),     # written only by Linux
        ("pong_seq", ctypes.c_uint32),     # written only by the E906
        ("total_pings", ctypes.c_uint32),  # E906: pings answered
        ("errors", ctypes.c_uint32),       # E906: malformed pings rejected
        ("ping_pkt", ShmPingPacket),
        ("pong_pkt", ShmPingPacket),
    ]

def get_time_ns() -> int:
    return time.clock_gettime_ns(time.CLOCK_MONOTONIC_RAW)

def main():
    parser = argparse.ArgumentParser(description="Lite-libmetal UIO Doorbell Ping-Pong Benchmark (Python)")
    parser.add_argument("-u", "--uio-dev", default="/dev/uio0", help="UIO device node (default: /dev/uio0)")
    parser.add_argument("-n", "--count", type=int, default=10000, help="Number of pings (default: 10000, 0=continuous)")
    parser.add_argument("-d", "--delay", type=int, default=0, help="Delay between pings in microseconds (default: 0)")
    parser.add_argument("-p", "--payload", default="Ping from Linux UIO Python", help="Custom payload string")
    parser.add_argument("-t", "--timeout", type=float, default=1000.0, help="Pong timeout in milliseconds (default: 1000)")
    args = parser.parse_args()

    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}  Allwinner T527 Lite-libmetal UIO Ping-Pong Benchmark (Python)  {C_RESET}")
    print(f"{C_CYAN}  Device  : {args.uio_dev} (Mailbox Doorbell + Dedicated MCU SRAM C){C_RESET}")
    print(f"{C_CYAN}  Pattern : Event-driven select.epoll() ISR / Direct SRAM Doorbell{C_RESET}")
    print(f"{C_CYAN}  Count   : {args.count} iterations | Delay: {args.delay} us   {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}\n")

    # 1. Open UIO Device Node
    if not os.path.exists(args.uio_dev):
        print(f"{C_RED}[ERROR] UIO device '{args.uio_dev}' does not exist.{C_RESET}")
        print(f"{C_YELLOW}[HINT] Profile 3 needs base_dtb=sun55i-a527-cubie-a5e-uio.dtb and dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPing in /boot/config.txt.{C_RESET}")
        sys.exit(1)

    try:
        uio_fd = os.open(args.uio_dev, os.O_RDWR | os.O_SYNC)
    except OSError as e:
        print(f"{C_RED}[ERROR] Failed to open {args.uio_dev}: {e}{C_RESET}")
        sys.exit(1)

    # 2. Map UIO Regions directly through /dev/uio0
    # Map 0 (offset 0 * PAGE_SIZE): Mailbox MMIO registers (4 KB)
    # Map 1 (offset 1 * PAGE_SIZE): Dedicated MCU SRAM C   (4 KB)
    try:
        msgbox_mmap = mmap.mmap(uio_fd, PAGE_SIZE, mmap.MAP_SHARED, offset=0)
        sram_mmap   = mmap.mmap(uio_fd, PAGE_SIZE, mmap.MAP_SHARED, offset=PAGE_SIZE)
    except OSError as e:
        print(f"{C_RED}[ERROR] mmap on {args.uio_dev} failed: {e}{C_RESET}")
        os.close(uio_fd)
        sys.exit(1)

    channel = ShmPingChannel.from_buffer(sram_mmap)

    # Registers are read and written as single 32-bit accesses (ctypes c_uint32);
    # a byte-wise access to the FIFO register could pop more than one entry.
    def reg(off: int) -> ctypes.c_uint32:
        return ctypes.c_uint32.from_buffer(msgbox_mmap, off)

    def drain_doorbells() -> None:
        while reg(MSGBOX_MSG_STATUS).value & 0xF:
            _ = reg(MSGBOX_MSG_FIFO).value  # the read pops the entry
        reg(MSGBOX_RD_IRQ_STATUS).value = MSGBOX_RD_IRQ_BIT

    def unmask_irq() -> None:
        os.write(uio_fd, (1).to_bytes(4, "little"))

    # The message box must be clocked and out of reset, or no doorbell can
    # arrive. generic-uio does not switch either on. A clocked block keeps a
    # value written to its IRQ enable register; an unclocked one reads 0.
    reg(MSGBOX_RD_IRQ_EN).value = reg(MSGBOX_RD_IRQ_EN).value | MSGBOX_RD_IRQ_BIT
    if not reg(MSGBOX_RD_IRQ_EN).value & MSGBOX_RD_IRQ_BIT:
        print(f"{C_RED}[FAIL] message box not clocked or held in reset: IRQ enable at "
              f"0x{MSGBOX_RD_IRQ_EN:x} reads back 0{C_RESET}")
        sys.exit(1)
    drain_doorbells()
    ep = select.epoll()
    ep.register(uio_fd, select.EPOLLIN)

    print(f"{C_GREEN}[INFO] Lite-libmetal UIO mapped. Starting benchmark...{C_RESET}\n")

    latencies_us: List[float] = []
    psize = len(channel.ping_pkt.payload)
    timeout_ns = int(args.timeout * 1_000_000)

    # Continue after whatever an earlier run left: wait for the slot to be free.
    deadline = get_time_ns() + timeout_ns
    while channel.ping_seq != channel.pong_seq:
        if get_time_ns() >= deadline:
            print(f"{C_RED}[FAIL] slot busy: ping_seq {channel.ping_seq} never answered "
                  f"(pong_seq {channel.pong_seq}); is testPing.elf running?{C_RESET}")
            sys.exit(1)
    seq = channel.ping_seq

    bench_start_ns = get_time_ns()
    sent = 0
    timeouts = 0
    corruptions = 0
    irq_wakeups = 0
    irq_missing = False
    pong = None

    try:
        i = 0
        while args.count == 0 or i < args.count:
            seq = (seq + 1) & 0xFFFFFFFF
            i += 1
            sent += 1

            # Post the ping: packet first, then ping_seq announces it
            channel.ping_pkt.magic = SHM_PING_MAGIC
            channel.ping_pkt.seq = seq
            channel.ping_pkt.riscv_cycles = 0
            channel.ping_pkt.payload_len = psize
            for k in range(psize):
                channel.ping_pkt.payload[k] = pattern_byte(seq, k)
            tx_ns = get_time_ns()
            channel.ping_pkt.host_tx_ts_ns = tx_ns
            channel.ping_seq = seq

            # Sleep until the E906's doorbell interrupt; check pong_seq again
            # after unmasking so a pong that arrived first is not slept through.
            deadline = tx_ns + timeout_ns
            answered = False
            while True:
                if channel.pong_seq == seq:
                    answered = True
                    break
                unmask_irq()
                if channel.pong_seq == seq:
                    answered = True
                    break
                now = get_time_ns()
                if now >= deadline:
                    break
                if ep.poll((deadline - now) / 1e9):
                    os.read(uio_fd, 4)
                    irq_wakeups += 1
                    drain_doorbells()
                elif channel.pong_seq == seq:
                    # The pong arrived but its doorbell interrupt never did.
                    irq_missing = True
                    break
            rx_ns = get_time_ns()

            if irq_missing:
                print(f"{C_RED}[FAIL] seq {seq} answered, but no doorbell interrupt in "
                      f"{args.timeout:.0f} ms (is the message box clocked?){C_RESET}")
                break

            if not answered:
                timeouts += 1
                print(f"{C_RED}[FAIL] no pong for seq {seq} ({args.timeout:.0f} ms){C_RESET}")
                break

            # Check every byte of the pong
            pong = channel.pong_pkt
            magic = pong.magic
            # One element at a time: the mapping is device memory, where a bulk
            # copy (bytes(array)) can use unaligned accesses and raise SIGBUS.
            ok = (magic == SHM_PONG_MAGIC and pong.seq == seq and pong.host_tx_ts_ns == tx_ns
                  and [pong.payload[k] for k in range(4)] == list(b"PONG"))
            if ok:
                ok = all(pong.payload[k] == pattern_byte(seq, k) for k in range(4, psize))
            if not ok:
                corruptions += 1
                why = " (E906 rejected the ping)" if magic == SHM_ERROR_MAGIC else ""
                print(f"{C_RED}[ERROR] malformed pong for seq {seq}: magic=0x{magic:08X}, "
                      f"seq={pong.seq}{why}{C_RESET}")
                break

            latencies_us.append((rx_ns - tx_ns) / 1000.0)

            if args.delay > 0:
                time.sleep(args.delay / 1_000_000.0)

    except KeyboardInterrupt:
        print(f"\n{C_YELLOW}[INFO] Benchmark interrupted by user.{C_RESET}")

    total_time_ns = get_time_ns() - bench_start_ns
    total_time_sec = total_time_ns / 1_000_000_000.0

    # Clean up: drop every ctypes view of the mappings before closing them
    ep.close()
    pong = None
    del channel
    msgbox_mmap.close()
    sram_mmap.close()
    os.close(uio_fd)

    # 4. Print Statistics & Latency Histogram
    num_received = len(latencies_us)
    if num_received == 0:
        print(f"{C_RED}[ERROR] No pongs received.{C_RESET}")
        sys.exit(1)

    latencies_us.sort()
    min_lat = latencies_us[0]
    max_lat = latencies_us[-1]
    avg_lat = sum(latencies_us) / num_received

    variance = sum((x - avg_lat) ** 2 for x in latencies_us) / num_received
    std_dev = math.sqrt(variance)

    def percentile(p: float) -> float:
        idx = int(len(latencies_us) * (p / 100.0))
        return latencies_us[min(idx, len(latencies_us) - 1)]

    p50  = percentile(50.0)
    p90  = percentile(90.0)
    p99  = percentile(99.0)
    p999 = percentile(99.9)

    msg_rate = num_received / total_time_sec

    print(f"\n{C_GREEN}{C_BOLD}====================== BENCHMARK RESULTS ======================{C_RESET}")
    print(f"  Total Pings Sent    : {sent}")
    print(f"  Pongs Received      : {num_received} ({100.0 * num_received / sent:.2f}%)")
    print(f"  IRQ Wake-ups        : {irq_wakeups}")
    print(f"  Data Integrity      : {C_GREEN if corruptions == 0 else C_RED}{'PASS (0 corrupted / mismatched packets)' if corruptions == 0 else f'FAIL ({corruptions} errors)'}{C_RESET}")
    if corruptions > 0:
        print(f"  Corrupted Packets   : {corruptions}")
    print(f"  Timeouts            : {timeouts}")
    print(f"  Total Duration      : {total_time_sec:.4f} s")
    print(f"  Throughput Rate     : {C_BOLD}{msg_rate:,.1f} msgs/sec{C_RESET}")
    print(f"----------------------------------------------------------------")
    print(f"  Min RTT Latency     : {C_BOLD}{min_lat:.3f} us{C_RESET}")
    print(f"  Avg RTT Latency     : {C_BOLD}{avg_lat:.3f} us{C_RESET}")
    print(f"  Max RTT Latency     : {C_BOLD}{max_lat:.3f} us{C_RESET}")
    print(f"  Jitter (StdDev)     : {std_dev:.3f} us")
    print(f"----------------------------------------------------------------")
    print(f"  Percentile 50% (p50): {p50:.3f} us")
    print(f"  Percentile 90% (p90): {p90:.3f} us")
    print(f"  Percentile 99% (p99): {p99:.3f} us")
    print(f"  Percentile 99.9%    : {p999:.3f} us")
    print(f"{C_GREEN}{C_BOLD}================================================================{C_RESET}\n")

    if num_received != sent or timeouts or corruptions or irq_missing:
        sys.exit(1)

if __name__ == "__main__":
    main()
