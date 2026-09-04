#!/usr/bin/env python3
"""
ping_rpmsg.py - Standard Linux RPMsg Ping-Pong Host Benchmark (Python)

Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
Protocol: Standard Linux virtio_rpmsg_bus character interface (/dev/rpmsg0 or /dev/rpmsg_ctrl0)

Features:
 - Event-driven I/O using select() with 0% idle CPU utilization
 - Automatic endpoint creation via /dev/rpmsg_ctrl0 if needed
 - Precise Round-Trip Time (RTT) measurements with nanosecond timestamps
 - Statistics: Min, Average, Max, Jitter, Percentiles (p50, p90, p99), Throughput
"""

import os
import sys
import time
import struct
import fcntl
import select
import argparse
from typing import List, Optional

# ANSI Escape Colors
C_RESET   = "\033[0m"
C_BOLD    = "\033[1m"
C_RED     = "\033[31m"
C_GREEN   = "\033[32m"
C_YELLOW  = "\033[33m"
C_BLUE    = "\033[34m"
C_CYAN    = "\033[36m"

# Standard Linux RPMsg ioctl definitions
# struct rpmsg_endpoint_info: char name[32], u32 src, u32 dst
RPMSG_CREATE_EPT_IOCTL = 0x4028B501   # _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
RPMSG_PING_EPT_ADDR    = 1024
RPMSG_PAYLOAD_FMT      = "<IQ48s"      # uint32 seq, uint64 ts_ns, char text[48]
RPMSG_PAYLOAD_LEN      = struct.calcsize(RPMSG_PAYLOAD_FMT)

def create_endpoint_from_ctrl(ctrl_path: str, name: str = "rpmsg-ping-channel",
                              src: int = RPMSG_PING_EPT_ADDR,
                              dst: int = RPMSG_PING_EPT_ADDR) -> Optional[str]:
    """Create an RPMsg endpoint via control device ioctl."""
    try:
        ctrl_fd = os.open(ctrl_path, os.O_RDWR)
    except OSError as e:
        print(f"{C_RED}[ERROR] Failed to open control device {ctrl_path}: {e}{C_RESET}")
        return None

    try:
        name_bytes = name.encode('utf-8')[:31]
        name_bytes = name_bytes.ljust(32, b'\x00')
        ept_info = struct.pack("32sII", name_bytes, src, dst)
        fcntl.ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, ept_info)
        print(f"{C_GREEN}[INFO] Created endpoint '{name}' via {ctrl_path}{C_RESET}")
    except OSError as e:
        # If already exists, proceed
        print(f"{C_YELLOW}[WARN] ioctl CREATE_EPT returned: {e} (might already exist){C_RESET}")
    finally:
        os.close(ctrl_fd)

    # Wait for dynamic udev device creation
    candidate = "/dev/rpmsg0"
    for _ in range(10):
        if os.path.exists(candidate):
            return candidate
        time.sleep(0.05)
    return candidate

