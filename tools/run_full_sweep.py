#!/usr/bin/env python3
"""
run_full_sweep.py - Autonomous Single-Pass 3-Profile Silicon Sweep

Executes an end-to-end multi-profile test loop across live Radxa Cubie A5E hardware
without manual intervention or source code changes:
  1. Profile 1 (DDR VirtIO): run_tests.py, C++ ping_rpmsg, Python ping_rpmsg.py, monitor_trace.py
  2. Profile 2 (On-Chip SRAM Space 1 VirtIO): config.txt switch, reboot, run_tests.py, C++ ping_rpmsg, Python ping_rpmsg.py
  3. Profile 3 (Userspace UIO Direct Mailbox): config.txt switch, reboot, run_tests.py, C++ ping_shm, C++ ping_uio, Python ping_uio.py
  4. Restore Profile 1, reboot cleanly, and generate final summary table.
"""

import sys
import time
import subprocess
import re

TARGET_IP = "192.168.1.19"
TARGET_USER = "root"

def strip_ansi(text: str) -> str:
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)

def run_ssh(cmd, timeout=30):
    ssh_cmd = ["ssh", "-o", "ConnectTimeout=5", "-o", "StrictHostKeyChecking=no",
               f"{TARGET_USER}@{TARGET_IP}", cmd]
    try:
        res = subprocess.run(ssh_cmd, capture_output=True, text=True, timeout=timeout)
        return res.returncode, res.stdout.strip(), res.stderr.strip()
    except subprocess.TimeoutExpired:
        return 1, "", "timeout"

def wait_for_target(max_attempts=30):
    print("  Waiting for target board to come online...", end="", flush=True)
    for _ in range(max_attempts):
        time.sleep(2)
        code, out, _ = run_ssh("cat /sys/class/remoteproc/remoteproc0/state", timeout=5)
        if code == 0 and "running" in out:
            print(" ONLINE!")
            time.sleep(2)
            return True
        print(".", end="", flush=True)
    print(" TIMEOUT!")
    return False

def set_overlay_and_reboot(overlay_str, cmdline_str=None):
    print(f"\n[CONFIG] Setting overlay: '{overlay_str}'")
    run_ssh(f"sed -i 's/^dtoverlay=.*/dtoverlay={overlay_str}/' /boot/config.txt")
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

