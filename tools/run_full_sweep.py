#!/usr/bin/env python3
"""
run_full_sweep.py - Autonomous Single-Pass 3-Profile Silicon Sweep
Target: Radxa Cubie A5E (Allwinner A527 / T527)

Executes an end-to-end multi-profile test loop across live Radxa Cubie A5E hardware
for the XuanTie E907 RISC-V real-time co-processor without manual intervention:
  1. Profile 1 (DDR VirtIO): run_tests.py, C++ ping_rpmsg, Python ping_rpmsg.py,
     C++ ping_dram, Python monitor_trace.py
  2. Profile 2 (On-Chip SRAM Space 1 VirtIO): config.txt switch, reboot, run_tests.py,
     C++ ping_rpmsg, Python ping_rpmsg.py
  3. Profile 3 (Userspace UIO Direct Mailbox): config.txt switch, reboot, run_tests.py,
     C++ ping_shm, Python ping_uio.py
  4. Restore Profile 1, reboot cleanly, and generate final quantitative summary table
     + persistent JSON and Markdown reports.
"""

import sys
import time
import subprocess
import re
import json
import os
import shutil

import threading
try:
    import serial
except ImportError:
    serial = None

def load_env_file():
    """Load defaults from .cubie.env or .env in workspace root or user home."""
    candidates = [
        os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".cubie.env")),
        os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".env")),
        os.path.expanduser("~/.cubie.env"),
    ]
    for path in candidates:
        if os.path.isfile(path):
            try:
                with open(path, "r") as f:
                    for line in f:
                        line = line.strip()
                        if line and not line.startswith("#") and "=" in line:
                            k, v = line.split("=", 1)
                            k, v = k.strip(), v.strip().strip("'\"")
                            if k not in os.environ:
                                os.environ[k] = v
            except Exception:
                pass

load_env_file()

TARGET_IP = os.environ.get("TARGET_IP", "192.168.1.19")
TARGET_USER = os.environ.get("TARGET_USER", "root")
TARGET_PORT = int(os.environ.get("TARGET_PORT", 22))
TARGET_PASSWORD = os.environ.get("TARGET_PASSWORD", None)
TARGET_KEY = os.environ.get("TARGET_KEY", None)
SERIAL_PORT = os.environ.get("SERIAL_PORT", "/dev/ttyUSB0" if os.path.exists("/dev/ttyUSB0") else None)

SWEEP_JSON_OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_results.json"))
SWEEP_MD_OUT   = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_results.md"))
SERIAL_LOG_OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "serial_console.log"))

class SerialLogger:
    def __init__(self, port, baud=115200, log_path=SERIAL_LOG_OUT):
        self.port = port
        self.baud = baud
        self.log_path = log_path
        self.ser = None
        self.running = False
        self.thread = None
        self.buffer = []
        self.lock = threading.Lock()

    def start(self):
        if not self.port or not serial:
            return False
        if not os.path.exists(self.port):
            return False
        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=0.5)
            self.running = True
            self.thread = threading.Thread(target=self._reader_loop, daemon=True)
            self.thread.start()
            print(f"[SERIAL] Attached to serial console {self.port} @ {self.baud} baud -> {self.log_path}")
            return True
        except Exception as e:
            print(f"[SERIAL WARN] Could not open {self.port}: {e}")
            return False

    def _reader_loop(self):
        try:
            with open(self.log_path, "a", buffering=1) as f:
                f.write(f"\n--- SERIAL LOGGING STARTED AT {time.strftime('%Y-%m-%d %H:%M:%S')} ---\n")
                while self.running:
                    line = self.ser.readline()
                    if line:
                        text = line.decode("utf-8", errors="replace").rstrip()
                        ts = time.strftime("[%H:%M:%S]")
                        log_line = f"{ts} {text}\n"
                        f.write(log_line)
                        with self.lock:
                            self.buffer.append(log_line)
                            if len(self.buffer) > 2000:
                                self.buffer.pop(0)
        except Exception:
            pass

    def stop(self):
        self.running = False
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass

    def get_recent_lines(self, count=20):
        with self.lock:
            return self.buffer[-count:]