def auto_find_device() -> str:
    """Auto-detect available RPMsg devices in sysfs / dev."""
    candidates = [
        "/dev/rpmsg0",
        "/dev/rpmsg_ctrl0",
        "/dev/ttyRPMSG0",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return "/dev/rpmsg0"

def main():
    parser = argparse.ArgumentParser(description="Standard Linux RPMsg Ping-Pong Host Benchmark (Python)")
    parser.add_argument("-d", "--dev", default="", help="RPMsg device path (default: auto-detect)")
    parser.add_argument("-n", "--count", type=int, default=1000, help="Number of pings (default: 1000, 0=continuous)")
    parser.add_argument("-s", "--sleep", type=int, default=1000, help="Sleep between pings in microseconds (default: 1000)")
    parser.add_argument("-p", "--payload", default="Ping from Linux RPMsg Python", help="Custom payload string")
    parser.add_argument("-t", "--timeout", type=float, default=1000.0, help="Pong timeout in milliseconds (default: 1000)")
    args = parser.parse_args()

    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}  Allwinner T527 Linux VirtIO RPMsg Ping-Pong Benchmark (Python){C_RESET}")
    print(f"{C_CYAN}  Protocol: Linux kernel virtio_rpmsg_bus (/dev/rpmsg0)         {C_RESET}")
    print(f"{C_CYAN}  Channel : rpmsg-ping-channel (Endpoint Addr: 1024)            {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}\n")

    dev_path = args.dev if args.dev else auto_find_device()
    print(f"[INFO] Initial device selection: {dev_path}")

    # Handle control device endpoint creation if needed
    if "rpmsg_ctrl" in dev_path:
        created_dev = create_endpoint_from_ctrl(dev_path)
        if created_dev and os.path.exists(created_dev):
            dev_path = created_dev
        else:
            dev_path = "/dev/rpmsg0"

    print(f"{C_GREEN}[INFO] Opening RPMsg device: {dev_path}{C_RESET}")
    try:
        fd = os.open(dev_path, os.O_RDWR | os.O_NONBLOCK)
    except OSError as e:
        print(f"{C_RED}[FATAL] Could not open {dev_path}: {e}{C_RESET}")
        print(f"{C_YELLOW}[HINT] Ensure remoteproc is started and /sys/class/remoteproc/remoteproc0/state is 'running'.{C_RESET}")
        sys.exit(1)

    payload_bytes = args.payload.encode('utf-8')[:47].ljust(48, b'\x00')
    timeout_sec = args.timeout / 1000.0
    sleep_sec = args.sleep / 1_000_000.0

    latencies_us: List[float] = []
    seq = 0
    timeouts = 0
    bench_start_ns = time.monotonic_ns()

    print(f"[INFO] Starting benchmark: count={args.count}, delay={args.sleep}us, timeout={args.timeout}ms...\n")

    try:
        while args.count == 0 or seq < args.count:
            seq += 1
            tx_ns = time.monotonic_ns()
            tx_pkt = struct.pack(RPMSG_PAYLOAD_FMT, seq, tx_ns, payload_bytes)

            try:
                os.write(fd, tx_pkt)
            except OSError as e:
                print(f"{C_RED}[ERROR] write() failed on seq={seq}: {e}{C_RESET}")
                break

            # Event-driven wait with select (0% CPU idle consumption)
            rlist, _, _ = select.select([fd], [], [], timeout_sec)
            rx_ns = time.monotonic_ns()

            if rlist:
                try:
                    rx_data = os.read(fd, 512)
                    if rx_data:
                        rtt_us = (rx_ns - tx_ns) / 1000.0
                        latencies_us.append(rtt_us)
                    else:
                        timeouts += 1
                except OSError as e:
                    print(f"{C_YELLOW}[WARN] read() error on seq={seq}: {e}{C_RESET}")
                    timeouts += 1
            else:
                timeouts += 1
                if timeouts <= 5 or timeouts % 100 == 0:
                    print(f"{C_YELLOW}[WARN] Ping seq={seq} TIMEOUT ({args.timeout} ms){C_RESET}")

            if sleep_sec > 0:
                time.sleep(sleep_sec)

            if seq % 200 == 0 and latencies_us:
                sys.stdout.write(f"  Progress: {seq} sent | Last RTT: {latencies_us[-1]:.2f} us | Timeouts: {timeouts}\r")
                sys.stdout.flush()

    except KeyboardInterrupt:
        print(f"\n{C_YELLOW}[INFO] Interrupted by user.{C_RESET}")
    finally:
        os.close(fd)

    bench_end_ns = time.monotonic_ns()
    total_elapsed_ms = (bench_end_ns - bench_start_ns) / 1_000_000.0

    # Print Summary Statistics
    print(f"\n\n{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"{C_CYAN}{C_BOLD}                    RPMsg Benchmark Results                     {C_RESET}")
    print(f"{C_CYAN}{C_BOLD}================================================================{C_RESET}")
    print(f"  Total Packets Sent : {C_BOLD}{seq}{C_RESET}")
    print(f"  Successful Replies : {C_GREEN}{len(latencies_us)}{C_RESET}")
    print(f"  Timed Out Packets  : {C_RED if timeouts else C_GREEN}{timeouts}{C_RESET}")
    print(f"  Total Test Time    : {total_elapsed_ms:.2f} ms")

    if latencies_us:
        min_lat = min(latencies_us)
        max_lat = max(latencies_us)
        avg_lat = sum(latencies_us) / len(latencies_us)

        # Variance / Jitter
        variance = sum((x - avg_lat) ** 2 for x in latencies_us) / len(latencies_us)
        jitter = variance ** 0.5

        sorted_lat = sorted(latencies_us)
        p50 = sorted_lat[int(len(sorted_lat) * 0.50)]
        p90 = sorted_lat[int(len(sorted_lat) * 0.90)]
        p99 = sorted_lat[int(len(sorted_lat) * 0.99)]
        throughput = (len(latencies_us) / (total_elapsed_ms / 1000.0)) if total_elapsed_ms > 0 else 0

        print(f"  Throughput         : {C_BOLD}{throughput:.1f} msgs/sec{C_RESET}")
        print(f"----------------------------------------------------------------")
        print(f"  Min Latency        : {C_GREEN}{min_lat:.2f} us{C_RESET}")
        print(f"  Average Latency    : {C_BOLD}{avg_lat:.2f} us{C_RESET}")
        print(f"  Max Latency        : {C_YELLOW if max_lat < 500 else C_RED}{max_lat:.2f} us{C_RESET}")
        print(f"  Jitter (Std Dev)   : {jitter:.2f} us")
        print(f"----------------------------------------------------------------")
        print(f"  Percentile p50     : {p50:.2f} us")
        print(f"  Percentile p90     : {p90:.2f} us")
        print(f"  Percentile p99     : {p99:.2f} us")
    print(f"{C_CYAN}================================================================{C_RESET}\n")

if __name__ == "__main__":
    main()
