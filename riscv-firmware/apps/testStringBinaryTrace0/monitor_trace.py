#!/usr/bin/env python3
"""
monitor_trace.py - Linux Host RemoteProc Trace Monitor & Telemetry Decoder

Monitors /sys/kernel/debug/remoteproc/remoteproc0/trace0 for live RISC-V co-processor
output on Allwinner T527 / Radxa Cubie A5E.

Features:
- Live streaming of ASCII log strings (STRING:)
- Dynamic unpacking and decoding of packed binary telemetry structs (BINARY:)
- Human-readable hex dump parsing (HEXDUMP:)
- Circular buffer tracking without duplicates

Trade-offs & Architectural Notes:
- Why Polling is Required (No epoll support):
  In Linux upstream RemoteProc (drivers/remoteproc/remoteproc_debugfs.c), trace0
  is exposed via simple debugfs file_operations (.read, .open, .llseek).
  It does NOT implement .poll and has NO wait_queue or hardware doorbell attached.
  Attempting to register trace0 with epoll_ctl() returns EPERM (Operation not permitted).
- CPU Utilization:
  Because epoll is unsupported, host software must poll (read with sleep).
  This consumes CPU cycles and introduces polling jitter.
- Phase 2 Transition:
  Phase 2 samples (testPingRpmsg / /dev/rpmsg0) use hardware Mailbox interrupts
  wired to virtio_rpmsg_bus wait-queues, enabling true epoll with 0% CPU idle wait.
"""

import os
import sys
import time
import struct
import argparse

DEFAULT_TRACE_PATH = "/sys/kernel/debug/remoteproc/remoteproc0/trace0"

# Matches: struct __attribute__((packed)) TelemetryPacket (36 bytes total)
# uint32_t header_magic  (0x54454C4D = "TELM")
# uint32_t sequence
# uint32_t uptime_ms
# float    accel_x, accel_y, accel_z
# double   sine_wave
# uint16_t checksum
# uint16_t tail_magic    (0x55AA)
PKT_FMT = "<IIIfff d HH"
PKT_SIZE = struct.calcsize(PKT_FMT)

ANSI_GREEN  = "\033[92m"
ANSI_CYAN   = "\033[96m"
ANSI_YELLOW = "\033[93m"
ANSI_BLUE   = "\033[94m"
ANSI_RED    = "\033[91m"
ANSI_RESET  = "\033[0m"
ANSI_BOLD   = "\033[1m"

def parse_binary_packet(raw_bytes):
    if len(raw_bytes) < PKT_SIZE:
        return None
    try:
        magic, seq, uptime, ax, ay, az, sin_val, csum, tail = struct.unpack(PKT_FMT, raw_bytes[:PKT_SIZE])
        if magic == 0x54454C4D and tail == 0x55AA:
            return {
                "seq": seq,
                "uptime_ms": uptime,
                "accel": (ax, ay, az),
                "sin": sin_val,
                "csum": csum
            }
    except Exception:
        pass
    return None

def main():
    parser = argparse.ArgumentParser(description="Monitor RemoteProc Trace0 with Mixed ASCII/Binary Decoding")
    parser.add_argument("--trace", "-t", default=DEFAULT_TRACE_PATH, help=f"Path to trace0 (default: {DEFAULT_TRACE_PATH})")
    parser.add_argument("--poll", "-p", type=float, default=0.05, help="Polling interval in seconds (default: 0.05)")
    parser.add_argument("--raw", "-r", action="store_true", help="Print raw lines without formatting")
    args = parser.parse_args()

    if not os.path.exists(args.trace):
        print(f"{ANSI_RED}Error: Trace file '{args.trace}' does not exist.{ANSI_RESET}")
        print("Ensure the RISC-V core is booted via remoteproc:")
        print("  echo riscv-firmware.elf > /sys/class/remoteproc/remoteproc0/firmware")
        print("  echo start > /sys/class/remoteproc/remoteproc0/state")
        sys.exit(1)

    print(f"{ANSI_BOLD}{ANSI_CYAN}=== RemoteProc Trace0 Live Monitor (T527 E907) ==={ANSI_RESET}")
    print(f"Target: {args.trace} | Packet Size: {PKT_SIZE} bytes | Poll: {args.poll*1000:.0f}ms")
    print(f"{ANSI_YELLOW}Note: debugfs polling eats CPU cycles. Phase 2 introduces hardware Mailboxes for 0% CPU wait.{ANSI_RESET}\n")

    last_content = b""

    try:
        while True:
            with open(args.trace, "rb") as f:
                content = f.read()

            if content != last_content:
                # Detect whether content was appended or buffer wrapped
                if content.startswith(last_content) and len(last_content) > 0:
                    chunk = content[len(last_content):]
                else:
                    chunk = content
                last_content = content

                lines = chunk.split(b"\n")
                for line in lines:
                    if not line:
                        continue

                    if args.raw:
                        sys.stdout.buffer.write(line + b"\n")
                        sys.stdout.flush()
                        continue

                    if line.startswith(b"STRING:"):
                        text = line[7:].decode("latin1", errors="replace").strip()
                        print(f"{ANSI_GREEN}[ASCII]  {ANSI_RESET} {text}")
                    elif line.startswith(b"BINARY:"):
                        bin_data = line[7:]
                        pkt = parse_binary_packet(bin_data)
                        if pkt:
                            print(f"{ANSI_BLUE}[STRUCT] {ANSI_RESET} "
                                  f"Seq #{pkt['seq']:<5} | "
                                  f"Up: {pkt['uptime_ms']:<6}ms | "
                                  f"Accel: ({pkt['accel'][0]:+6.3f}, {pkt['accel'][1]:+6.3f}, {pkt['accel'][2]:+6.3f}) | "
                                  f"FPU Sin: {pkt['sin']:+.4f} | "
                                  f"Csum: 0x{pkt['csum']:04X}")
                        else:
                            print(f"{ANSI_YELLOW}[BIN-RAW]{ANSI_RESET} {len(bin_data)} bytes: {bin_data[:16].hex()}...")
                    elif line.startswith(b"HEXDUMP:"):
                        print(f"{ANSI_CYAN}[HEXDUMP]{ANSI_RESET}")
                    elif line.startswith(b"0x"):
                        print(f"         {line.decode('latin1', errors='replace')}")
                    else:
                        text = line.decode("latin1", errors="replace")
                        print(f"  {text}")

            time.sleep(args.poll)

    except KeyboardInterrupt:
        print(f"\n{ANSI_CYAN}Stopped monitoring trace0.{ANSI_RESET}")

if __name__ == "__main__":
    main()
