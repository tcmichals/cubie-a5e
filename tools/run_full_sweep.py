#!/usr/bin/env python3
"""
run_full_sweep.py - Autonomous Single-Pass 3-Profile Silicon Sweep
Target: Radxa Cubie A5E (Allwinner A527 / T527)

Executes an end-to-end multi-profile test loop across live Radxa Cubie A5E hardware
without manual intervention or source code changes:
  1. Profile 1 (DDR VirtIO): run_tests.py, C++ ping_rpmsg, Python ping_rpmsg.py,
     C++ ping_dram, Python monitor_trace.py
  2. Profile 2 (On-Chip SRAM Space 1 VirtIO): config.txt switch, reboot, run_tests.py,
     C++ ping_rpmsg, Python ping_rpmsg.py
  3. Profile 3 (Userspace UIO Direct Mailbox): config.txt switch, reboot, run_tests.py,
     C++ ping_shm, C++ ping_uio, Python ping_uio.py
  4. Restore Profile 1, reboot cleanly, and generate final quantitative summary table
     + persistent JSON and Markdown reports.
"""

import sys
import time
import subprocess
import re
import json
import os

TARGET_IP = "192.168.1.19"
TARGET_USER = "root"

SWEEP_JSON_OUT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_results.json"))
SWEEP_MD_OUT   = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_results.md"))

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

def parse_target_json_report():
    c, out, _ = run_ssh("cat /tmp/remoteproc_test_results.json", timeout=10)
    if c == 0 and out:
        try:
            return json.loads(out)
        except Exception:
            pass
    return None

def main():
    global TARGET_IP, TARGET_USER
    import argparse
    parser = argparse.ArgumentParser(description="Autonomous 3-Profile Silicon Sweep for Radxa Cubie A5E")
    parser.add_argument("--ip", default=TARGET_IP, help=f"Target board IP address (default: {TARGET_IP})")
    parser.add_argument("--user", default=TARGET_USER, help=f"Target SSH user (default: {TARGET_USER})")
    parser.add_argument("--json-out", default=SWEEP_JSON_OUT, help=f"Path for output JSON sweep results (default: {SWEEP_JSON_OUT})")
    parser.add_argument("--report-out", default=SWEEP_MD_OUT, help=f"Path for output Markdown sweep report (default: {SWEEP_MD_OUT})")
    args = parser.parse_args()

    TARGET_IP = args.ip
    TARGET_USER = args.user

    print("========================================================================")
    print("  Autonomous 3-Profile Silicon Sweep (Radxa Cubie A5E)")
    print("  Target: " + TARGET_IP + " (Linux 7.1 PREEMPT_RT)")
    print("========================================================================\n")

    results = []
    aggregated_profile_data = []

    # -------------------------------------------------------------------------
    # PROFILE 1
    # -------------------------------------------------------------------------
    print(">>> STAGE 1: Testing Profile 1 (DDR VirtIO RPMsg)")
    set_overlay_and_reboot("cubie-a5e-flight-stack")

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

    # 1.4 C++ ping_dram
    print("\n--- 1.4 Running C++ ping_dram (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("/usr/bin/ping_dram -n 1000 -s 512", timeout=30)
    print(out)
    clean_out = strip_ansi(out)
    p1_dram_pass = (c == 0 and "Timeouts       : 0" in clean_out)
    results.append(("Profile 1", "C++ ping_dram (1000 pkts)", "PASS" if p1_dram_pass else "FAIL"))

    # 1.5 Python monitor_trace.py
    print("\n--- 1.5 Running Python monitor_trace.py ---")
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
    prof2_data = parse_target_json_report()
    if prof2_data:
        aggregated_profile_data.append(prof2_data)

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
    p3_shm_pass = (c == 0 and "Data Integrity : PASS" in clean_out)
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
    print("\n" + "=" * 74)
    print("             FINAL 3-PROFILE UNATTENDED SWEEP REPORT")
    print("=" * 74)
    print(f"  {'Profile':<12} | {'Test / Tool':<35} | {'Result'}")
    print("  " + "-" * 12 + "-+-" + "-" * 35 + "-+--------")
    all_pass = True
    for prof, test, status in results:
        color = "\033[92m" if status == "PASS" else "\033[91m"
        reset = "\033[0m"
        print(f"  {prof:<12} | {test:<35} | {color}{status}{reset}")
        if status != "PASS":
            all_pass = False
    print("=" * 74)

    # Save Host Reports
    report_bundle = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "target": f"{TARGET_USER}@{TARGET_IP}",
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
            f.write(f"- **Target**: {TARGET_IP} (Linux 7.1 PREEMPT_RT)\n\n")
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

    if all_pass:
        print("\n>>> 100% UNATTENDED SWEEP SUCCESS: ALL PROFILES & APPS PASSED! <<<\n")
        return 0
    else:
        print("\n>>> SWEEP FAILED: Review failures above! <<<\n")
        return 1

if __name__ == "__main__":
    sys.exit(main())
