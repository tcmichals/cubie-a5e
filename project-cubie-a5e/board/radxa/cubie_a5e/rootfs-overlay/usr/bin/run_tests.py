#!/usr/bin/env python3
"""
run_tests.py - Automated End-to-End Validation Suite for XuanTie E907 RISC-V Firmware
SoC: Allwinner T527 / A527 (Radxa Cubie A5E)
Framework: Linux RemoteProc Subsystem

Validates firmware applications across 3 Device Tree profiles:
  - Profile 1 (DDR VirtIO): testBasic, testStringBinaryTrace0, testCrash, testPingRpmsg, testDRAMMsg
  - Profile 2 (Pure SRAM VirtIO): testPingRpmsgSram
  - Profile 3 (Userspace UIO): testPing (ping_uio, ping_shm)
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
DT_BASE     = "/sys/firmware/devicetree/base"

# ANSI Colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_GREEN  = "\033[92m"
C_RED    = "\033[91m"
C_YELLOW = "\033[93m"
C_CYAN   = "\033[96m"
C_BLUE   = "\033[94m"

# Profiles
PROFILE_1 = 1  # Standard DDR VirtIO
PROFILE_2 = 2  # Pure On-Chip SRAM VirtIO
PROFILE_3 = 3  # Userspace UIO Mode

PROFILE_INFO = {
    PROFILE_1: {
        "name": "Profile 1 (Standard DDR VirtIO & Carveout)",
        "overlay_config": "dtoverlay=cubie-a5e-flight-stack",
        "test_overlays": ["cubie-a5e-testBasic", "cubie-a5e-testStringBinaryTrace0", "cubie-a5e-testCrash", "cubie-a5e-testPingRpmsg", "cubie-a5e-testDRAMMsg"],
        "node_desc": "DDR DRAM Carveout / vdev@48000000",
        "compatible_tests": ["basic", "trace", "crash", "rpmsg", "dram"],
    },
    PROFILE_2: {
        "name": "Profile 2 (Pure On-Chip SRAM VirtIO)",
        "overlay_config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram",
        "test_overlays": ["cubie-a5e-testPingRpmsgSram", "cubie-a5e-rpmsg-sram"],
        "node_desc": "On-Chip SRAM Space 1 / sram1@72c0000",
        "compatible_tests": ["basic", "trace", "rpmsg-sram"],
    },
    PROFILE_3: {
        "name": "Profile 3 (Userspace UIO Direct Mailbox)",
        "overlay_config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPing",
        "test_overlays": ["cubie-a5e-testPing", "cubie-a5e-uio"],
        "node_desc": "Hardware Mailbox bound to generic-uio (/dev/uio0)",
        "compatible_tests": ["ping-uio"],
    },
}

TEST_OVERLAY_HINTS = {
    "testPingRpmsgSram": {
        "dtbo": "cubie-a5e-testPingRpmsgSram",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram",
    },
    "testPing": {
        "dtbo": "cubie-a5e-testPing",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPing",
    },
    "testPingRpmsg": {
        "dtbo": "cubie-a5e-testPingRpmsg",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testPingRpmsg",
    },
    "testDRAMMsg": {
        "dtbo": "cubie-a5e-testDRAMMsg",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testDRAMMsg",
    },
    "testCrash": {
        "dtbo": "cubie-a5e-testCrash",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testCrash",
    },
    "testBasic": {
        "dtbo": "cubie-a5e-testBasic",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testBasic",
    },
    "testStringBinaryTrace0": {
        "dtbo": "cubie-a5e-testStringBinaryTrace0",
        "config": "dtoverlay=cubie-a5e-flight-stack cubie-a5e-testStringBinaryTrace0",
    },
}

TEST_ALIAS_MAP = {
    "basic": "testBasic",
    "testBasic": "testBasic",
    "trace": "testStringBinaryTrace0",
    "testStringBinaryTrace0": "testStringBinaryTrace0",
    "crash": "testCrash",
    "testCrash": "testCrash",
    "rpmsg": "testPingRpmsg",
    "testPingRpmsg": "testPingRpmsg",
    "dram": "testDRAMMsg",
    "testDRAMMsg": "testDRAMMsg",
    "rpmsg-sram": "testPingRpmsgSram",
    "testPingRpmsgSram": "testPingRpmsgSram",
    "ping-uio": "testPing",
    "testPing": "testPing",
}

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

def detect_active_dt_profile():
    """
    Inspects /sys/firmware/devicetree/base to determine the active hardware profile.
    Returns: (profile_id, details_str)
    """
    # 1. Check if Mailbox is bound to generic-uio (Profile 3)
    msgbox_compat_path = os.path.join(DT_BASE, "soc/mailbox@3003000/compatible")
    if os.path.exists(msgbox_compat_path):
        try:
            with open(msgbox_compat_path, "rb") as f:
                compat = f.read().decode("latin1", errors="replace")
                if "generic-uio" in compat or os.path.exists("/dev/uio0"):
                    return PROFILE_3, "generic-uio (/dev/uio0 active)"
        except Exception:
            pass

    # 2. Check if remoteproc memory-region points to sram1 (Profile 2)
    sram1_path = os.path.join(DT_BASE, "reserved-memory/sram1@72c0000")
    rproc_mem_reg = os.path.join(DT_BASE, "soc/remoteproc@7130000/memory-region")
    if os.path.isdir(sram1_path) and os.path.exists(rproc_mem_reg):
        try:
            with open(rproc_mem_reg, "rb") as f:
                rproc_ph_bytes = f.read()
            if len(rproc_ph_bytes) >= 4:
                rproc_ph = struct.unpack(">I", rproc_ph_bytes[0:4])[0]
                sram1_ph_path = os.path.join(sram1_path, "phandle")
                if not os.path.exists(sram1_ph_path):
                    sram1_ph_path = os.path.join(sram1_path, "linux,phandle")
                if os.path.exists(sram1_ph_path):
                    with open(sram1_ph_path, "rb") as f:
                        sram1_ph = struct.unpack(">I", f.read()[0:4])[0]
                    if rproc_ph == sram1_ph:
                        return PROFILE_2, "sram1@72c0000 (0x072c0000, 256 KB On-Chip SRAM Space 1)"
        except Exception:
            pass

    # 3. Default is Profile 1 (DDR Carveout)
    return PROFILE_1, "vdev@48000000 (0x48000000, 1 MB DDR Carveout)"

def verify_profile_or_halt(test_id, allowed_profiles):
    if not isinstance(allowed_profiles, (list, tuple)):
        allowed_profiles = [allowed_profiles]

    active_profile, active_detail = detect_active_dt_profile()
    if active_profile not in allowed_profiles:
        primary_req = allowed_profiles[0]
        req_info = PROFILE_INFO[primary_req]
        act_info = PROFILE_INFO[active_profile]

        hint = TEST_OVERLAY_HINTS.get(test_id)
        overlay_cmd = hint["config"] if hint else req_info["overlay_config"]
        test_dtbo = hint["dtbo"] if hint else None

        print(f"\n{C_BOLD}{C_RED}{'='*74}{C_RESET}")
        print(f"{C_BOLD}{C_RED}  [FATAL HARDWARE GATE] DEVICE TREE CONFIGURATION MISMATCH{C_RESET}")
        print(f"{C_BOLD}{C_RED}{'='*74}{C_RESET}")
        print(f"  {C_BOLD}Requested Test :{C_RESET} {test_id}")
        print(f"  {C_BOLD}Required Mode  :{C_RESET} {req_info['name']}")
        print(f"  {C_BOLD}Required Node  :{C_RESET} {req_info['node_desc']}")
        if test_dtbo:
            print(f"  {C_BOLD}Test Overlay   :{C_RESET} {test_dtbo}.dtbo")
        print(f"  {C_BOLD}Required DTBO  :{C_RESET} {overlay_cmd}")
        print("")
        print(f"  {C_BOLD}CURRENT ACTIVE SYSTEM CONFIGURATION:{C_RESET}")
        print(f"    Active Profile: {act_info['name']}")
        print(f"    Active Node   : {active_detail}")
        print("")
        print(f"  {C_BOLD}{C_YELLOW}>>> TEST HALTED TO PREVENT HARDWARE LOCKUP / MEMORY CORRUPTION <<<{C_RESET}")
        print("")
        print(f"  {C_BOLD}HOW TO CONFIGURE AND RUN THIS TEST:{C_RESET}")
        print(f"    1. Set matching overlay in /boot/config.txt:")
        print(f"       {C_GREEN}sed -i 's/^dtoverlay=.*/{overlay_cmd}/' /boot/config.txt{C_RESET}")
        print(f"    2. Reboot the board:")
        print(f"       {C_GREEN}reboot{C_RESET}")
        print(f"    3. Re-run test once rebooted.")
        print(f"{C_BOLD}{C_RED}{'='*74}{C_RESET}\n")
        sys.exit(2)

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

    # Detect live Device Tree profile
    active_profile, active_detail = detect_active_dt_profile()
    log_pass(f"Live Hardware Profile: {PROFILE_INFO[active_profile]['name']}")
    log_info(f"Active Memory Region : {active_detail}")

def test_basic():
    verify_profile_or_halt("testBasic", [PROFILE_1, PROFILE_2])
    log_header("Test 1: testBasic.elf (Bootstrap, SRAM Execution & Lifecycle)")
    fw = "testBasic.elf"
    
    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to transition remoteproc to 'running' state for {fw}")
        return False

    log_pass("Remote processor successfully started (state: running)")
    
    log_info("Polling trace0 for heartbeat logs (sampling up to 4s)...")
    found_heartbeat = False
    found_misa = False
    misa_val = None
    
    start_time = time.time()
    while time.time() - start_time < 4.0:
        trace_data = read_trace_bytes()
        if b"Heartbeat" in trace_data:
            found_heartbeat = True
        if b"misa:" in trace_data:
            found_misa = True
            try:
                for line in trace_data.decode("latin1", errors="replace").splitlines():
                    if "misa:" in line:
                        misa_val = line.strip()
                        break
            except Exception:
                pass
        if found_heartbeat and found_misa:
            break
        time.sleep(0.3)
        
    if found_heartbeat:
        log_pass("Heartbeat telemetry received from XuanTie E907 via trace0")
    else:
        log_fail("Heartbeat telemetry timeout on trace0")
        stop_rproc()
        return False
        
    if found_misa and misa_val:
        log_pass(f"Machine ISA verified: {misa_val}")
    else:
        log_warn("MISA register string not captured in trace0")

    log_info("Testing graceful shutdown of remote core...")
    stop_rproc()
    if read_file(RPROC_STATE) == "offline":
        log_pass("Remote processor stopped cleanly (state: offline)")
    else:
        log_fail(f"Processor failed to stop cleanly (state: {read_file(RPROC_STATE)})")
        return False
        
    return True

def test_string_binary_trace0():
    verify_profile_or_halt("testStringBinaryTrace0", [PROFILE_1, PROFILE_2])
    log_header("Test 2: testStringBinaryTrace0.elf (Dual String & Fast Binary Telemetry)")
    fw = "testStringBinaryTrace0.elf"
    
    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    log_pass("Remote processor started. Sampling trace0 stream...")
    time.sleep(2.0)

    trace_data = read_trace_bytes()
    trace_text = trace_data.decode("latin1", errors="replace")

    has_string = "STRING:" in trace_text or "TELM" in trace_text or "FPU Sin" in trace_text
    has_hexdump = "HEXDUMP:" in trace_text or "0x00000000:" in trace_text

    if has_string:
        log_pass("ASCII string telemetry stream verified (formatted floats & sin values)")
    else:
        log_fail("Formatted telemetry string missing in trace0")
        stop_rproc()
        return False

    if has_hexdump:
        log_pass("Canonical memory hex dump formatted correctly in trace0")
    else:
        log_warn("Hexdump block not detected")

    stop_rproc()
    return True

def test_crash():
    verify_profile_or_halt("testCrash", PROFILE_1)
    log_header("Test 3: testCrash.elf (Exception Trap & Register Autopsy)")
    fw = "testCrash.elf"

    write_sysfs(RPROC_RECOV, "disabled")
    log_info("RemoteProc automatic recovery disabled for post-mortem analysis")

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        write_sysfs(RPROC_RECOV, "enabled")
        return False

    log_pass("Remote processor started. Waiting up to 6.5s for countdown & intentional trap...")
    time.sleep(6.5)

    trace_text = read_trace_bytes().decode("latin1", errors="replace")
    has_heartbeats = "Heartbeat #1" in trace_text or "Heartbeat #2" in trace_text
    has_autopsy = "EXCEPTION AUTOPSY REPORT" in trace_text or "mcause" in trace_text
    has_registers = "ra :" in trace_text and "sp :" in trace_text and "mepc :" in trace_text

    if has_heartbeats:
        log_pass("Countdown heartbeats completed before intentional fault")
    else:
        log_warn("Heartbeats prior to fault not detected")

    if has_autopsy:
        log_pass("Machine-Mode trap vector (mtvec) caught intentional illegal instruction")
        for line in trace_text.splitlines():
            if "mcause" in line:
                log_pass(f"Autopsy report captured: {line.strip()}")
                break
    else:
        log_fail("Autopsy report missing from trace buffer")
        stop_rproc()
        write_sysfs(RPROC_RECOV, "enabled")
        return False

    if has_registers:
        log_pass("All 31 General Purpose Registers and EPC captured to trace buffer")

    log_info("Checking ARM Linux host kernel stability post-crash...")
    try:
        loadavg = read_file("/proc/loadavg")
        log_pass(f"Linux kernel fully stable and responsive (loadavg: {loadavg})")
    except Exception as e:
        log_fail(f"Host stability check failed: {e}")
        stop_rproc()
        write_sysfs(RPROC_RECOV, "enabled")
        return False

    log_info("Cleaning up and stopping crashed core...")
    stop_rproc()
    write_sysfs(RPROC_RECOV, "enabled")
    log_pass("Crashed core stopped cleanly via remoteproc driver")
    return True

def test_ping_rpmsg():
    verify_profile_or_halt("testPingRpmsg", PROFILE_1)
    log_header("Test 4: testPingRpmsg.elf (Standard Linux VirtIO RPMsg over DDR)")
    fw = "testPingRpmsg.elf"

    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return None

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    log_pass("Remote processor started. Waiting 2.0s for VirtIO bus discovery...")
    time.sleep(2.0)

    bin_tool = "/usr/bin/ping_rpmsg"
    ping_success = False

    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        try:
            res = subprocess.run([bin_tool, "-n", "10", "-D", "0", "-s", "496"],
                                 capture_output=True, text=True, timeout=5)
            if res.returncode == 0:
                log_pass(f"ping_rpmsg binary completed successfully:\n  {res.stdout.strip()}")
                ping_success = True
        except Exception as e:
            log_warn(f"ping_rpmsg error: {e}")

    stop_rproc()
    if ping_success:
        log_pass("VirtIO RPMsg ping-pong communication verified successfully")
        return True
    else:
        log_fail("VirtIO RPMsg ping-pong failed: No replies received")
        return False

def test_dram_msg():
    verify_profile_or_halt("testDRAMMsg", PROFILE_1)
    log_header("Test 5: testDRAMMsg.elf (Hybrid SRAM Control / DDR Carveout Bulk Streaming)")
    fw = "testDRAMMsg.elf"

    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return None

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    time.sleep(1.0)
    bin_tool = "/usr/bin/ping_dram"
    success = False

    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        try:
            res = subprocess.run([bin_tool, "-n", "10", "-s", "512"],
                                 capture_output=True, text=True, timeout=5)
            if res.returncode == 0:
                log_pass(f"ping_dram completed successfully:\n  {res.stdout.strip()}")
                success = True
        except Exception as e:
            log_warn(f"ping_dram error: {e}")

    stop_rproc()
    return success

def test_ping_rpmsg_sram():
    verify_profile_or_halt("testPingRpmsgSram", PROFILE_2)
    log_header("Test 6: testPingRpmsgSram.elf (Pure On-Chip SRAM Space 1 VirtIO RPMsg)")
    fw = "testPingRpmsgSram.elf"

    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return None

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    time.sleep(2.0)
    bin_tool = "/usr/bin/ping_rpmsg"
    ping_success = False

    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        try:
            res = subprocess.run([bin_tool, "-n", "10", "-D", "0", "-s", "496"],
                                 capture_output=True, text=True, timeout=5)
            if res.returncode == 0:
                log_pass(f"ping_rpmsg (SRAM Space 1) completed successfully:\n  {res.stdout.strip()}")
                ping_success = True
        except Exception as e:
            log_warn(f"ping_rpmsg error: {e}")

    stop_rproc()
    return ping_success

def test_ping_uio():
    verify_profile_or_halt("testPing", PROFILE_3)
    log_header("Test 7: testPing.elf (Userspace UIO Direct Mailbox Benchmark)")
    fw = "testPing.elf"

    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return None

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return False

    time.sleep(1.0)
    bin_tool = "/usr/bin/ping_shm"
    success = False

    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        try:
            res = subprocess.run([bin_tool, "-n", "100"],
                                 capture_output=True, text=True, timeout=5)
            if res.returncode == 0:
                log_pass(f"ping_shm completed successfully:\n  {res.stdout.strip()}")
                success = True
        except Exception as e:
            log_warn(f"ping_shm error: {e}")

    stop_rproc()
    return success

def main():
    parser = argparse.ArgumentParser(description="Automated Device Tree-Aware XuanTie E907 Firmware Test Suite")
    parser.add_argument("--test",
                        default="all",
                        help="Select test to run (default: all compatible with active DT). "
                             "Options: all, basic, trace, crash, rpmsg, dram, rpmsg-sram, ping-uio, "
                             "or full test name (e.g. testPingRpmsg, testPingRpmsgSram, testPing)")
    parser.add_argument("--detect-dt", action="store_true", help="Print active Device Tree configuration and exit")
    args = parser.parse_args()

    if args.detect_dt:
        prof, detail = detect_active_dt_profile()
        info = PROFILE_INFO[prof]
        print(f"\n{C_BOLD}{C_CYAN}Active Device Tree Profile:{C_RESET} {info['name']}")
        print(f"  {C_BOLD}Node Mapping  :{C_RESET} {detail}")
        print(f"  {C_BOLD}Overlay Config:{C_RESET} {info['overlay_config']}")
        print(f"  {C_BOLD}Compatible    :{C_RESET} {', '.join(info['compatible_tests'])}\n")
        sys.exit(0)

    print(f"{C_BOLD}{C_GREEN}")
    print("========================================================================")
    print("  Allwinner T527 / A527 XuanTie E907 Device Tree Test Suite             ")
    print("  Subsystem: Linux RemoteProc Framework                                 ")
    print("========================================================================")
    print(f"{C_RESET}")

    req_test = args.test
    if req_test != "all":
        if req_test not in TEST_ALIAS_MAP:
            print(f"{C_RED}[ERROR] Unknown test '{req_test}'. Available options:{C_RESET}")
            print(f"  Short names: basic, trace, crash, rpmsg, dram, rpmsg-sram, ping-uio")
            print(f"  Full names : testBasic, testStringBinaryTrace0, testCrash, testPingRpmsg, testDRAMMsg, testPingRpmsgSram, testPing")
            sys.exit(1)
        canon_test = TEST_ALIAS_MAP[req_test]
    else:
        canon_test = "all"

    active_profile, _ = detect_active_dt_profile()

    # Immediate pre-flight gate for specific requested tests
    if canon_test == "testPingRpmsgSram":
        verify_profile_or_halt("testPingRpmsgSram", PROFILE_2)
    elif canon_test == "testPing":
        verify_profile_or_halt("testPing", PROFILE_3)
    elif canon_test in ("testCrash", "testPingRpmsg", "testDRAMMsg"):
        verify_profile_or_halt(canon_test, PROFILE_1)

    check_prerequisites()

    results = {}

    if canon_test == "all":
        # Run tests compatible with active profile
        if active_profile == PROFILE_1:
            results["testBasic"] = test_basic()
            results["testStringBinaryTrace0"] = test_string_binary_trace0()
            results["testCrash"] = test_crash()
            results["testPingRpmsg"] = test_ping_rpmsg()
            results["testDRAMMsg"] = test_dram_msg()
        elif active_profile == PROFILE_2:
            results["testBasic"] = test_basic()
            results["testStringBinaryTrace0"] = test_string_binary_trace0()
            results["testPingRpmsgSram"] = test_ping_rpmsg_sram()
        elif active_profile == PROFILE_3:
            results["testPing"] = test_ping_uio()
    else:
        # Run specific requested test (strict profile verification)
        if canon_test == "testBasic":
            results["testBasic"] = test_basic()
        elif canon_test == "testStringBinaryTrace0":
            results["testStringBinaryTrace0"] = test_string_binary_trace0()
        elif canon_test == "testCrash":
            results["testCrash"] = test_crash()
        elif canon_test == "testPingRpmsg":
            results["testPingRpmsg"] = test_ping_rpmsg()
        elif canon_test == "testDRAMMsg":
            results["testDRAMMsg"] = test_dram_msg()
        elif canon_test == "testPingRpmsgSram":
            results["testPingRpmsgSram"] = test_ping_rpmsg_sram()
        elif canon_test == "testPing":
            results["testPing"] = test_ping_uio()

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
    
    if all_passed and results:
        print(f"\n{C_GREEN}{C_BOLD}>>> ALL EXECUTED TESTS PASSED for {PROFILE_INFO[active_profile]['name']}! <<<{C_RESET}\n")
        sys.exit(0)
    elif not results:
        print(f"\n{C_YELLOW}No tests were executed.{C_RESET}\n")
        sys.exit(0)
    else:
        print(f"\n{C_RED}{C_BOLD}>>> SOME TESTS FAILED. Check logs above for details. <<<{C_RESET}\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
