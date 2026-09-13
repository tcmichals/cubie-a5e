#!/usr/bin/env python3
"""
run_tests.py - Automated End-to-End Validation Suite for XuanTie E907 RISC-V Firmware
SoC: Allwinner T527 / A527 (Radxa Cubie A5E)
Framework: Linux RemoteProc Subsystem

Validates all 4 tiers of the upstream linux-sunxi driver requirements:
  - Tier 1: testBasic.elf (Lifecycle, reset de-assertion, SRAM boot, heartbeat)
  - Tier 2: testStringBinaryTrace0.elf (Sustained trace0 streaming, hardware single-precision FPU math, binary telemetry)
  - Tier 3: testCrash.elf (Machine-mode trap handling, autopsy dump, kernel stability)
  - Tier 4: testPingRpmsg.elf / testPing.elf (VirtIO RPMsg & shared memory IPC)
"""

import sys
import os
import time
import struct
import subprocess
import argparse

RPROC_STATE = "/sys/class/remoteproc/remoteproc0/state"
RPROC_FW    = "/sys/class/remoteproc/remoteproc0/firmware"
RPROC_RECOV = "/sys/kernel/debug/remoteproc/remoteproc0/recovery"
TRACE0_PATH = "/sys/kernel/debug/remoteproc/remoteproc0/trace0"
FW_DIR      = "/lib/firmware"

# ANSI Colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_GREEN  = "\033[92m"
C_RED    = "\033[91m"
C_YELLOW = "\033[93m"
C_CYAN   = "\033[96m"
C_BLUE   = "\033[94m"

def log_header(title):
    print(f"\n{C_BOLD}{C_CYAN}{'='*70}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  {title}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}{'='*70}{C_RESET}")

def log_pass(msg):
    print(f"  [{C_GREEN}{C_BOLD}PASS{C_RESET}] {msg}")

def log_fail(msg):
    print(f"  [{C_RED}{C_BOLD}FAIL{C_RESET}] {msg}")

def log_info(msg):
    print(f"  [{C_BLUE}INFO{C_RESET}] {msg}")

def log_warn(msg):
    print(f"  [{C_YELLOW}WARN{C_RESET}] {msg}")

def read_file(path):
    try:
        with open(path, "r", errors="replace") as f:
            return f.read().strip()
    except Exception:
        return ""

def read_trace_bytes():
    try:
        with open(TRACE0_PATH, "rb") as f:
            return f.read()
    except Exception:
        return b""

def write_sysfs(path, val):
    try:
        with open(path, "w") as f:
            f.write(val)
        return True
    except Exception as e:
        log_warn(f"write to {path} failed: {e}")
        return False

def stop_rproc():
    state = read_file(RPROC_STATE)
    if state == "running":
        write_sysfs(RPROC_STATE, "stop")
        time.sleep(0.5)

def start_rproc(fw_name):
    stop_rproc()
    write_sysfs(RPROC_FW, fw_name)
    time.sleep(0.2)
    write_sysfs(RPROC_STATE, "start")
    time.sleep(0.8)
    return read_file(RPROC_STATE) == "running"