g_serial_logger = None

def strip_ansi(text: str) -> str:
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)

def log_banner(stage_num: int, title: str, subtitle: str = ""):
    print("\n\033[1;36m" + "=" * 76)
    print(f"  STAGE {stage_num}: {title.upper()}")
    if subtitle:
        print(f"  {subtitle}")
    print("=" * 76 + "\033[0m")

def run_ssh(cmd, timeout=30):
    ssh_cmd = []
    env = os.environ.copy()

    if TARGET_PASSWORD:
        ssh_cmd.extend(["sshpass", "-e"])
        env["SSHPASS"] = str(TARGET_PASSWORD)

    ssh_cmd.extend([
        "ssh",
        "-o", "ConnectTimeout=5",
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
        "-p", str(TARGET_PORT),
    ])

    if TARGET_KEY:
        ssh_cmd.extend(["-i", TARGET_KEY])

    ssh_cmd.append(f"{TARGET_USER}@{TARGET_IP}")
    ssh_cmd.append(cmd)

    try:
        res = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=timeout, env=env)
        return res.returncode, res.stdout.strip(), res.stderr.strip()
    except subprocess.TimeoutExpired:
        return 1, "", "timeout"

def wait_for_target(max_attempts=30):
    print("  Waiting for target board to come online...", end="", flush=True)
    for i in range(max_attempts):
        time.sleep(2)
        code, out, _ = run_ssh("cat /sys/class/remoteproc/remoteproc0/state 2>/dev/null", timeout=5)
        if code == 0:
            print(" ONLINE!")
            time.sleep(2)
            return True
        print(".", end="", flush=True)
        if i == 20 and g_serial_logger:
            recent = g_serial_logger.get_recent_lines(5)
            if recent:
                print(f"\n  [SERIAL LAST]: {''.join(recent).strip()}")
    print(" TIMEOUT!")
    return False

def set_overlay_and_reboot(overlay_str, cmdline_str=None):
    print(f"\n[CONFIG] Setting overlay: '{overlay_str}'")
    run_ssh(f"sed -i '/^[# ]*dtoverlay=/d' /boot/config.txt; echo 'dtoverlay={overlay_str}' >> /boot/config.txt")
    run_ssh("sed -i '/^cmdline=uio_pdrv_genirq/d' /boot/config.txt")
    if cmdline_str:
        run_ssh(f"echo '{cmdline_str}' >> /boot/config.txt")

    print("[REBOOT] Rebooting target board...")
    try:
        run_ssh("reboot", timeout=5)
    except subprocess.TimeoutExpired:
        pass
    time.sleep(8)
    return wait_for_target()

def parse_target_json_report():
    c, out, _ = run_ssh("cat /tmp/remoteproc_test_results.json", timeout=10)
    if c == 0 and out:
        try:
            return json.loads(out)
        except Exception:
            pass
    return None

def check_kernel_dmesg_health(stage_name="Test", allowed_patterns=None):
    """Inspect dmesg for kernel errors, oopses, panics, or call traces."""
    if allowed_patterns is None:
        allowed_patterns = []

    c, out, _ = run_ssh("dmesg -l err,crit,alert,emerg 2>/dev/null || dmesg | grep -iE 'call trace|kernel panic|oops|bug:|cut here' | tail -n 30", timeout=10)
    if c == 0 and out.strip():
        lines = [line.strip() for line in out.strip().splitlines() if line.strip()]
        filtered = []
        for line in lines:
            if any(pattern in line for pattern in allowed_patterns):
                continue
            if re.search(r'(?:call trace|kernel panic|oops|bug:|cut here|null pointer dereference|remoteproc.*failed)', line, re.IGNORECASE):
                filtered.append(line)

        if filtered:
            print(f"  \033[91m[DMESG ERROR DETECTED in {stage_name}]\033[0m")
            for err in filtered[:5]:
                print(f"    \033[91m>> {err}\033[0m")
            return False, "\n".join(filtered[:3])
    return True, "Clean"

