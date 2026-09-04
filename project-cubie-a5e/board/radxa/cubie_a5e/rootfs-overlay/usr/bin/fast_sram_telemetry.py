#!/usr/bin/env python3
"""
fast_sram_telemetry.py - Ultra-Low-Latency Direct SRAM Telemetry Reader (Lite/Fast)

Directly memory-maps Dedicated MCU SRAM C (0x07131000) via /dev/mem to poll
the packed binary TelemetryPacket with zero kernel copies and microsecond latency.

Trade-offs & Architectural Notes:
- Why Polling is Required (No epoll support):
  Direct SRAM mapping via /dev/mem maps raw physical address pages into user-space.
  Like trace0, /dev/mem has NO wait_queue, NO event notification, and NO .poll method.
  Attempting to register /dev/mem with epoll_ctl() returns EPERM (Operation not permitted).
- CPU Utilization:
  Without hardware doorbell interrupts waking a kernel thread, the host must
  poll memory in a loop (e.g. 500us sleep), which burns host CPU cycles.
- Phase 2 Transition:
  Phase 2 samples (testPing, testPingRpmsg, testDRAMMsg) introduce hardware Mailbox
  doorbell interrupts. This enables zero-copy shared memory or VirtIO queues to sleep
  in the kernel with true 0% CPU utilization until the E907 kicks the doorbell.
"""

import os
import sys
import time
import mmap
import struct
import argparse

SRAM_C_BASE = 0x07130000
TELEMETRY_OFFSET = 0x1000       # 0x07131000 (directly after 4KB trace_buffer)
MAP_SIZE = 0x2000               # 8 KB window

# Matches: struct __attribute__((packed)) TelemetryPacket (36 bytes)
PKT_FMT = "<IIIfff d HH"
PKT_SIZE = struct.calcsize(PKT_FMT)

ANSI_GREEN  = "\033[92m"
ANSI_CYAN   = "\033[96m"
ANSI_YELLOW = "\033[93m"
ANSI_BLUE   = "\033[94m"
ANSI_RED    = "\033[91m"
ANSI_RESET  = "\033[0m"
ANSI_BOLD   = "\033[1m"

def main():
    parser = argparse.ArgumentParser(description="Direct Zero-Copy SRAM Telemetry Poller (Lite/Fast)")
    parser.add_argument("--dev", "-d", default="/dev/mem", help="Memory device (default: /dev/mem)")
    parser.add_argument("--addr", "-a", type=lambda x: int(x, 0), default=SRAM_C_BASE,
                        help="SRAM Base physical address (default: 0x07130000)")
    parser.add_argument("--offset", "-o", type=lambda x: int(x, 0), default=TELEMETRY_OFFSET,
                        help="Telemetry packet offset in SRAM (default: 0x1000)")
    parser.add_argument("--count", "-n", type=int, default=0, help="Number of packets to read (0 = continuous)")
    args = parser.parse_args()

    target_phys = args.addr + args.offset
    print(f"{ANSI_BOLD}{ANSI_CYAN}=== Direct Zero-Copy SRAM Telemetry Reader (Lite/Fast) ==={ANSI_RESET}")
    print(f"Device: {args.dev} | Physical Target: 0x{target_phys:08X} | Packet Size: {PKT_SIZE} bytes")
    print(f"Mode: Direct physical mmap (bypasses debugfs / kernel filesystem layers)")
    print(f"{ANSI_YELLOW}Note: Memory polling consumes CPU cycles. Phase 2 introduces hardware Mailbox doorbells for 0% CPU wait.{ANSI_RESET}\n")

    try:
        fd = os.open(args.dev, os.O_RDWR | os.O_SYNC)
    except PermissionError:
        print(f"{ANSI_RED}Error: Permission denied opening {args.dev}. Must run as root/sudo!{ANSI_RESET}")
        sys.exit(1)
    except FileNotFoundError:
        print(f"{ANSI_RED}Error: {args.dev} not found.{ANSI_RESET}")
        sys.exit(1)

    # Memory map the 8KB SRAM window
    page_mask = ~(mmap.PAGESIZE - 1)
    page_base = args.addr & page_mask
    page_offset = args.addr - page_base

    mem = mmap.mmap(fd, MAP_SIZE, mmap.MAP_SHARED, mmap.PROT_READ, offset=page_base)
    pkt_offset = page_offset + args.offset

    last_seq = 0
    read_count = 0
    start_time = time.time()
    last_stat_time = start_time
    stat_packets = 0

    try:
        while True:
            # Zero-copy read directly from physical SRAM memory
            mem.seek(pkt_offset)
            raw = mem.read(PKT_SIZE)

            if len(raw) == PKT_SIZE:
                magic, seq, uptime, ax, ay, az, sin_val, csum, tail = struct.unpack(PKT_FMT, raw)

                if magic == 0x54454C4D and tail == 0x55AA:
                    if seq != last_seq:
                        last_seq = seq
                        read_count += 1
                        stat_packets += 1

                        now = time.time()
                        fps = stat_packets / (now - last_stat_time) if (now - last_stat_time) > 0.001 else 0.0

                        print(f"{ANSI_GREEN}[SRAM-DIRECT]{ANSI_RESET} "
                              f"Seq #{seq:<6} | "
                              f"Up: {uptime:<6}ms | "
                              f"Accel: ({ax:+6.3f}, {ay:+6.3f}, {az:+6.3f}) | "
                              f"FPU Sin: {sin_val:+.4f} | "
                              f"Rate: {fps:5.1f} Hz")

                        if now - last_stat_time >= 2.0:
                            last_stat_time = now
                            stat_packets = 0

                        if args.count > 0 and read_count >= args.count:
                            break
                else:
                    # Packet not initialized yet
                    time.sleep(0.01)

            time.sleep(0.0005) # 500us tight poll

    except KeyboardInterrupt:
        print(f"\n{ANSI_CYAN}Stopped SRAM telemetry monitor.{ANSI_RESET}")
    finally:
        mem.close()
        os.close(fd)

if __name__ == "__main__":
    main()