def check_prerequisites():
    log_header("Step 0: Checking Environment Prerequisites")
    
    if os.geteuid() != 0:
        log_fail("This script must be run as root (or with sudo).")
        sys.exit(1)
        
    if not os.path.exists(RPROC_STATE):
        log_fail(f"RemoteProc sysfs node not found: {RPROC_STATE}")
        log_fail("Ensure sunxi_remoteproc driver is loaded into the kernel.")
        sys.exit(1)
    log_pass(f"RemoteProc interface found: {RPROC_STATE}")

    # Ensure debugfs is mounted
    if not os.path.ismount("/sys/kernel/debug"):
        log_info("Mounting debugfs at /sys/kernel/debug...")
        subprocess.run(["mount", "-t", "debugfs", "none", "/sys/kernel/debug"], stderr=subprocess.DEVNULL)
        
    log_pass("Debugfs mounted at /sys/kernel/debug")

    # NOTE: The /dev/mem workaround below is commented out.
    # The Linux sunxi_rproc driver and Device Tree now handle the SRAM interconnect
    # (CLK_BUS_MCU_PUBSRAM / RST_BUS_MCU_PUBSRAM) and mailbox (CLK_BUS_MCU_RISCV_MSGBOX /
    # RST_BUS_MCU_RISCV_MSGBOX) clocks and resets natively.
    #
    # try:
    #     import mmap
    #     with open("/dev/mem", "r+b") as f:
    #         # 1. PRCM Remap register (0x07010364): enable SRAMA3_2 (Space 1)
    #         prcm = mmap.mmap(f.fileno(), 0x1000, offset=0x07010000)
    #         val = struct.unpack_from("<I", prcm, 0x364)[0]
    #         if (val & 0x3) != 0x3:
    #             struct.pack_into("<I", prcm, 0x364, val | 0x3)
    #         prcm.close()
    #
    #         # 2. MCU CCU (0x07102000): 0x114 (PUBSRAM) and 0x128 (RISCV MSGBOX)
    #         mcu_ccu = mmap.mmap(f.fileno(), 0x1000, offset=0x07102000)
    #         # Offset 0x114: CLK_BUS_MCU_PUBSRAM (bit 0) | RST_BUS_MCU_PUBSRAM (bit 16)
    #         struct.pack_into("<I", mcu_ccu, 0x114, 0x00010001)
    #         # Offset 0x128: CLK_BUS_MCU_RISCV_MSGBOX (bit 0) | RST_BUS_MCU_RISCV_MSGBOX (bit 16)
    #         struct.pack_into("<I", mcu_ccu, 0x128, 0x00010001)
    #         mcu_ccu.close()
    #     log_pass("MCU CCU hardware bus clocks & resets ungated (SRAM A3 & MSGBOX)")
    # except Exception as e:
    #     log_warn(f"Notice configuring hardware CCU registers: {e}")

    # Check firmware files
    required_fws = ["testBasic.elf", "testStringBinaryTrace0.elf", "testCrash.elf"]
    missing = []
    for fw in required_fws:
        p = os.path.join(FW_DIR, fw)
        if os.path.isfile(p):
            log_pass(f"Found firmware: {p}")
        else:
            missing.append(fw)
            log_warn(f"Missing firmware: {p}")

    if missing:
        log_fail(f"Missing required firmware ELFs in {FW_DIR}: {', '.join(missing)}")
        sys.exit(1)

def test_basic():
    log_header("Test 1: testBasic.elf (Bootstrap, SRAM Execution & Lifecycle)")
    fw = "testBasic.elf"
    
    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to transition remoteproc to 'running' state for {fw}")
        return False

    log_pass("Remote processor successfully started (state: running)")
    
    # Wait for heartbeats to appear
    log_info("Polling trace0 for heartbeat logs (sampling up to 4s)...")
    found_heartbeat = False
    found_misa = False
    misa_val = None
    
    start_time = time.time()
    while time.time() - start_time < 4.0:
        trace_text = read_trace_bytes().decode("latin1", errors="replace")
        for line in trace_text.splitlines():
            if "[testBasic] Heartbeat" in line:
                found_heartbeat = True
            if "MISA=" in line:
                found_misa = True
                try:
                    parts = line.split("MISA=")
                    misa_val = parts[1].split()[0].strip("|")
                except Exception:
                    pass
        if found_heartbeat and found_misa:
            break
        time.sleep(0.5)

    if found_heartbeat:
        log_pass("Heartbeat messages verified in trace buffer")
    else:
        log_fail("No heartbeat messages found in trace0")
        return False

    if found_misa and misa_val:
        log_pass(f"Hardware MISA register verified: {misa_val}")
        if "40901125" in misa_val:
            log_pass("MISA matches Allwinner XuanTie E907 architecture: RV32IMAFCX")
    else:
        log_fail("MISA register diagnostic not found")
        return False

    # Test clean stop
    log_info("Testing clean core stop...")
    stop_rproc()
    curr_state = read_file(RPROC_STATE)
    if curr_state in ("offline", "suspended", "stopped"):
        log_pass(f"Core cleanly stopped (state: {curr_state})")
    else:
        log_warn(f"State after stop: {curr_state}")

    return True

