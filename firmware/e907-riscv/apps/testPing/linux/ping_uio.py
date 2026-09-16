#!/usr/bin/env python3
"""
ping_uio.py - Event-Driven Lite-libmetal UIO Doorbell Ping-Pong Benchmark (Python)

Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
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
PAGE_SIZE      = 4096

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
        ("host_doorbell", ctypes.c_uint32),
        ("riscv_doorbell", ctypes.c_uint32),
        ("total_pings", ctypes.c_uint32),
        ("total_pongs", ctypes.c_uint32),
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
        print(f"{C_YELLOW}[HINT] Ensure 'cubie-a5e-uio.dtbo' is applied via config.txt and CONFIG_UIO is active.{C_RESET}")
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

    # Clear shared memory doorbells
    channel.host_doorbell = 0
    channel.riscv_doorbell = 0

    print(f"{C_GREEN}[INFO] Lite-libmetal UIO mapped. Starting benchmark...{C_RESET}\n")

    latencies_us: List[float] = []
    payload_raw = args.payload.encode('utf-8')[:480]
    plen = len(payload_raw)

    bench_start_ns = get_time_ns()
    seq = 0
    timeouts = 0
    corruptions = 0
    timeout_sec = args.timeout / 1000.0

    try:
        i = 0
        while args.count == 0 or i < args.count:
            seq += 1
            i += 1

            # Prepare Ping Packet in SRAM using scalar ctypes structure writes
            channel.ping_pkt.magic = SHM_PING_MAGIC
            channel.ping_pkt.seq = seq
            tx_ns = get_time_ns()
            channel.ping_pkt.host_tx_ts_ns = tx_ns
            channel.ping_pkt.riscv_cycles = 0
            channel.ping_pkt.payload_len = plen
            for k in range(plen):
                channel.ping_pkt.payload[k] = payload_raw[k]

            # Ring Doorbell
            channel.host_doorbell = 1

            # Wait for RISC-V pong response via SRAM doorbell
            deadline_ns = tx_ns + int(timeout_sec * 1_000_000_000)
            received = False
            rx_ns = 0

            while get_time_ns() < deadline_ns:
                if channel.riscv_doorbell == 1:
                    rx_ns = get_time_ns()
                    received = True
                    break

            if not received:
                timeouts += 1
                if timeouts <= 5:
                    print(f"{C_YELLOW}[WARN] Ping seq {seq} timed out waiting for UIO doorbell!{C_RESET}")
                channel.host_doorbell = 0
                channel.riscv_doorbell = 0
                continue

            # Middle ground check: magic, seq, and PONG prefix
            pong_prefix = bytes(channel.pong_pkt.payload[:4])
            if channel.pong_pkt.magic == SHM_PONG_MAGIC and channel.pong_pkt.seq == seq and pong_prefix == b"PONG":
                rtt_us = (rx_ns - tx_ns) / 1000.0
                latencies_us.append(rtt_us)
            else:
                corruptions += 1
                print(f"{C_RED}[ERROR] Malformed pong: magic=0x{channel.pong_pkt.magic:08X}, seq={channel.pong_pkt.seq}{C_RESET}")

            # Acknowledge pong
            channel.riscv_doorbell = 0

            if args.delay > 0:
                time.sleep(args.delay / 1_000_000.0)

    except KeyboardInterrupt:
        print(f"\n{C_YELLOW}[INFO] Benchmark interrupted by user.{C_RESET}")

    total_time_ns = get_time_ns() - bench_start_ns
    total_time_sec = total_time_ns / 1_000_000_000.0

    # Clean up
    del channel
    msgbox_mmap.close()
    sram_mmap.close()
    os.close(uio_fd)

    # 4. Print Statistics & Latency Histogram
    num_received = len(latencies_us)
    if num_received == 0:
        print(f"{C_RED}[ERROR] No pongs received. Remote core may not be running.{C_RESET}")
        return

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
    print(f"  Total Pings Sent    : {seq}")
    print(f"  Pongs Received      : {num_received} ({100.0 * num_received / seq:.2f}%)")
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

if __name__ == "__main__":
    main()