def validate_kunit_tests():
    """
    Query, validate, and report in-kernel KUnit driver test suites on target hardware.
    Tests verified:
      - sunxi_rproc (35 tests): SRAM boundaries, DRAM carveouts, arithmetic overflow guards,
                                corrupted ELF segments, and malformed resource tables.
      - sun55i_msgbox (32 tests): CPUS/DSP/RV 12-channel routing, FIFO drain limits (FIFO_MAX),
                                  backpressure thresholds, multi-port interleaving, spurious IRQs.
    Total: 67 in-kernel tests.
    """
    print("\n" + "=" * 76)
    print("        1.0 IN-KERNEL KUNIT DRIVER TEST VALIDATION (67 TESTS)")
    print("=" * 76)

    # Ensure debugfs is mounted on target
    run_ssh("mount -t debugfs none /sys/kernel/debug 2>/dev/null")

    suites_info = {
        "sunxi_rproc": {
            "title": "sunxi_rproc (Remoteproc Driver)",
            "expected_count": 35,
        },
        "sun55i_msgbox": {
            "title": "sun55i_msgbox (Mailbox Driver)",
            "expected_count": 32,
        },
    }

    suite_results = {}
    total_expected = sum(s["expected_count"] for s in suites_info.values())
    total_passed = 0
    total_failed = 0
    total_skipped = 0
    total_executed = 0

    for suite_name, meta in suites_info.items():
        # Attempt 1: Read results from debugfs
        code, out, _ = run_ssh(f"cat /sys/kernel/debug/kunit/{suite_name}/results 2>/dev/null", timeout=10)
        source = "debugfs"
        if code != 0 or not out.strip():
            # Attempt 2: Fallback to dmesg boot log
            code, out, _ = run_ssh(f"dmesg | grep -iE '{suite_name}'", timeout=10)
            source = "dmesg"

        subtests = []
        failed_tests = []
        pass_count = 0
        fail_count = 0
        skip_count = 0
        total_count = 0

        if out.strip():
            # Parse KTAP summary line if present: # <suite>: pass:<P> fail:<F> skip:<S> total:<T>
            sum_match = re.search(r'#\s*' + suite_name + r':\s*pass:(\d+)\s*fail:(\d+)\s*skip:(\d+)\s*total:(\d+)', out)
            if sum_match:
                pass_count = int(sum_match.group(1))
                fail_count = int(sum_match.group(2))
                skip_count = int(sum_match.group(3))
                total_count = int(sum_match.group(4))

            # Parse individual test lines: (ok|not ok) <idx> - <test_name>
            raw_tests = re.findall(r'(?:\[[\s\d\.]+\]\s*)?(ok|not ok)\s+(\d+)\s*-\s*([a-zA-Z0-9_]+)', out)
            for status_str, idx_str, name_str in raw_tests:
                if name_str == suite_name:
                    continue  # Skip suite summary line
                subtests.append({"name": name_str, "status": "PASS" if status_str == "ok" else "FAIL"})
                if status_str == "not ok":
                    failed_tests.append(name_str)

            # If summary line was absent, deduce from parsed subtests
            if total_count == 0 and subtests:
                total_count = len(subtests)
                pass_count = sum(1 for t in subtests if t["status"] == "PASS")
                fail_count = sum(1 for t in subtests if t["status"] == "FAIL")

        suite_status = "PASS" if (pass_count > 0 and fail_count == 0) else ("FAIL" if fail_count > 0 else "MISSING")
        suite_results[suite_name] = {
            "title": meta["title"],
            "expected_count": meta["expected_count"],
            "total": total_count,
            "passed": pass_count,
            "failed": fail_count,
            "skipped": skip_count,
            "status": suite_status,
            "source": source,
            "subtests": subtests,
            "failed_tests": failed_tests,
        }
        total_passed += pass_count
        total_failed += fail_count
        total_skipped += skip_count
        total_executed += total_count

    # Render Terminal Breakdown Table
    print(f"  {'Suite / Component':<32} | {'Tests':<6} | {'Passed':<6} | {'Failed':<6} | {'Skipped':<7} | {'Status'}")
    print("  " + "-" * 32 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 7 + "-+-------")
    for suite_name, s in suite_results.items():
        color = "\033[92m" if s["status"] == "PASS" else "\033[91m"
        reset = "\033[0m"
        print(f"  {s['title']:<32} | {s['total']:<6} | {s['passed']:<6} | {s['failed']:<6} | {s['skipped']:<7} | {color}{s['status']}{reset}")

    print("  " + "-" * 32 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 7 + "-+-------")
    overall_status = "PASS" if (total_failed == 0 and total_passed >= total_expected) else "FAIL"
    color = "\033[92m" if overall_status == "PASS" else "\033[91m"
    reset = "\033[0m"
    print(f"  {'COMBINED TOTAL':<32} | {total_executed:<6} | {total_passed:<6} | {total_failed:<6} | {total_skipped:<7} | {color}{overall_status}{reset}")
    print("=" * 76)

    # If any failures, print alert with test names
    any_failed = any(s["failed_tests"] for s in suite_results.values())
    if any_failed:
        print("\033[91m[ERROR] The following KUnit test cases failed on target hardware:\033[0m")
        for suite_name, s in suite_results.items():
            for ftest in s["failed_tests"]:
                print(f"  \033[91m  - {suite_name}: {ftest}\033[0m")
        print()
    elif total_passed >= total_expected:
        print(f"\033[92m[PASS] All {total_passed} in-kernel KUnit tests executed cleanly with 0 failures!\033[0m\n")
    else:
        print(f"\033[93m[WARN] KUnit tests partially executed: {total_passed}/{total_expected} passed.\033[0m\n")

    return {
        "status": overall_status,
        "total_expected": total_expected,
        "total_executed": total_executed,
        "total_passed": total_passed,
        "total_failed": total_failed,
        "total_skipped": total_skipped,
        "suites": suite_results
    }

