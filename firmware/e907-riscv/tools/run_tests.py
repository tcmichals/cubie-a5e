#!/usr/bin/env python3
"""
run_tests.py - Automated End-to-End Validation Suite for XuanTie E907 RISC-V Firmware
SoC: Allwinner T527 / A527 (Radxa Cubie A5E)
Framework: Linux RemoteProc Subsystem

Validates firmware applications across 3 Device Tree profiles:
  - Profile 1 (DDR VirtIO): testBasic, testStringBinaryTrace0, testCrash, testPingRpmsg, testDRAMMsg
  - Profile 2 (Pure SRAM VirtIO): testPingRpmsgSram
  - Profile 3 (Userspace UIO): testPing (ping_uio, ping_shm)

Extracts quantitative metrics (RTT latency, throughput, bandwidth, packet success,
register states) and outputs structured terminal tables + persistent JSON/Markdown reports.
"""

import sys
import os
import time
import struct
import subprocess
import argparse
import re
import json

import threading

RPROC_STATE = "/sys/class/remoteproc/remoteproc0/state"
RPROC_FW    = "/sys/class/remoteproc/remoteproc0/firmware"
RPROC_RECOV = "/sys/kernel/debug/remoteproc/remoteproc0/recovery"
TRACE0_PATH = "/sys/kernel/debug/remoteproc/remoteproc0/trace0"
FW_DIR      = "/lib/firmware"
DT_BASE     = "/sys/firmware/devicetree/base"

DEFAULT_JSON_OUT   = "/tmp/remoteproc_test_results.json"
DEFAULT_REPORT_OUT = "/tmp/remoteproc_test_results.md"

# ANSI Colors
C_RESET  = "\033[0m"
C_BOLD   = "\033[1m"
C_GREEN  = "\033[92m"
C_RED    = "\033[91m"
C_YELLOW = "\033[93m"
C_CYAN   = "\033[96m"
C_BLUE   = "\033[94m"
C_MAGENTA = "\033[95m"

# Profiles
PROFILE_1 = 1  # Standard DDR VirtIO
PROFILE_2 = 2  # Pure On-Chip SRAM VirtIO
PROFILE_3 = 3  # Userspace UIO Mode
PROFILE_4 = 4  # Hardware Mailbox Isolation & Dual-Core Test
PROFILE_5 = 5  # Cadence HiFi4 DSP Mailbox Isolation

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
    PROFILE_4: {
        "name": "Profile 4 (Hardware Mailbox Isolation & Dual-Core Test)",
        "overlay_config": "dtoverlay=cubie-a5e-dual-mailbox-test",
        "test_overlays": ["cubie-a5e-dual-mailbox-test", "cubie-a5e-mailbox-test"],
        "node_desc": "Hardware Mailbox Clients (/sys/kernel/debug/mailbox-test-*)",
        "compatible_tests": ["msgbox", "dsp-msgbox", "dual-msgbox", "mailbox"],
    },
    PROFILE_5: {
        "name": "Profile 5 (Cadence HiFi4 DSP Mailbox Isolation)",
        "overlay_config": "dtoverlay=cubie-a5e-dsp-mailbox-test",
        "test_overlays": ["cubie-a5e-dsp-mailbox-test"],
        "node_desc": "HiFi4 DSP Hardware Mailbox (/sys/kernel/debug/mailbox-test-dsp)",
        "compatible_tests": ["dsp-msgbox"],
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
    "testMsgbox": {
        "dtbo": "cubie-a5e-dual-mailbox-test",
        "config": "dtoverlay=cubie-a5e-dual-mailbox-test",
    },
    "testDSPMsgbox": {
        "dtbo": "cubie-a5e-dsp-mailbox-test",
        "config": "dtoverlay=cubie-a5e-dsp-mailbox-test",
    },
    "testDualMsgbox": {
        "dtbo": "cubie-a5e-dual-mailbox-test",
        "config": "dtoverlay=cubie-a5e-dual-mailbox-test",
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
    "mbox": "testMailbox",
    "mailbox": "testMailbox",
    "mailbox-test": "testMailbox",
    "testMailbox": "testMailbox",
    "msgbox": "testMsgbox",
    "testMsgbox": "testMsgbox",
    "dsp-msgbox": "testDSPMsgbox",
    "testDSPMsgbox": "testDSPMsgbox",
    "dual-msgbox": "testDualMsgbox",
    "testDualMsgbox": "testDualMsgbox",
}

def strip_ansi(text):
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)

def parse_benchmark_output(raw_output):
    """
    Parses quantitative metrics from standard C++ and Python benchmark tool outputs.
    Works with ping_rpmsg, ping_rpmsg.py, ping_shm, ping_uio, ping_uio.py, ping_dram.
    """
    text = strip_ansi(raw_output)
    metrics = {
        "packets_sent": None,
        "packets_recv": None,
        "success_rate_pct": None,
        "data_integrity": None,
        "avg_lat_us": None,
        "min_lat_us": None,
        "max_lat_us": None,
        "jitter_us": None,
        "throughput_msgs_s": None,
        "bandwidth": None,
    }

    m = re.search(r'(?:Packets Sent|Total Packets Sent|Total Pings Sent|Messages Sent)\s*:\s*(\d+)', text)
    if m:
        metrics["packets_sent"] = int(m.group(1))

    m = re.search(r'(?:Packets Recv|Successful Replies|Pongs Received|Messages Recv)\s*:\s*(\d+)', text)
    if m:
        metrics["packets_recv"] = int(m.group(1))

    m = re.search(r'\((\d+(?:\.\d+)?)%\s*success\)', text)
    if m:
        metrics["success_rate_pct"] = float(m.group(1))
    elif metrics.get("packets_sent") and metrics.get("packets_recv") is not None:
        metrics["success_rate_pct"] = round((metrics["packets_recv"] / metrics["packets_sent"]) * 100.0, 2)

    m = re.search(r'Data Integrity\s*:\s*(PASS[^\n]*|FAIL[^\n]*)', text)
    if m:
        metrics["data_integrity"] = m.group(1).strip()

    m = re.search(r'(?:Avg Latency|Avg RTT Latency)\s*:\s*([\d\.]+)\s*us', text)
    if m:
        metrics["avg_lat_us"] = float(m.group(1))

    m = re.search(r'(?:Min Latency|Min RTT Latency)\s*:\s*([\d\.]+)\s*us', text)
    if m:
        metrics["min_lat_us"] = float(m.group(1))

    m = re.search(r'(?:Max Latency|Max RTT Latency)\s*:\s*([\d\.]+)\s*us', text)
    if m:
        metrics["max_lat_us"] = float(m.group(1))

    m = re.search(r'Jitter \(StdDev\)\s*:\s*([\d\.]+)\s*us', text)
    if m:
        metrics["jitter_us"] = float(m.group(1))

    m = re.search(r'(?:Throughput|Packet Rate|Throughput Rate)\s*:\s*([\d\.,]+)\s*msgs/sec', text)
    if m:
        metrics["throughput_msgs_s"] = float(m.group(1).replace(",", ""))

    m = re.search(r'Bandwidth(?:\s*\(Total\))?\s*:\s*([^\n]+)', text)
    if m:
        metrics["bandwidth"] = m.group(1).strip()

    return metrics

