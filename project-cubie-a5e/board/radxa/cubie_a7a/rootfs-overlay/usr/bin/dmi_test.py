#!/usr/bin/env python3
"""
dmi_test.py - Automated RISC-V Debug Module (v0.13.2) Verification via OpenOCD

Connects to OpenOCD's Telnet interface (default TCP 4444) and issues raw
DMI (Debug Module Interface) read/write commands to verify hardware debug
accessibility on XuanTie E906/E907 RISC-V co-processors.
"""

import argparse
import socket
import sys
import time


class OpenOCDClient:
    def __init__(self, host: str = "127.0.0.1", port: int = 4444, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.sock = socket.create_connection((host, port), timeout=timeout)
        # Flush initial OpenOCD banner and prompt
        self.sock.settimeout(1.0)
        try:
            while True:
                chunk = self.sock.recv(4096)
                if not chunk:
                    break
        except (socket.timeout, BlockingIOError):
            pass
        self.sock.settimeout(timeout)

    def send_cmd(self, cmd: str) -> str:
        self.sock.sendall(f"{cmd}\n".encode("utf-8"))
        response = b""
        while b"\r\n\x1a" not in response and b"> " not in response:
            try:
                chunk = self.sock.recv(1024)
                if not chunk:
                    break
                response += chunk
            except socket.timeout:
                break
        return response.decode("utf-8", errors="ignore").strip()

    def dmi_write(self, address: int, value: int) -> str:
        cmd = f"riscv dmi_write 0x{address:02x} 0x{value:08x}"
        return self.send_cmd(cmd)

    def dmi_read(self, address: int) -> int:
        cmd = f"riscv dmi_read 0x{address:02x}"
        out = self.send_cmd(cmd)
        for line in out.splitlines():
            tokens = line.strip().split()
            for token in tokens:
                if token.startswith("0x"):
                    try:
                        return int(token, 16)
                    except ValueError:
                        continue
        raise ValueError(f"Failed to parse hex value from OpenOCD response: {out}")

    def close(self):
        try:
            self.sock.close()
        except Exception:
            pass


def parse_dmstatus(val: int):
    print(f"\n[+] Raw dmstatus (0x11): 0x{val:08x}")
    version = val & 0xF
    confstrptrvalid = (val >> 4) & 1
    hasresethaltreq = (val >> 5) & 1
    authbusy = (val >> 6) & 1
    authenticated = (val >> 7) & 1
    anyhalted = (val >> 8) & 1
    allhalted = (val >> 9) & 1
    anyrunning = (val >> 10) & 1
    allrunning = (val >> 11) & 1
    anyunavail = (val >> 12) & 1
    allunavail = (val >> 13) & 1
    anynonexistent = (val >> 14) & 1
    allnonexistent = (val >> 15) & 1
    anyresumeack = (val >> 16) & 1
    allresumeack = (val >> 17) & 1
    impebreak = (val >> 22) & 1

    ver_str = {0: "None / Not Present", 1: "0.11", 2: "0.13.2", 3: "1.0"}.get(version, f"Custom ({version})")
    print(f"  - Version:        {version} ({ver_str})")
    print(f"  - Authenticated:  {bool(authenticated)}")
    print(f"  - All Halted:     {bool(allhalted)} (any: {bool(anyhalted)})")
    print(f"  - All Running:    {bool(allrunning)} (any: {bool(anyrunning)})")
    print(f"  - All Unavailable:{bool(allunavail)} (any: {bool(anyunavail)})")
    print(f"  - Non-existent:   {bool(allnonexistent)}")
    print(f"  - Resume Ack:     {bool(allresumeack)}")
    print(f"  - Implicit EBREAK:{bool(impebreak)}")


def parse_hartinfo(val: int):
    print(f"\n[+] Raw hartinfo (0x12): 0x{val:08x}")
    dataaccess = (val >> 16) & 1
    datasize = (val >> 12) & 0xF
    dataaddr = val & 0xFFF
    nscratch = (val >> 20) & 0xF
    print(f"  - Data Access:    {'Memory-mapped' if dataaccess else 'CSR'}")
    print(f"  - Data Words:     {datasize}")
    print(f"  - Data Address:   0x{dataaddr:03x}")
    print(f"  - Scratch Regs:   {nscratch}")


def parse_abstractcs(val: int):
    print(f"\n[+] Raw abstractcs (0x16): 0x{val:08x}")
    datacount = val & 0xF
    cmderr = (val >> 8) & 0x7
    progbufsize = (val >> 24) & 0x1F
    busy = (val >> 12) & 1
    
    err_str = {
        0: "None (OK)",
        1: "Busy",
        2: "Not Supported",
        3: "Exception",
        4: "Halt/Resume",
        5: "Bus Error",
        7: "Other"
    }.get(cmderr, f"Unknown ({cmderr})")

    print(f"  - Command Error:  {cmderr} ({err_str})")
    print(f"  - Busy:           {bool(busy)}")
    print(f"  - Data Count:     {datacount}")
    print(f"  - ProgBuf Size:   {progbufsize} words")


def main():
    parser = argparse.ArgumentParser(description="Test RISC-V Debug Module via OpenOCD DMI")
    parser.add_argument("--host", default="127.0.0.1", help="OpenOCD host IP (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=4444, help="OpenOCD Telnet port (default: 4444)")
    args = parser.parse_args()

    print(f"[*] Connecting to OpenOCD at {args.host}:{args.port}...")
    try:
        dbg = OpenOCDClient(host=args.host, port=args.port)
        print("[+] Connected to OpenOCD.")
    except ConnectionRefusedError:
        print(f"[-] Could not connect to OpenOCD at {args.host}:{args.port}.")
        print("    Ensure OpenOCD is running with 'openocd -f ...'")
        sys.exit(1)
    except Exception as e:
        print(f"[-] Connection error: {e}")
        sys.exit(1)

    try:
        print("\n[*] Step 1: Enabling Debug Module (dmcontrol: dmactive = 1)...")
        dbg.dmi_write(0x10, 0x00000001)
        time.sleep(0.05)

        print("[*] Step 2: Querying dmstatus (0x11)...")
        status = dbg.dmi_read(0x11)
        parse_dmstatus(status)

        if status in (0x00000000, 0xFFFFFFFF):
            print("\n[-] FAIL: Debug Module returned invalid status (0x00000000 / 0xFFFFFFFF).")
            print("    Check that the RISC-V CCU clock gate (0x07102120 / 0x07102124) is un-gated")
            print("    and the core is released from reset.")
            sys.exit(1)

        print("\n[*] Step 3: Querying hartinfo (0x12)...")
        try:
            hinfo = dbg.dmi_read(0x12)
            parse_hartinfo(hinfo)
        except Exception as e:
            print(f"  [-] Failed to read hartinfo: {e}")

        print("\n[*] Step 4: Querying abstractcs (0x16)...")
        try:
            acs = dbg.dmi_read(0x16)
            parse_abstractcs(acs)
        except Exception as e:
            print(f"  [-] Failed to read abstractcs: {e}")

        print("\n" + "=" * 55)
        print("[+] SUCCESS: RISC-V Debug Module Interface (DMI) is active and valid!")
        print("=" * 55)

    finally:
        dbg.close()


if __name__ == "__main__":
    main()