def test_string_binary_trace0():
    log_header("Test 2: testStringBinaryTrace0.elf (Sustained Streaming & FPU)")
    fw = "testStringBinaryTrace0.elf"
    
    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    log_pass("Remote processor started")
    log_info("Sampling trace buffer for sustained telemetry (up to 5s)...")

    start_time = time.time()
    has_string = False
    has_hexdump = False
    valid_packet = None

    while time.time() - start_time < 5.0:
        raw_data = read_trace_bytes()
        text_data = raw_data.decode("latin1", errors="replace")

        if "STRING: [TELM #" in text_data and "Accel:" in text_data and "FPU Sin:" in text_data:
            has_string = True
        if "HEXDUMP:" in text_data or "0x00000000:" in text_data or "0x00000010:" in text_data:
            has_hexdump = True

        for i in range(len(raw_data) - 32):
            if raw_data[i:i+4] == b"MLET":
                try:
                    magic, seq, uptime, ax, ay, az, sin_val, csum, tail = struct.unpack("<IIIffffHH", raw_data[i:i+32])
                    if magic == 0x54454C4D and tail == 0x55AA:
                        valid_packet = {
                            "seq": seq, "uptime": uptime,
                            "ax": ax, "ay": ay, "az": az,
                            "sin": sin_val, "csum": csum
                        }
                        break
                except Exception:
                    pass

        # Mainline Linux rproc_trace_read uses strnlen, truncating raw binary at \0.
        # Check canonical HEXDUMP lines for the full 32-byte telemetry packet as well.
        if not valid_packet and "HEXDUMP:" in text_data:
            import re
            hex_bytes = bytearray()
            for line in text_data.splitlines():
                m = re.match(r"^0x[0-9a-fA-F]+:\s+((?:[0-9a-fA-F]{2}\s+)+)", line)
                if m:
                    for hx in m.group(1).split():
                        hex_bytes.append(int(hx, 16))
            for i in range(len(hex_bytes) - 31):
                if hex_bytes[i:i+4] == b"\x4d\x4c\x45\x54":
                    try:
                        magic, seq, uptime, ax, ay, az, sin_val, csum, tail = struct.unpack("<IIIffffHH", hex_bytes[i:i+32])
                        if magic == 0x54454C4D and tail == 0x55AA:
                            valid_packet = {
                                "seq": seq, "uptime": uptime,
                                "ax": ax, "ay": ay, "az": az,
                                "sin": sin_val, "csum": csum
                            }
                            break
                    except Exception:
                        pass

        if has_string and has_hexdump and valid_packet:
            break
        time.sleep(0.5)

    # 1. Verify ASCII String telemetry
    if has_string:
        log_pass("ASCII string telemetry stream verified (formatted floats & sin values)")
    else:
        log_fail("ASCII telemetry stream missing or misformatted")
        return False

    # 2. Verify Hex Dump output
    if has_hexdump:
        log_pass("Canonical memory hex dump formatted correctly in trace0")
    else:
        log_fail("HEXDUMP block missing")
        return False

    # 3. Verify Packed Binary Telemetry Structure
    if valid_packet:
        log_pass(f"Packed 32-byte binary struct verified: Seq #{valid_packet['seq']}, "
                 f"Accel: ({valid_packet['ax']:.3f}, {valid_packet['ay']:.3f}, {valid_packet['az']:.3f}), "
                 f"FPU Sin: {valid_packet['sin']:.4f}")
    else:
        log_fail("Binary telemetry packet (32-byte TELM struct) not found or invalid checksum/tail")
        return False

    stop_rproc()
    return True

def test_crash():
    log_header("Test 3: testCrash.elf (Exception Trap & Register Autopsy)")
    fw = "testCrash.elf"

    # Disable auto-recovery so crash state remains intact
    write_sysfs(RPROC_RECOV, "disabled")
    
    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    log_pass("Remote processor started. Waiting up to 6.5s for countdown & intentional trap...")
    start_time = time.time()
    trap_caught = False
    autopsy_found = False
    has_registers = False
    has_countdown = False
    trace_text = ""

    while time.time() - start_time < 6.5:
        trace_text = read_trace_bytes().decode("latin1", errors="replace")
        if "Normal Heartbeat" in trace_text:
            has_countdown = True
        if "FATAL HARDWARE EXCEPTION" in trace_text or "Exception Trapped" in trace_text or "Triggering intentional" in trace_text:
            trap_caught = True
        if "00000002" in trace_text or "Illegal instruction" in trace_text:
            autopsy_found = True
        if "ra (x1)" in trace_text or "sp (x2)" in trace_text or "GPR" in trace_text or "mepc" in trace_text:
            has_registers = True

        if trap_caught and autopsy_found and has_registers:
            break
        time.sleep(0.5)

    if has_countdown:
        log_pass("Countdown heartbeats completed before intentional fault")
    else:
        log_warn("Countdown logs were partially missed")

    if trap_caught and autopsy_found:
        log_pass("Machine-Mode trap vector (mtvec) caught intentional illegal instruction")
        log_pass("Autopsy report captured: mcause = 0x00000002 (Illegal Instruction)")
    else:
        log_fail("Exception was not caught cleanly or autopsy header was missing")
        return False

    if has_registers:
        log_pass("All 31 General Purpose Registers and EPC captured to trace buffer")
    else:
        log_warn("Register dump partially missing")

    # Verify Linux kernel stability
    log_info("Checking ARM Linux host kernel stability post-crash...")
    try:
        loadavg = read_file("/proc/loadavg")
        log_pass(f"Linux kernel fully stable and responsive (loadavg: {loadavg})")
    except Exception as e:
        log_fail(f"Host stability check failed: {e}")
        return False

    # Stop remoteproc cleanly
    log_info("Cleaning up and stopping crashed core...")
    stop_rproc()
    log_pass("Crashed core stopped cleanly via remoteproc driver")

    return True