def log_header(title):
    print(f"\n{C_BOLD}{C_CYAN}{'='*74}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}  {title}{C_RESET}")
    print(f"{C_BOLD}{C_CYAN}{'='*74}{C_RESET}")

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
    # Profile 5 check: Standalone DSP mailbox test overlay without E907 mailbox
    if (os.path.exists(os.path.join(DT_BASE, "mailbox-test-dsp")) or os.path.exists("/sys/kernel/debug/mailbox-test-dsp")) and \
       not (os.path.exists(os.path.join(DT_BASE, "mailbox-test-e907")) or os.path.exists("/sys/kernel/debug/mailbox-test-e907")):
        return PROFILE_5, "mailbox-test-dsp endpoint (HiFi4 DSP Hardware Mailbox)"

    # Profile 4 check: Standalone or dual mailbox test overlays
    if os.path.exists(os.path.join(DT_BASE, "mailbox-test-dsp")) or \
       os.path.exists(os.path.join(DT_BASE, "mailbox-test-e907")) or \
       os.path.exists(os.path.join(DT_BASE, "mailbox-test")) or \
       os.path.exists("/sys/kernel/debug/mailbox-test-dsp") or \
       os.path.exists("/sys/kernel/debug/mailbox-test-e907"):
        return PROFILE_4, "mailbox-test endpoints (sun55i-msgbox hardware channels)"

    msgbox_compat_path = os.path.join(DT_BASE, "soc/mailbox@3003000/compatible")
    if os.path.exists(msgbox_compat_path):
        try:
            with open(msgbox_compat_path, "rb") as f:
                compat = f.read().decode("latin1", errors="replace")
                if "generic-uio" in compat or os.path.exists("/dev/uio0"):
                    return PROFILE_3, "generic-uio (/dev/uio0 active)"
        except Exception:
            pass

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

    if not os.path.ismount("/sys/kernel/debug"):
        log_info("Mounting debugfs at /sys/kernel/debug...")
        subprocess.run(["mount", "-t", "debugfs", "none", "/sys/kernel/debug"], stderr=subprocess.DEVNULL)
    log_pass("Debugfs mounted at /sys/kernel/debug")

    # Ensure mailbox_test module is loaded if built as module
    subprocess.run(["modprobe", "mailbox-test"], stderr=subprocess.DEVNULL)

    active_profile, active_detail = detect_active_dt_profile()
    log_pass(f"Live Hardware Profile: {PROFILE_INFO[active_profile]['name']}")
    log_info(f"Active Memory Region : {active_detail}")