def main():
    print("========================================================================")
    print("  Autonomous 3-Profile Silicon Sweep (Radxa Cubie A5E)")
    print("  Target: " + TARGET_IP + " (Linux 7.1 PREEMPT_RT)")
    print("========================================================================\n")

    results = []

    # -------------------------------------------------------------------------
    # PROFILE 1
    # -------------------------------------------------------------------------
    print(">>> STAGE 1: Testing Profile 1 (DDR VirtIO RPMsg)")
    # Ensure Profile 1 config
    set_overlay_and_reboot("cubie-a5e-flight-stack")

    # 1.1 run_tests.py
    print("\n--- 1.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=45)
    print(out)
    results.append(("Profile 1", "run_tests.py (5 tests)", "PASS" if c == 0 else "FAIL"))

    # 1.2 C++ ping_rpmsg
    print("\n--- 1.2 Running C++ ping_rpmsg (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPingRpmsg.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 2')
    c, out, _ = run_ssh("/usr/bin/ping_rpmsg -n 1000 -D 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_cpp_pass = (c == 0 and "Data Integrity : PASS" in clean_out and "100.00% success" in clean_out)
    results.append(("Profile 1", "C++ ping_rpmsg (1000 pkts)", "PASS" if p1_cpp_pass else "FAIL"))

    # 1.3 Python ping_rpmsg.py
    print("\n--- 1.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -s 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_py_pass = (c == 0 and "Data Integrity     : PASS" in clean_out and "Successful Replies : 1000" in clean_out)
    results.append(("Profile 1", "Python ping_rpmsg.py (1000 pkts)", "PASS" if p1_py_pass else "FAIL"))

    # 1.4 Python monitor_trace.py
    print("\n--- 1.4 Running Python monitor_trace.py ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("python3 /usr/bin/monitor_trace.py -n 3", timeout=15)
    print(out)
    results.append(("Profile 1", "Python monitor_trace.py", "PASS" if c == 0 else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 2
    # -------------------------------------------------------------------------
    print("\n>>> STAGE 2: Testing Profile 2 (On-Chip SRAM Space 1 VirtIO)")
    set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram")

    # 2.1 run_tests.py
    print("\n--- 2.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=45)
    print(out)
    results.append(("Profile 2", "run_tests.py (SRAM VirtIO)", "PASS" if c == 0 else "FAIL"))

    # 2.2 C++ ping_rpmsg
    print("\n--- 2.2 Running C++ ping_rpmsg (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPingRpmsgSram.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 2')
    c, out, _ = run_ssh("/usr/bin/ping_rpmsg -n 1000 -D 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p2_cpp_pass = (c == 0 and "Data Integrity : PASS" in clean_out and "100.00% success" in clean_out)
    results.append(("Profile 2", "C++ ping_rpmsg (1000 pkts)", "PASS" if p2_cpp_pass else "FAIL"))

    # 2.3 Python ping_rpmsg.py
    print("\n--- 2.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -s 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p2_py_pass = (c == 0 and "Data Integrity     : PASS" in clean_out and "Successful Replies : 1000" in clean_out)
    results.append(("Profile 2", "Python ping_rpmsg.py (1000 pkts)", "PASS" if p2_py_pass else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 3
    # -------------------------------------------------------------------------
    print("\n>>> STAGE 3: Testing Profile 3 (Userspace UIO Direct Mailbox & SRAM)")
    set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPing", "cmdline=uio_pdrv_genirq.of_id=generic-uio")

    # 3.1 run_tests.py
    print("\n--- 3.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=30)
    print(out)
    results.append(("Profile 3", "run_tests.py (testPing)", "PASS" if c == 0 else "FAIL"))

    # 3.2 C++ ping_shm
    print("\n--- 3.2 Running C++ ping_shm (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testPing.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("/usr/bin/ping_shm -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p3_shm_pass = (c == 0 and "Data Integrity : PASS" in clean_out and "100% success" in clean_out)
    results.append(("Profile 3", "C++ ping_shm (1000 pkts)", "PASS" if p3_shm_pass else "FAIL"))

    # 3.3 C++ ping_uio
    print("\n--- 3.3 Running C++ ping_uio (1,000 pings) ---")
    c, out, _ = run_ssh("/usr/bin/ping_uio -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p3_uio_cpp_pass = (c == 0 and "Data Integrity      : PASS" in clean_out and "100.00%" in clean_out)
    results.append(("Profile 3", "C++ ping_uio (1000 pkts)", "PASS" if p3_uio_cpp_pass else "FAIL"))

    # 3.4 Python ping_uio.py
    print("\n--- 3.4 Running Python ping_uio.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_uio.py -n 1000 -d 0", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p3_uio_py_pass = (c == 0 and "Data Integrity      : PASS" in clean_out and "100.00%" in clean_out)
    results.append(("Profile 3", "Python ping_uio.py (1000 pkts)", "PASS" if p3_uio_py_pass else "FAIL"))

    # -------------------------------------------------------------------------
    # RESTORE DEFAULT PROFILE 1
    # -------------------------------------------------------------------------
    print("\n>>> CLEANUP: Restoring target board to default Profile 1")
    set_overlay_and_reboot("cubie-a5e-flight-stack")

    # -------------------------------------------------------------------------
    # FINAL SUMMARY REPORT
    # -------------------------------------------------------------------------
    print("\n" + "=" * 70)
    print("             FINAL 3-PROFILE UNATTENDED SWEEP REPORT")
    print("=" * 70)
    print(f"  {'Profile':<12} | {'Test / Tool':<35} | {'Result'}")
    print("  " + "-" * 12 + "-+-" + "-" * 35 + "-+--------")
    all_pass = True
    for prof, test, status in results:
        color = "\033[92m" if status == "PASS" else "\033[91m"
        reset = "\033[0m"
        print(f"  {prof:<12} | {test:<35} | {color}{status}{reset}")
        if status != "PASS":
            all_pass = False
    print("=" * 70)

    if all_pass:
        print("\n>>> 100% UNATTENDED SWEEP SUCCESS: ALL PROFILES & APPS PASSED! <<<\n")
        return 0
    else:
        print("\n>>> SWEEP FAILED: Review failures above! <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