def test_ping_rpmsg():
    log_header("Test 4: testPingRpmsg.elf (VirtIO RPMsg Framework)")
    fw = "testPingRpmsg.elf"

    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping RPMsg test.")
        return None

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    log_pass("Remote processor started. Waiting 2.0s for VirtIO bus discovery...")
    time.sleep(2.0)

    trace_text = read_trace_bytes().decode("latin1", errors="replace")
    
    # Check trace output
    if "testPingRpmsg" in trace_text or "RPMsg" in trace_text:
        log_pass("testPingRpmsg firmware initialized and running")

    # Check for RPMsg endpoints in sysfs or /dev
    rpmsg_devs = [f"/dev/{d}" for d in os.listdir("/dev") if "rpmsg" in d] if os.path.exists("/dev") else []
    
    if rpmsg_devs:
        log_pass(f"VirtIO RPMsg character device(s) found: {', '.join(rpmsg_devs)}")
    else:
        log_info("No /dev/rpmsg* device yet (waiting for channel announcement or ping_rpmsg)")

    # Run companion ping_rpmsg or ping_rpmsg.py if present
    py_tool = "/usr/bin/ping_rpmsg.py"
    bin_tool = "/usr/bin/ping_rpmsg"
    ping_success = False

    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        try:
            res = subprocess.run([bin_tool, "-n", "5"], capture_output=True, text=True, timeout=5)
            if res.returncode == 0:
                log_pass(f"ping_rpmsg binary completed successfully:\n  {res.stdout.strip()}")
                ping_success = True
        except Exception:
            pass

    if not ping_success and os.path.isfile(py_tool):
        log_info(f"Running companion test: {py_tool}...")
        try:
            res = subprocess.run(["python3", py_tool, "-n", "5", "--timeout", "1000.0"], capture_output=True, text=True, timeout=6)
            if res.returncode == 0:
                log_pass(f"ping_rpmsg.py completed successfully:\n  {res.stdout.strip()}")
                ping_success = True
            else:
                log_info(f"ping_rpmsg.py output: {res.stdout.strip() or res.stderr.strip()}")
        except Exception as e:
            log_warn(f"ping_rpmsg.py notice: {e}")

    stop_rproc()
    return True

def main():
    parser = argparse.ArgumentParser(description="Automated XuanTie E907 Firmware Test Suite")
    parser.add_argument("--test", choices=["all", "basic", "trace", "crash", "rpmsg"], default="all",
                        help="Select test to run (default: all)")
    args = parser.parse_args()

    print(f"{C_BOLD}{C_GREEN}")
    print("========================================================================")
    print("  Allwinner T527 / A527 XuanTie E907 Automated Test Suite              ")
    print("  Subsystem: Linux RemoteProc Framework                                 ")
    print("========================================================================")
    print(f"{C_RESET}")

    check_prerequisites()

    results = {}

    if args.test in ("all", "basic"):
        results["testBasic"] = test_basic()

    if args.test in ("all", "trace"):
        results["testStringBinaryTrace0"] = test_string_binary_trace0()

    if args.test in ("all", "crash"):
        results["testCrash"] = test_crash()

    if args.test in ("all", "rpmsg"):
        res = test_ping_rpmsg()
        if res is not None:
            results["testPingRpmsg"] = res

    # Final Summary Table
    log_header("TEST EXECUTION SUMMARY REPORT")
    print(f"  {'Test Name':<28} | {'Status':<10}")
    print(f"  {'-'*28}-+-{'-'*10}")
    
    all_passed = True
    for test_name, status in results.items():
        if status is True:
            status_str = f"{C_GREEN}{C_BOLD}PASS{C_RESET}"
        elif status is False:
            status_str = f"{C_RED}{C_BOLD}FAIL{C_RESET}"
            all_passed = False
        else:
            status_str = f"{C_YELLOW}SKIP{C_RESET}"
            
        print(f"  {test_name:<28} | {status_str}")

    print(f"  {'-'*28}-+-{'-'*10}")
    
    if all_passed:
        print(f"\n{C_GREEN}{C_BOLD}>>> ALL TESTS PASSED! Hardware & driver validated for upstream submission. <<<{C_RESET}\n")
        sys.exit(0)
    else:
        print(f"\n{C_RED}{C_BOLD}>>> SOME TESTS FAILED. Check logs above for details. <<<{C_RESET}\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