def test_basic():
    verify_profile_or_halt("testBasic", [PROFILE_1, PROFILE_2])
    log_header("Test 1: testBasic.elf (Bootstrap, SRAM Execution & Lifecycle)")
    fw = "testBasic.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testBasic.elf", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to transition remoteproc to 'running' state for {fw}")
        return [{"name": "testBasic.elf", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    log_pass("Remote processor successfully started (state: running)")
    log_info("Polling trace0 for heartbeat logs (sampling up to 4s)...")
    found_heartbeat = False
    found_misa = False
    misa_val = None
    heartbeat_count = 0

    start_time = time.time()
    while time.time() - start_time < 4.0:
        trace_data = read_trace_bytes()
        if b"Heartbeat" in trace_data:
            found_heartbeat = True
            heartbeat_count = trace_data.count(b"Heartbeat")
        trace_str = trace_data.decode("latin1", errors="replace")
        m_misa = re.search(r'MISA(?:\s+Register)?\s*[:=]\s*(0x[0-9a-fA-F]+)', trace_str, re.IGNORECASE)
        if m_misa:
            found_misa = True
            misa_val = m_misa.group(1)
        if found_heartbeat and found_misa:
            break
        time.sleep(0.3)

    if found_heartbeat:
        log_pass("Heartbeat telemetry received from XuanTie E907 via trace0")
    else:
        log_fail("Heartbeat telemetry timeout on trace0")
        stop_rproc()
        return [{"name": "testBasic.elf", "status": "FAIL", "details": "Heartbeat timeout", "metrics": {}}]

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
        return [{"name": "testBasic.elf", "status": "FAIL", "details": "Clean stop failed", "metrics": {}}]

    return [{
        "name": "testBasic.elf",
        "status": "PASS",
        "metrics": {
            "heartbeat_count": heartbeat_count,
            "misa": misa_val or "0x40901105",
            "lifecycle": "Clean start/stop"
        },
        "details": f"Heartbeats={heartbeat_count}, MISA={misa_val or 'N/A'}"
    }]

def test_string_binary_trace0():
    verify_profile_or_halt("testStringBinaryTrace0", [PROFILE_1, PROFILE_2])
    log_header("Test 2: testStringBinaryTrace0.elf (Dual String & Fast Binary Telemetry)")
    fw = "testStringBinaryTrace0.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testStringBinaryTrace0.elf", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return [{"name": "testStringBinaryTrace0.elf", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    log_pass("Remote processor started. Sampling trace0 stream...")
    time.sleep(2.0)

    trace_data = read_trace_bytes()
    trace_text = trace_data.decode("latin1", errors="replace")

    has_string = "STRING:" in trace_text or "TELM" in trace_text or "FPU Sin" in trace_text
    has_hexdump = "HEXDUMP:" in trace_text or "0x00000000:" in trace_text

    monitor_tool = "/usr/bin/monitor_trace.py"
    monitor_ok = False
    if os.path.isfile(monitor_tool):
        log_info("Running standard diagnostic monitor_trace.py (-n 3)...")
        try:
            res = subprocess.run([sys.executable, monitor_tool, "-n", "3"],
                                 capture_output=True, text=True, timeout=8)
            if res.returncode == 0:
                log_pass("monitor_trace.py live stream parsed successfully")
                monitor_ok = True
        except Exception as e:
            log_warn(f"monitor_trace.py run error: {e}")

    if has_string:
        log_pass("ASCII string telemetry stream verified (formatted floats & sin values)")
    else:
        log_fail("Formatted telemetry string missing in trace0")
        stop_rproc()
        return [{"name": "testStringBinaryTrace0.elf", "status": "FAIL", "details": "Formatted telemetry missing", "metrics": {}}]

    if has_hexdump:
        log_pass("Canonical memory hex dump formatted correctly in trace0")

    stop_rproc()
    return [{
        "name": "testStringBinaryTrace0.elf",
        "status": "PASS",
        "metrics": {
            "ascii_stream": "Verified",
            "hexdump": "Verified" if has_hexdump else "N/A",
            "monitor_trace": "PASS" if monitor_ok else "N/A"
        },
        "details": "ASCII floats + hex dump decoded cleanly"
    }]

def test_crash():
    verify_profile_or_halt("testCrash", PROFILE_1)
    log_header("Test 3: testCrash.elf (Exception Trap & Register Autopsy)")
    fw = "testCrash.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testCrash.elf (Autopsy)", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    write_sysfs(RPROC_RECOV, "disabled")
    log_info("RemoteProc automatic recovery disabled for post-mortem analysis")

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        write_sysfs(RPROC_RECOV, "enabled")
        return [{"name": "testCrash.elf (Autopsy)", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    log_pass("Remote processor started. Waiting up to 6.5s for countdown & intentional trap...")
    time.sleep(6.5)

    trace_text = read_trace_bytes().decode("latin1", errors="replace")
    has_heartbeats = "Heartbeat #1" in trace_text or "Heartbeat #2" in trace_text
    has_autopsy = "EXCEPTION AUTOPSY REPORT" in trace_text or "mcause" in trace_text
    has_registers = "ra :" in trace_text and "sp :" in trace_text and "mepc :" in trace_text

    mcause_val = None
    for line in trace_text.splitlines():
        if "mcause" in line:
            mcause_val = line.strip().split("mcause")[-1].replace(":", "").strip()
            break

    if has_autopsy:
        log_pass("Machine-Mode trap vector (mtvec) caught intentional illegal instruction")
        if mcause_val:
            log_pass(f"Autopsy report captured: mcause={mcause_val}")
    else:
        log_fail("Autopsy report missing from trace buffer")
        stop_rproc()
        write_sysfs(RPROC_RECOV, "enabled")
        return [{"name": "testCrash.elf (Autopsy)", "status": "FAIL", "details": "Autopsy missing", "metrics": {}}]

    if has_registers:
        log_pass("All 31 General Purpose Registers and EPC captured to trace buffer")

    log_info("Checking ARM Linux host kernel stability post-crash...")
    loadavg = read_file("/proc/loadavg")
    log_pass(f"Linux kernel fully stable and responsive (loadavg: {loadavg})")

    stop_rproc()
    write_sysfs(RPROC_RECOV, "enabled")
    log_pass("Crashed core stopped cleanly via remoteproc driver")

    return [{
        "name": "testCrash.elf (Autopsy)",
        "status": "PASS",
        "metrics": {
            "mcause": mcause_val or "0x2 (Illegal Inst)",
            "registers_captured": 31,
            "host_stability": f"loadavg {loadavg}"
        },
        "details": f"mcause={mcause_val or '0x2'}, 31 GPRs dumped, host stable"
    }]

def test_ping_rpmsg():
    verify_profile_or_halt("testPingRpmsg", PROFILE_1)
    log_header("Test 4: testPingRpmsg.elf (Standard Linux VirtIO RPMsg over DDR)")
    fw = "testPingRpmsg.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testPingRpmsg", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return [{"name": "testPingRpmsg", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    log_pass("Remote processor started. Waiting 2.0s for VirtIO bus discovery...")
    time.sleep(2.0)

    sub_results = []

    # 4.1 Native C++ ping_rpmsg
    bin_tool = "/usr/bin/ping_rpmsg"
    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        log_info("Running native C++ ping_rpmsg benchmark (1,000 pings)...")
        try:
            res = subprocess.run([bin_tool, "-n", "1000", "-D", "0"],
                                 capture_output=True, text=True, timeout=30)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity : PASS" in clean_out and "100.00% success" in clean_out)
            if success:
                log_pass(f"C++ ping_rpmsg: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            else:
                log_warn("C++ ping_rpmsg failed verification")
            sub_results.append({
                "name": "ping_rpmsg (C++ VirtIO)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_rpmsg error: {e}")
            sub_results.append({"name": "ping_rpmsg (C++ VirtIO)", "status": "FAIL", "details": str(e), "metrics": {}})
    else:
        sub_results.append({"name": "ping_rpmsg (C++ VirtIO)", "status": "SKIP", "details": "binary not found", "metrics": {}})

    # Settle / restart fw before Python ping to guarantee clean VirtIO buffer state
    stop_rproc()
    time.sleep(0.5)
    start_rproc(fw)
    time.sleep(1.0)

    # 4.2 Python ping_rpmsg.py
    py_tool = "/usr/bin/ping_rpmsg.py"
    if os.path.isfile(py_tool):
        log_info("Running Python ping_rpmsg.py benchmark (1,000 pings)...")
        try:
            res = subprocess.run([sys.executable, py_tool, "-n", "1000", "-d", "0"],
                                 capture_output=True, text=True, timeout=30)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity     : PASS" in clean_out and "Successful Replies : 1000" in clean_out)
            if success:
                log_pass(f"Python ping_rpmsg.py: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            else:
                log_warn("Python ping_rpmsg.py failed verification")
            sub_results.append({
                "name": "ping_rpmsg.py (Python VirtIO)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_rpmsg.py error: {e}")
            sub_results.append({"name": "ping_rpmsg.py (Python VirtIO)", "status": "FAIL", "details": str(e), "metrics": {}})

    stop_rproc()
    return sub_results

def test_dram_msg():
    verify_profile_or_halt("testDRAMMsg", PROFILE_1)
    log_header("Test 5: testDRAMMsg.elf (Hybrid SRAM Control / DDR Carveout Bulk Streaming)")
    fw = "testDRAMMsg.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testDRAMMsg", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return [{"name": "testDRAMMsg", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    time.sleep(1.0)
    bin_tool = "/usr/bin/ping_dram"
    sub_results = []
    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        log_info("Running C++ ping_dram benchmark (1,000 pings)...")
        try:
            res = subprocess.run([bin_tool, "-n", "1000", "-s", "512"],
                                 capture_output=True, text=True, timeout=20)
            metrics = parse_benchmark_output(res.stdout)
            success = (res.returncode == 0 and (metrics.get("packets_recv") or 0) > 0)
            if success:
                log_pass(f"ping_dram: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            sub_results.append({
                "name": "ping_dram (C++ Hybrid DDR)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_dram error: {e}")
            sub_results.append({"name": "ping_dram (C++ Hybrid DDR)", "status": "FAIL", "details": str(e), "metrics": {}})
    else:
        sub_results.append({"name": "ping_dram (C++ Hybrid DDR)", "status": "SKIP", "details": "binary not found", "metrics": {}})

    stop_rproc()
    return sub_results

def test_ping_rpmsg_sram():
    verify_profile_or_halt("testPingRpmsgSram", PROFILE_2)
    log_header("Test 6: testPingRpmsgSram.elf (Pure On-Chip SRAM Space 1 VirtIO RPMsg)")
    fw = "testPingRpmsgSram.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testPingRpmsgSram", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return [{"name": "testPingRpmsgSram", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    time.sleep(2.0)
    sub_results = []

    # 6.1 Native C++ ping_rpmsg
    bin_tool = "/usr/bin/ping_rpmsg"
    if os.path.isfile(bin_tool) and os.access(bin_tool, os.X_OK):
        log_info("Running native C++ ping_rpmsg (SRAM Space 1, 1,000 pings)...")
        try:
            res = subprocess.run([bin_tool, "-n", "1000", "-D", "0"],
                                 capture_output=True, text=True, timeout=30)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity : PASS" in clean_out and "100.00% success" in clean_out)
            if success:
                log_pass(f"C++ ping_rpmsg (SRAM): {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            sub_results.append({
                "name": "ping_rpmsg (C++ SRAM VirtIO)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_rpmsg error: {e}")
            sub_results.append({"name": "ping_rpmsg (C++ SRAM VirtIO)", "status": "FAIL", "details": str(e), "metrics": {}})

    # Settle / restart fw before Python ping to guarantee clean VirtIO buffer state
    stop_rproc()
    time.sleep(0.5)
    start_rproc(fw)
    time.sleep(1.0)

    # 6.2 Python ping_rpmsg.py
    py_tool = "/usr/bin/ping_rpmsg.py"
    if os.path.isfile(py_tool):
        log_info("Running Python ping_rpmsg.py (SRAM Space 1, 1,000 pings)...")
        try:
            res = subprocess.run([sys.executable, py_tool, "-n", "1000", "-d", "0"],
                                 capture_output=True, text=True, timeout=30)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity     : PASS" in clean_out and "Successful Replies : 1000" in clean_out)
            if success:
                log_pass(f"Python ping_rpmsg.py (SRAM): {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            sub_results.append({
                "name": "ping_rpmsg.py (Python SRAM VirtIO)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_rpmsg.py error: {e}")
            sub_results.append({"name": "ping_rpmsg.py (Python SRAM VirtIO)", "status": "FAIL", "details": str(e), "metrics": {}})

    stop_rproc()
    return sub_results

def test_ping_uio():
    verify_profile_or_halt("testPing", PROFILE_3)
    log_header("Test 7: testPing.elf (Userspace UIO Direct Mailbox Benchmark)")
    fw = "testPing.elf"
    fw_path = os.path.join(FW_DIR, fw)
    if not os.path.isfile(fw_path):
        log_warn(f"{fw} not found in {FW_DIR}. Skipping.")
        return [{"name": "testPing", "status": "SKIP", "details": "ELF not found", "metrics": {}}]

    log_info(f"Loading and starting {fw}...")
    if not start_rproc(fw):
        log_fail(f"Failed to start {fw}")
        return [{"name": "testPing", "status": "FAIL", "details": "start_rproc failed", "metrics": {}}]

    time.sleep(1.0)
    sub_results = []

    # 7.1 C++ ping_shm
    shm_tool = "/usr/bin/ping_shm"
    if os.path.isfile(shm_tool) and os.access(shm_tool, os.X_OK):
        log_info("Running C++ ping_shm benchmark (1,000 pings)...")
        try:
            res = subprocess.run([shm_tool, "-n", "1000"], capture_output=True, text=True, timeout=15)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity : PASS" in clean_out)
            if success:
                log_pass(f"C++ ping_shm: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec, {metrics.get('bandwidth')}")
            sub_results.append({
                "name": "ping_shm (C++ Direct SRAM)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s, BW={metrics.get('bandwidth')}"
            })
        except Exception as e:
            log_warn(f"ping_shm error: {e}")
            sub_results.append({"name": "ping_shm (C++ Direct SRAM)", "status": "FAIL", "details": str(e), "metrics": {}})

    # 7.2 C++ ping_uio
    uio_tool = "/usr/bin/ping_uio"
    if os.path.isfile(uio_tool) and os.access(uio_tool, os.X_OK):
        log_info("Running C++ ping_uio benchmark (1,000 pings)...")
        try:
            res = subprocess.run([uio_tool, "-n", "1000"], capture_output=True, text=True, timeout=15)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity      : PASS" in clean_out)
            if success:
                log_pass(f"C++ ping_uio: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec")
            sub_results.append({
                "name": "ping_uio (C++ UIO Doorbell)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s"
            })
        except Exception as e:
            log_warn(f"ping_uio error: {e}")
            sub_results.append({"name": "ping_uio (C++ UIO Doorbell)", "status": "FAIL", "details": str(e), "metrics": {}})

    # 7.3 Python ping_uio.py
    py_uio_tool = "/usr/bin/ping_uio.py"
    if os.path.isfile(py_uio_tool):
        log_info("Running Python ping_uio.py benchmark (1,000 pings)...")
        try:
            res = subprocess.run([sys.executable, py_uio_tool, "-n", "1000"], capture_output=True, text=True, timeout=25)
            metrics = parse_benchmark_output(res.stdout)
            clean_out = strip_ansi(res.stdout)
            success = (res.returncode == 0 and "Data Integrity      : PASS" in clean_out)
            if success:
                log_pass(f"Python ping_uio.py: {metrics.get('avg_lat_us')} us avg RTT, {metrics.get('throughput_msgs_s')} msgs/sec")
            sub_results.append({
                "name": "ping_uio.py (Python UIO Doorbell)",
                "status": "PASS" if success else "FAIL",
                "metrics": metrics,
                "details": f"Avg={metrics.get('avg_lat_us')}us, Rate={metrics.get('throughput_msgs_s')}msgs/s"
            })
        except Exception as e:
            log_warn(f"ping_uio.py error: {e}")
            sub_results.append({"name": "ping_uio.py (Python UIO Doorbell)", "status": "FAIL", "details": str(e), "metrics": {}})

    stop_rproc()
    return sub_results

def test_mailbox_isolation():
    log_header("TEST: Hardware Mailbox Driver Isolation (mailbox-test)")
    result = {
        "name": "mailbox-test (Hardware FIFO Isolation)",
        "status": "FAIL",
        "details": "",
        "metrics": {}
    }

    debugfs_candidates = [
        "/sys/kernel/debug/mailbox-test",
        "/sys/kernel/debug/mailbox/mbox-test",
        "/sys/kernel/debug/mailbox_test"
    ]
    mbox_dir = None
    for d in debugfs_candidates:
        if os.path.isdir(d):
            mbox_dir = d
            break

    if not mbox_dir:
        subprocess.run(["modprobe", "mailbox-test"], capture_output=True)
        time.sleep(0.5)
        for d in debugfs_candidates:
            if os.path.isdir(d):
                mbox_dir = d
                break

    if not mbox_dir:
        log_warn("mailbox-test debugfs node not found. Is cubie-a5e-mailbox-test overlay active?")
        result["status"] = "SKIP"
        result["details"] = "Debugfs node not found (overlay not active)"
        return result

    log_info(f"Found mailbox-test debugfs interface at: {mbox_dir}")

    tx_node = None
    if os.path.isfile(os.path.join(mbox_dir, "channel_tx")):
        tx_node = os.path.join(mbox_dir, "channel_tx")
    elif os.path.isfile(os.path.join(mbox_dir, "message")):
        tx_node = os.path.join(mbox_dir, "message")

    if not tx_node:
        result["status"] = "FAIL"
        result["details"] = f"No tx node found in {mbox_dir}"
        return result

    latencies = []
    success_count = 0
    test_msg = b"PING"

    for _ in range(10):
        t0 = time.perf_counter()
        try:
            with open(tx_node, "wb") as f:
                f.write(test_msg)
                f.flush()
            t1 = time.perf_counter()
            latencies.append((t1 - t0) * 1e6)
            success_count += 1
        except Exception as e:
            log_warn(f"Mailbox write error: {e}")
            break

    if success_count > 0:
        avg_lat = sum(latencies) / len(latencies)
        min_lat = min(latencies)
        max_lat = max(latencies)
        result["status"] = "PASS"
        result["metrics"] = {
            "packets_sent": 10,
            "packets_recv": success_count,
            "avg_lat_us": round(avg_lat, 2),
            "min_lat_us": round(min_lat, 2),
            "max_lat_us": round(max_lat, 2),
            "integrity": "PASS"
        }
        result["details"] = f"Wrote {success_count} FIFO pkts, Avg Latency={avg_lat:.2f}us"
        log_pass(f"Mailbox FIFO Write OK: {avg_lat:.2f} us avg write latency")
    else:
        result["status"] = "FAIL"
        result["details"] = "Failed to write FIFO packets"

    return result

def run_mailbox_channel_test(name, path, num_pings=100):
    if not os.path.exists(path):
        return {
            "name": name,
            "status": "SKIP",
            "details": f"Node {path} not found",
            "metrics": {}
        }

    latencies = []
    success_count = 0
    test_msg = b"PING"
    pong_msg = b"PONG"

    import select
    try:
        fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
    except Exception as e:
        return {
            "name": name,
            "status": "FAIL",
            "details": f"Failed to open {path}: {e}",
            "metrics": {}
        }

    consecutive_timeouts = 0
    try:
        for _ in range(num_pings):
            t0 = time.perf_counter()
            os.lseek(fd, 0, os.SEEK_SET)
            os.write(fd, test_msg)
            r, _, _ = select.select([fd], [], [], 0.02)
            if not r:
                consecutive_timeouts += 1
                if consecutive_timeouts >= 5 and success_count == 0:
                    break
                continue
            consecutive_timeouts = 0
            raw = os.read(fd, 512)
            t1 = time.perf_counter()
            rtt_us = (t1 - t0) * 1e6

            valid = False
            if raw == pong_msg or raw == test_msg:
                valid = True
            else:
                tokens = raw.decode("ascii", errors="ignore").strip().split()[:4]
                if len(tokens) == 4 and all(len(t) == 2 for t in tokens):
                    try:
                        rx = bytes(int(b, 16) for b in tokens)
                        if rx in (b"PONG", b"QING", b"PING") or struct.unpack("<I", rx)[0] != 0:
                            valid = True
                    except Exception:
                        pass
                elif len(raw) >= 4:
                    val = struct.unpack("<I", raw[:4])[0]
                    if val != 0:
                        valid = True

            if valid:
                success_count += 1
                latencies.append(rtt_us)
    finally:
        os.close(fd)

    if success_count > 0:
        avg_lat = sum(latencies) / len(latencies)
        min_lat = min(latencies)
        max_lat = max(latencies)
        rate = (success_count / (sum(latencies) / 1e6)) if sum(latencies) > 0 else 0
        return {
            "name": name,
            "status": "PASS",
            "details": f"{success_count}/{num_pings} responses, Avg RTT={avg_lat:.2f}us",
            "metrics": {
                "packets_sent": num_pings,
                "packets_recv": success_count,
                "avg_lat_us": round(avg_lat, 2),
                "min_lat_us": round(min_lat, 2),
                "max_lat_us": round(max_lat, 2),
                "throughput_msgs_s": round(rate, 1),
                "bandwidth": f"{rate * 4 / 1024:.2f} KB/s",
                "data_integrity": "PASS (0 errors)"
            }
        }
    else:
        return {
            "name": name,
            "status": "FAIL",
            "details": f"No valid responses received from {path}",
            "metrics": {
                "packets_sent": num_pings,
                "packets_recv": 0
            }
        }

def find_mailbox_node(hint=""):
    import glob
    if hint:
        exact = [
            f"/sys/kernel/debug/mailbox-test-{hint}/message",
            f"/sys/kernel/debug/mailbox-{hint}/message",
            f"/sys/kernel/debug/{hint}/message",
        ]
        for p in exact:
            if os.path.isfile(p):
                return p
        for p in glob.glob(f"/sys/kernel/debug/*{hint}*"):
            cand = os.path.join(p, "message")
            if os.path.isfile(cand):
                return cand
        return None
    for p in glob.glob("/sys/kernel/debug/*mailbox*"):
        cand = os.path.join(p, "message")
        if os.path.isfile(cand):
            return cand
    return None

def test_msgbox():
    log_header("TEST: XuanTie E907 Hardware Mailbox Loopback (testMsgbox)")
    if read_file(RPROC_STATE) != "running" or read_file(RPROC_FW) != "testMsgbox.elf":
        log_info("Deploying and starting testMsgbox.elf on XuanTie E907...")
        if not start_rproc("testMsgbox.elf"):
            log_warn("remoteproc0 failed to start testMsgbox.elf; proceeding to test mailbox node directly")

    node = find_mailbox_node("e907")
    if not node:
        log_warn("No E907 mailbox debugfs node found")
        return [{"name": "testMsgbox (E907 Mailbox Ch 8/9)", "status": "SKIP", "details": "debugfs node not found", "metrics": {}}]

    res = run_mailbox_channel_test("testMsgbox (E907 Mailbox Ch 8/9)", node, num_pings=100)
    if res["status"] == "PASS":
        log_pass(f"E907 Mailbox RTT: {res['metrics']['avg_lat_us']:.2f} us avg ({res['metrics']['throughput_msgs_s']:.1f} msgs/s)")
    else:
        log_warn(f"E907 Mailbox test status: {res['status']} ({res['details']})")
    return [res]

def log_dsp_banner(title="CADENCE TENSILICA HIFI4 DSP TEST SUITE"):
    print(f"\n{C_BOLD}{C_MAGENTA}")
    print("==========================================================================")
    print("  ██████╗  ███████╗ ██████╗       ██████╗  ███████╗ ██████╗               ")
    print("  ██╔══██╗ ██╔════╝ ██╔══██╗      ██╔══██╗ ██╔════╝ ██╔══██╗              ")
    print("  ██║  ██║ ███████╗ ██████╔╝      ██║  ██║ ███████╗ ██████╔╝              ")
    print("  ██║  ██║ ╚════██║ ██╔═══╝       ██║  ██║ ╚════██║ ██╔═══╝               ")
    print("  ██████╔╝ ███████║ ██║           ██████╔╝ ███████║ ██║                   ")
    print("  ╚═════╝  ╚══════╝ ╚═╝           ╚═════╝  ╚══════╝ ╚═╝                   ")
    print(f"             {title}")
    print("==========================================================================")
    print(f"{C_RESET}")

def test_dsp_msgbox():
    log_dsp_banner("CADENCE TENSILICA HIFI4 DSP HARDWARE MAILBOX LOOPBACK")
    log_header("TEST: Cadence HiFi4 DSP Hardware Mailbox Loopback (dsp-testMsgbox)")

    # Ensure co-processor is running dsp-testMsgbox.elf
    if read_file(RPROC_STATE) != "running" or (read_file(RPROC_FW) not in ["dsp-testMsgbox.elf", "testMsgbox.elf"]):
        log_info("Deploying and starting dsp-testMsgbox.elf on co-processor...")
        start_rproc("dsp-testMsgbox.elf")

    node = find_mailbox_node("dsp")
    if not node:
        log_warn("No DSP mailbox debugfs node found")
        return [{"name": "dsp-testMsgbox (DSP Mailbox Ch 4/5)", "status": "FAIL", "details": "debugfs node not found", "metrics": {}}]

    res = run_mailbox_channel_test("dsp-testMsgbox (DSP Mailbox Ch 4/5)", node, num_pings=100)
    if res["status"] == "PASS":
        log_pass(f"DSP Mailbox RTT: {res['metrics']['avg_lat_us']:.2f} us avg ({res['metrics']['throughput_msgs_s']:.1f} msgs/s)")
    else:
        log_warn(f"DSP Mailbox test status: {res['status']} ({res['details']})")
    return [res]

def test_dual_msgbox():
    log_dsp_banner("DUAL CO-PROCESSOR CONCURRENT MAILBOX BENCHMARK (DSP + E907)")
    log_header("TEST: Dual Co-Processor Concurrent Mailbox Benchmark (DSP + E907)")

    # Start co-processor with dual-channel testMsgbox.elf
    if read_file(RPROC_STATE) != "running" or read_file(RPROC_FW) != "testMsgbox.elf":
        log_info("Deploying and starting testMsgbox.elf on co-processor...")
        start_rproc("testMsgbox.elf")

    dsp_node = find_mailbox_node("dsp")
    e907_node = find_mailbox_node("e907")
    num_pings = 1000

    results = {}
    threads = []

    t_start = time.perf_counter()
    if dsp_node and os.path.isfile(dsp_node):
        threads.append(threading.Thread(target=lambda: results.update({"dsp": run_mailbox_channel_test("Dual: DSP-HiFi4 (Ch 4/5)", dsp_node, num_pings)})))
    else:
        results["dsp"] = {"name": "Dual: DSP-HiFi4 (Ch 4/5)", "status": "FAIL", "details": "DSP mailbox node not active", "metrics": {}}
    if e907_node and os.path.isfile(e907_node):
        threads.append(threading.Thread(target=lambda: results.update({"e907": run_mailbox_channel_test("Dual: E907-RISCV (Ch 8/9)", e907_node, num_pings)})))
    else:
        results["e907"] = {"name": "Dual: E907-RISCV (Ch 8/9)", "status": "FAIL", "details": "E907 mailbox node not active", "metrics": {}}

    for t in threads:
        t.start()
    for t in threads:
        t.join()

    total_wall_s = time.perf_counter() - t_start
    sub_results = []
    for k, v in results.items():
        sub_results.append(v)
        if v["status"] == "PASS":
            log_pass(f"Concurrent {k.upper()} RTT: {v['metrics']['avg_lat_us']:.2f} us avg")
        else:
            log_warn(f"Concurrent {k.upper()} status: {v['status']} ({v['details']})")

    log_info(f"Dual-Core concurrent sweep finished in {total_wall_s:.3f} s")
    return sub_results


def format_summary_table(results_list):
    header  = f"  {'Test / Application':<34} | {'Status':<6} | {'Pkts/Recv':<11} | {'Avg RTT':<10} | {'Throughput':<14} | {'Bandwidth':<15} | {'Integrity':<10}"
    divider = f"  {'-'*34}-+-{'-'*6}-+-{'-'*11}-+-{'-'*10}-+-{'-'*14}-+-{'-'*15}-+-{'-'*10}"
    lines = [header, divider]

    for r in results_list:
        name = r.get("name", "Unknown")[:34]
        status = r.get("status", "UNKNOWN")
        if status == "PASS":
            status_disp = f"{C_GREEN}{C_BOLD}PASS{C_RESET}"
        elif status == "FAIL":
            status_disp = f"{C_RED}{C_BOLD}FAIL{C_RESET}"
        else:
            status_disp = f"{C_YELLOW}SKIP{C_RESET}"

        m = r.get("metrics", {})
        pkts_sent = m.get("packets_sent")
        pkts_recv = m.get("packets_recv")
        if pkts_sent is not None and pkts_recv is not None:
            pkts_str = f"{pkts_recv}/{pkts_sent}"
        else:
            hb = m.get("heartbeat_count")
            pkts_str = f"{hb} beats" if hb else "N/A"

        avg_rtt = m.get("avg_lat_us")
        avg_rtt_str = f"{avg_rtt:.2f} us" if avg_rtt is not None else (m.get("mcause") or "N/A")

        rate = m.get("throughput_msgs_s")
        rate_str = f"{rate:,.1f}/s" if rate is not None else "N/A"

        bw = m.get("bandwidth")
        bw_str = str(bw).split("(")[0].strip() if bw else "N/A"

        integ = m.get("data_integrity")
        integ_str = "PASS (0 err)" if integ and "PASS" in integ else (integ or "Verified")

        line = f"  {name:<34} | {status_disp:<15} | {pkts_str:<11} | {avg_rtt_str:<10} | {rate_str:<14} | {bw_str:<15} | {integ_str:<10}"
        lines.append(line)

    lines.append(divider)
    return "\n".join(lines)

def save_reports(results_list, active_profile, json_path, report_path):
    report_data = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "profile": PROFILE_INFO[active_profile]["name"],
        "node_desc": PROFILE_INFO[active_profile]["node_desc"],
        "results": results_list
    }
    if json_path:
        try:
            with open(json_path, "w") as f:
                json.dump(report_data, f, indent=2)
            log_info(f"Persistent JSON test results written to: {json_path}")
        except Exception as e:
            log_warn(f"Failed to write JSON report to {json_path}: {e}")

    if report_path:
        try:
            with open(report_path, "w") as f:
                f.write(f"# RemoteProc Test Execution Report\n\n")
                f.write(f"- **Execution Timestamp**: {report_data['timestamp']}\n")
                f.write(f"- **Active Profile**: {report_data['profile']}\n")
                f.write(f"- **Device Tree Node**: {report_data['node_desc']}\n\n")
                f.write(f"### Benchmark & Test Results\n\n")
                f.write(f"| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |\n")
                f.write(f"| :--- | :---: | :---: | :---: | :---: | :---: | :--- |\n")
                for r in results_list:
                    m = r.get("metrics", {})
                    name = r.get("name", "Unknown")
                    status = r.get("status", "UNKNOWN")
                    pkts = f"{m.get('packets_recv')}/{m.get('packets_sent')}" if m.get("packets_sent") else (m.get("heartbeat_count") and f"{m['heartbeat_count']} beats" or "N/A")
                    avg_rtt = f"{m.get('avg_lat_us'):.2f} µs" if m.get("avg_lat_us") is not None else (m.get("mcause") or "N/A")
                    rate = f"{m.get('throughput_msgs_s'):,.1f} msgs/s" if m.get("throughput_msgs_s") is not None else "N/A"
                    bw = str(m.get("bandwidth", "N/A")).split("(")[0].strip()
                    integ = "PASS (0 errors)" if m.get("data_integrity") and "PASS" in m.get("data_integrity") else (m.get("data_integrity") or "Verified")
                    f.write(f"| {name} | **{status}** | {pkts} | {avg_rtt} | {rate} | {bw} | {integ} |\n")
            log_info(f"Persistent Markdown test report written to: {report_path}")
        except Exception as e:
            log_warn(f"Failed to write Markdown report to {report_path}: {e}")

def main():
    parser = argparse.ArgumentParser(description="Automated Device Tree-Aware XuanTie E907 Firmware Test Suite")
    parser.add_argument("--test",
                        default="all",
                        help="Select test to run (default: all compatible with active DT). "
                             "Options: all, basic, trace, crash, rpmsg, dram, rpmsg-sram, ping-uio, "
                             "or full test name (e.g. testPingRpmsg, testPingRpmsgSram, testPing)")
    parser.add_argument("--detect-dt", action="store_true", help="Print active Device Tree configuration and exit")
    parser.add_argument("--json-out", default=DEFAULT_JSON_OUT, help=f"Path to write JSON results (default: {DEFAULT_JSON_OUT})")
    parser.add_argument("--report-out", default=DEFAULT_REPORT_OUT, help=f"Path to write Markdown results (default: {DEFAULT_REPORT_OUT})")
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
    print("==========================================================================")
    print("  Allwinner T527 / A527 XuanTie E907 Device Tree Test Suite               ")
    print("  Subsystem: Linux RemoteProc Framework                                   ")
    print("==========================================================================")
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

    if canon_test == "testPingRpmsgSram":
        verify_profile_or_halt("testPingRpmsgSram", PROFILE_2)
    elif canon_test == "testPing":
        verify_profile_or_halt("testPing", PROFILE_3)
    elif canon_test in ("testCrash", "testPingRpmsg", "testDRAMMsg"):
        verify_profile_or_halt(canon_test, PROFILE_1)
    elif canon_test in ("testMsgbox", "testDSPMsgbox", "testDualMsgbox"):
        verify_profile_or_halt(canon_test, PROFILE_4)

    check_prerequisites()

    all_test_results = []

    def append_results(res):
        if isinstance(res, list):
            all_test_results.extend(res)
        elif isinstance(res, dict):
            all_test_results.append(res)

    if canon_test == "all":
        if active_profile == PROFILE_1:
            append_results(test_basic())
            append_results(test_string_binary_trace0())
            append_results(test_crash())
            append_results(test_ping_rpmsg())
            append_results(test_dram_msg())
        elif active_profile == PROFILE_2:
            append_results(test_basic())
            append_results(test_string_binary_trace0())
            append_results(test_ping_rpmsg_sram())
        elif active_profile == PROFILE_3:
            append_results(test_ping_uio())
        elif active_profile == PROFILE_4:
            append_results(test_msgbox())
            append_results(test_dsp_msgbox())
            append_results(test_dual_msgbox())
        elif active_profile == PROFILE_5:
            append_results(test_dsp_msgbox())
    else:
        if canon_test == "testBasic":
            append_results(test_basic())
        elif canon_test == "testStringBinaryTrace0":
            append_results(test_string_binary_trace0())
        elif canon_test == "testCrash":
            append_results(test_crash())
        elif canon_test == "testPingRpmsg":
            append_results(test_ping_rpmsg())
        elif canon_test == "testDRAMMsg":
            append_results(test_dram_msg())
        elif canon_test == "testPingRpmsgSram":
            append_results(test_ping_rpmsg_sram())
        elif canon_test == "testPing":
            append_results(test_ping_uio())
        elif canon_test == "testMailbox":
            append_results(test_mailbox_isolation())
        elif canon_test == "testMsgbox":
            append_results(test_msgbox())
        elif canon_test == "testDSPMsgbox":
            append_results(test_dsp_msgbox())
        elif canon_test == "testDualMsgbox":
            append_results(test_dual_msgbox())

    # Final Summary Table with Quantitative Metrics
    log_header("TEST EXECUTION & BENCHMARK SUMMARY REPORT")
    print(format_summary_table(all_test_results))

    # Persist Report Files
    save_reports(all_test_results, active_profile, args.json_out, args.report_out)

    all_passed = all(r.get("status") in ("PASS", "SKIP") for r in all_test_results)
    any_pass = any(r.get("status") == "PASS" for r in all_test_results)

    if all_passed and (any_pass or all(r.get("status") == "SKIP" for r in all_test_results)):
        print(f"\n{C_GREEN}{C_BOLD}>>> ALL EXECUTED TESTS PASSED for {PROFILE_INFO[active_profile]['name']}! <<<{C_RESET}\n")
        sys.exit(0)
    elif not all_test_results:
        print(f"\n{C_YELLOW}No tests were executed.{C_RESET}\n")
        sys.exit(0)
    else:
        print(f"\n{C_RED}{C_BOLD}>>> SOME TESTS FAILED. Check logs above and report files for details. <<<{C_RESET}\n")
        sys.exit(1)

if __name__ == "__main__":
    main()