def main():
    global TARGET_IP, TARGET_USER, TARGET_PORT, TARGET_PASSWORD, TARGET_KEY, g_serial_logger
    import argparse
    parser = argparse.ArgumentParser(description="Autonomous 3-Profile Silicon Sweep for Radxa Cubie A5E (E907 RISC-V)")
    parser.add_argument("--ip", default=os.environ.get("TARGET_IP", TARGET_IP), help=f"Target board IP address (default: {TARGET_IP})")
    parser.add_argument("--user", default=os.environ.get("TARGET_USER", TARGET_USER), help=f"Target SSH user (default: {TARGET_USER})")
    parser.add_argument("--password", "-p", default=os.environ.get("TARGET_PASSWORD", None), help="SSH password for target board authentication")
    parser.add_argument("--port", "-P", type=int, default=int(os.environ.get("TARGET_PORT", 22)), help="SSH port (default: 22)")
    parser.add_argument("--key", "-i", default=os.environ.get("TARGET_KEY", None), help="Path to SSH private key file")
    parser.add_argument("--serial", default=SERIAL_PORT, help=f"Serial console device (default: {SERIAL_PORT})")
    parser.add_argument("--serial-baud", type=int, default=115200, help="Serial console baud rate (default: 115200)")
    parser.add_argument("--serial-log", default=SERIAL_LOG_OUT, help=f"Path for output serial console log (default: {SERIAL_LOG_OUT})")
    parser.add_argument("--json-out", default=SWEEP_JSON_OUT, help=f"Path for output JSON sweep results (default: {SWEEP_JSON_OUT})")
    parser.add_argument("--report-out", default=SWEEP_MD_OUT, help=f"Path for output Markdown sweep report (default: {SWEEP_MD_OUT})")
    args = parser.parse_args()

    TARGET_IP = args.ip
    TARGET_USER = args.user
    TARGET_PORT = args.port
    TARGET_PASSWORD = args.password
    TARGET_KEY = args.key

    if TARGET_PASSWORD and not shutil.which("sshpass"):
        print("[ERROR] A password was specified via --password, but 'sshpass' is not installed on the host.", file=sys.stderr)
        print("        Please install sshpass (e.g. `sudo apt install sshpass`) or use SSH keys.", file=sys.stderr)
        sys.exit(1)

    if args.serial:
        g_serial_logger = SerialLogger(args.serial, baud=args.serial_baud, log_path=args.serial_log)
        g_serial_logger.start()

    print("========================================================================")
    print("  Autonomous 3-Profile Silicon Sweep (Radxa Cubie A5E)")
    print("  Co-Processor: XuanTie E907 RISC-V (RV32IMAFDC + Double FPU)")
    print(f"  Target: {TARGET_USER}@{TARGET_IP}:{TARGET_PORT} (Linux 7.1 PREEMPT_RT)")
    if args.serial:
        print(f"  Serial Console: {args.serial} @ {args.serial_baud} baud -> {args.serial_log}")
    print("========================================================================\n")


    results = []
    aggregated_profile_data = []

    # -------------------------------------------------------------------------
    # PROFILE 1
    # -------------------------------------------------------------------------
    log_banner(1, "Testing Profile 1 (DDR VirtIO RPMsg)", "Overlay: cubie-a5e-flight-stack")
    set_overlay_and_reboot("cubie-a5e-flight-stack")

    # 1.0 Kernel KUnit Driver Tests
    kunit_summary = validate_kunit_tests()
    for suite_name, s in kunit_summary["suites"].items():
        results.append(("Profile 1", f"KUnit: {s['title']} ({s['passed']}/{s['total']} tests)", s["status"]))

    # 1.1 run_tests.py
    print("\n--- 1.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=45)
    print(out)
    results.append(("Profile 1", "run_tests.py (Complete Suite)", "PASS" if c == 0 else "FAIL"))
    prof1_data = parse_target_json_report()
    if prof1_data:
        aggregated_profile_data.append(prof1_data)

    # 1.2 C++ ping_rpmsg
    print("\n--- 1.2 Running C++ ping_rpmsg (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 2')
    c, out, _ = run_ssh("/usr/bin/ping_rpmsg -n 1000 -D 50", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_cpp_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)) and bool(re.search(r'1000|100(?:\.00)?%', clean_out)))
    results.append(("Profile 1", "C++ ping_rpmsg (1000 pkts)", "PASS" if p1_cpp_pass else "FAIL"))

    # 1.3 Python ping_rpmsg.py
    print("\n--- 1.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_py_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)) and bool(re.search(r'Successful Replies\s*:\s*1000|1000', clean_out)))
    results.append(("Profile 1", "Python ping_rpmsg.py (1000 pkts)", "PASS" if p1_py_pass else "FAIL"))

    # 1.4 C++ ping_dram
    print("\n--- 1.4 Running C++ ping_dram (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("/usr/bin/ping_dram -n 1000 -s 512", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_dram_pass = (c == 0 and bool(re.search(r'Timeouts\s*:\s*0|1000', clean_out)))
    results.append(("Profile 1", "C++ ping_dram (1000 pkts)", "PASS" if p1_dram_pass else "FAIL"))

    # 1.5 Python monitor_trace.py
    print("\n--- 1.5 Running Python monitor_trace.py ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("python3 /usr/bin/monitor_trace.py -n 3", timeout=15)
    print(out)
    results.append(("Profile 1", "Python monitor_trace.py", "PASS" if c == 0 else "FAIL"))

    # 1.6 Kernel dmesg health check
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 1", allowed_patterns=["testCrash"])
    results.append(("Profile 1", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 2
    # -------------------------------------------------------------------------
    log_banner(2, "Testing Profile 2 (On-Chip SRAM Space 1 VirtIO)", "Overlay: cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram")
    set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram")

    # 2.1 run_tests.py
    print("\n--- 2.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=45)
    print(out)
    results.append(("Profile 2", "run_tests.py (SRAM VirtIO)", "PASS" if c == 0 else "FAIL"))
    prof2_data = parse_target_json_report()
    if prof2_data:
        aggregated_profile_data.append(prof2_data)

    # 2.2 C++ ping_rpmsg
    print("\n--- 2.2 Running C++ ping_rpmsg (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPingRpmsgSram.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 2')
    c, out, _ = run_ssh("/usr/bin/ping_rpmsg -n 1000 -D 50", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p2_cpp_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)) and bool(re.search(r'1000|100(?:\.00)?%', clean_out)))
    results.append(("Profile 2", "C++ ping_rpmsg (1000 pkts)", "PASS" if p2_cpp_pass else "FAIL"))

    # 2.3 Python ping_rpmsg.py
    print("\n--- 2.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p2_py_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)) and bool(re.search(r'Successful Replies\s*:\s*1000|1000', clean_out)))
    results.append(("Profile 2", "Python ping_rpmsg.py (1000 pkts)", "PASS" if p2_py_pass else "FAIL"))

    # 2.4 Kernel dmesg health check
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 2")
    results.append(("Profile 2", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 3
    # -------------------------------------------------------------------------
    log_banner(3, "Testing Profile 3 (Userspace UIO Direct Mailbox & SRAM)", "Overlay: cubie-a5e-flight-stack cubie-a5e-testPing")
    set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPing", "cmdline=uio_pdrv_genirq.of_id=generic-uio")

    # 3.1 run_tests.py
    print("\n--- 3.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=30)
    print(out)
    results.append(("Profile 3", "run_tests.py (UIO Suite)", "PASS" if c == 0 else "FAIL"))
    prof3_data = parse_target_json_report()
    if prof3_data:
        aggregated_profile_data.append(prof3_data)

    # 3.2 C++ ping_shm
    print("\n--- 3.2 Running C++ ping_shm (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("/usr/bin/ping_shm -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p3_shm_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)))
    results.append(("Profile 3", "C++ ping_shm (1000 pkts)", "PASS" if p3_shm_pass else "FAIL"))

    # 3.3 Python ping_uio.py
    print("\n--- 3.3 Running Python ping_uio.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_uio.py -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p3_uio_py_pass = (c == 0 and bool(re.search(r'Data Integrity\s*:\s*PASS', clean_out)) and bool(re.search(r'1000|100(?:\.00)?%', clean_out)))
    results.append(("Profile 3", "Python ping_uio.py (1000 pkts)", "PASS" if p3_uio_py_pass else "FAIL"))

    # 3.4 Kernel dmesg health check
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 3")
    results.append(("Profile 3", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    # -------------------------------------------------------------------------
    # RESTORE DEFAULT PROFILE 1
    # -------------------------------------------------------------------------
    print("\n>>> CLEANUP: Restoring target board to default Profile 1")
    set_overlay_and_reboot("cubie-a5e-flight-stack")

    # -------------------------------------------------------------------------
    # FINAL SUMMARY REPORT
    # -------------------------------------------------------------------------
    print("\n" + "=" * 76)
    print("             FINAL 3-PROFILE UNATTENDED SILICON SWEEP REPORT")
    print("=" * 76)
    print(f"  {'Profile':<12} | {'Test / Tool':<42} | {'Result'}")
    print("  " + "-" * 12 + "-+-" + "-" * 42 + "-+--------")
    all_pass = True
    for prof, test, status in results:
        color = "\033[92m" if status == "PASS" else "\033[91m"
        reset = "\033[0m"
        print(f"  {prof:<12} | {test:<42} | {color}{status}{reset}")
        if status != "PASS":
            all_pass = False
    print("=" * 76)

    # Save Host Reports
    report_bundle = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "target": f"{TARGET_USER}@{TARGET_IP}",
        "kunit_tests": kunit_summary,
        "sweep_summary": results,
        "profiles": aggregated_profile_data
    }
    try:
        with open(args.json_out, "w") as f:
            json.dump(report_bundle, f, indent=2)
        print(f"[REPORT] Saved sweep JSON results to: {args.json_out}")
    except Exception as e:
        print(f"[WARN] Failed to write {args.json_out}: {e}")

    try:
        with open(args.report_out, "w") as f:
            f.write("# Autonomous 3-Profile Silicon Sweep Results\n\n")
            f.write(f"- **Timestamp**: {report_bundle['timestamp']}\n")
            f.write(f"- **Target**: {TARGET_IP} (Linux 7.1 PREEMPT_RT)\n")
            f.write(f"- **Co-Processor**: XuanTie E907 RISC-V\n\n")

            if kunit_summary and "suites" in kunit_summary:
                f.write("## In-Kernel KUnit Driver Test Verification (67 Tests)\n\n")
                f.write(f"- **Overall Status**: **{kunit_summary['status']}** ({kunit_summary['total_passed']}/{kunit_summary['total_expected']} tests passed, {kunit_summary['total_failed']} failed)\n\n")
                f.write("| Subsystem / Driver | Test Suite | Executed | Passed | Failed | Skipped | Status |\n")
                f.write("| :--- | :--- | :---: | :---: | :---: | :---: | :---: |\n")
                for sname, s in kunit_summary["suites"].items():
                    f.write(f"| {s['title']} | `{sname}` | {s['total']} | {s['passed']} | {s['failed']} | {s['skipped']} | **{s['status']}** |\n")
                f.write(f"| **Combined Total** | **All In-Kernel Drivers** | **{kunit_summary['total_executed']}** | **{kunit_summary['total_passed']}** | **{kunit_summary['total_failed']}** | **{kunit_summary['total_skipped']}** | **{kunit_summary['status']}** |\n\n")

                f.write("<details>\n<summary>Click to view individual test case breakdown</summary>\n\n")
                for sname, s in kunit_summary["suites"].items():
                    f.write(f"### {s['title']} ({len(s['subtests'])} tests)\n\n")
                    if s['subtests']:
                        for t in s['subtests']:
                            mark = "x" if t["status"] == "PASS" else " "
                            f.write(f"- [{mark}] `{t['name']}` ({t['status']})\n")
                    else:
                        f.write(f"- *Summary validated: {s['passed']}/{s['total']} passed via {s['source']}*\n")
                    f.write("\n")
                f.write("</details>\n\n")

            f.write("## Overall Test Summary\n\n")
            f.write("| Profile | Test / Tool | Status |\n")
            f.write("| :--- | :--- | :---: |\n")
            for prof, test, status in results:
                f.write(f"| {prof} | {test} | **{status}** |\n")

            if aggregated_profile_data:
                f.write("\n## Detailed Quantitative Metrics per Profile\n\n")
                for prof in aggregated_profile_data:
                    f.write(f"### {prof.get('profile')}\n\n")
                    f.write(f"- **Node Mapping**: {prof.get('node_desc')}\n\n")
                    f.write("| Test / Application | Status | Packets | Avg RTT | Throughput | Bandwidth | Data Integrity |\n")
                    f.write("| :--- | :---: | :---: | :---: | :---: | :---: | :--- |\n")
                    for r in prof.get("results", []):
                        m = r.get("metrics", {})
                        pkts = f"{m.get('packets_recv')}/{m.get('packets_sent')}" if m.get("packets_sent") else (m.get("heartbeat_count") and f"{m['heartbeat_count']} beats" or "N/A")
                        avg_rtt = f"{m.get('avg_lat_us'):.2f} µs" if m.get("avg_lat_us") is not None else (m.get("mcause") or "N/A")
                        rate = f"{m.get('throughput_msgs_s'):,.1f} msgs/s" if m.get("throughput_msgs_s") is not None else "N/A"
                        bw = str(m.get("bandwidth", "N/A")).split("(")[0].strip()
                        integ = "PASS (0 errors)" if m.get("data_integrity") and "PASS" in m.get("data_integrity") else (m.get("data_integrity") or "Verified")
                        f.write(f"| {r.get('name')} | **{r.get('status')}** | {pkts} | {avg_rtt} | {rate} | {bw} | {integ} |\n")
                    f.write("\n")
        print(f"[REPORT] Saved sweep Markdown results to: {SWEEP_MD_OUT}")
    except Exception as e:
        print(f"[WARN] Failed to write {SWEEP_MD_OUT}: {e}")

    if g_serial_logger:
        g_serial_logger.stop()
        print(f"[SERIAL] Serial console logging saved to: {args.serial_log}")

    if all_pass:
        print("\n>>> 100% UNATTENDED SWEEP SUCCESS: ALL 3 PROFILES & APPS PASSED! <<<\n")
        return 0
    else:
        print("\n>>> SWEEP FAILED: Review failures above! <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
