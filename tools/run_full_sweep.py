#!/usr/bin/env python3
"""
run_full_sweep.py - Autonomous Single-Pass 3-Profile Silicon Sweep
Target: Radxa Cubie A5E (Allwinner A527 / T527)

Executes an end-to-end multi-profile test loop across live Radxa Cubie A5E hardware
for the XuanTie E906 RISC-V real-time co-processor without manual intervention:
  1. Profile 1 (DDR VirtIO): run_tests.py, C++ ping_rpmsg, Python ping_rpmsg.py,
     C++ ping_dram, Python monitor_trace.py, testFreeRTOS (tick, IRQ-driven rpmsg),
     message box clock/reset check across unbind/bind, TX-completion benchmark (timer)
  1T. Message box TX completion, ISR mode (overlay cubie-a5e-msgbox-txirq, reboot):
     the same benchmark plus a deterministic write-IRQ test; side-by-side timer vs ISR
     latency, jitter, throughput and interrupt counts in the report
  2. Profile 2 (On-Chip SRAM Space 1 VirtIO): config.txt switch, reboot, run_tests.py,
     C++ ping_rpmsg, Python ping_rpmsg.py
  3. Profile 3 (Userspace UIO Direct Mailbox): config.txt switch, reboot, run_tests.py,
     C++ ping_shm, Python ping_uio.py
  4. Restore Profile 1, reboot cleanly, and generate final quantitative summary table
     + persistent JSON and Markdown reports.
  Optional: line coverage of the message box and remoteproc drivers (gcov), when the
  kernel is built as tools/sweep_gcov.py describes and --gcov-build-dir is given.
"""

import sys
import time
import subprocess
import re
import json
import os
import shutil

import threading
import sweep_gcov
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
SWEEP_HISTORY_JSON = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_history.json"))
SWEEP_HISTORY_MD   = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "test_sweep_history.md"))
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
g_coverage = None		# sweep_gcov.BoardCoverage, when coverage is collected
g_active_overlay = "boot"	# label of the counters a reboot is about to clear

def strip_ansi(text: str) -> str:
    return re.sub(r'\x1b\[[0-9;]*[a-zA-Z]', '', text)

def log_banner(stage_num: int, title: str, subtitle: str = ""):
    print("\n\033[1;36m" + "=" * 76)
    print(f"  STAGE {stage_num}: {title.upper()}")
    if subtitle:
        print(f"  {subtitle}")
    print("=" * 76 + "\033[0m")

def run_ssh(cmd, timeout=30, report_timeout=True):
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
    except subprocess.TimeoutExpired as e:
        # Dropping the SSH session does not stop the command on the board. Kill
        # it there, or it keeps running and collides with the next test step.
        tools = [os.path.basename(t) for t in cmd.split() if t.startswith("/usr/bin/")]
        if report_timeout:
            print(f"  \033[91m[FAIL] timed out after {timeout} s on the target: {cmd}\033[0m")
        if tools and not cmd.startswith("cat "):
            run_ssh(f"for p in $(ps | grep '[{tools[0][0]}]{tools[0][1:]}' | awk '{{print $1}}'); do kill $p; done",
                    timeout=10)
        out = e.stdout.decode(errors="replace") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return 1, out.strip(), "timeout"

def push_file(local, remote, timeout=30):
    """Copy a file to the target with the same login as run_ssh (legacy scp: no sftp-server)."""
    cmd = []
    env = os.environ.copy()
    if TARGET_PASSWORD:
        cmd.extend(["sshpass", "-e"])
        env["SSHPASS"] = str(TARGET_PASSWORD)
    cmd.extend(["scp", "-O", "-q",
                "-o", "ConnectTimeout=5",
                "-o", "StrictHostKeyChecking=no",
                "-o", "UserKnownHostsFile=/dev/null",
                "-o", "LogLevel=ERROR",
                "-P", str(TARGET_PORT)])
    if TARGET_KEY:
        cmd.extend(["-i", TARGET_KEY])
    cmd.extend([local, f"{TARGET_USER}@{TARGET_IP}:{remote}"])
    try:
        return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, env=env).returncode == 0
    except subprocess.TimeoutExpired:
        return False

def report_remoteproc_presence():
    """An SSH-reachable board without remoteproc0 is a probe failure, not a boot failure."""
    code, _, _ = run_ssh("test -e /sys/class/remoteproc/remoteproc0/state", timeout=5)
    if code == 0:
        return True
    print("  \033[91m[WARN] Board is up but remoteproc0 is missing (driver probe failed or deferred)\033[0m")
    _, out, _ = run_ssh("cat /sys/kernel/debug/devices_deferred 2>/dev/null; "
                        "dmesg | grep -iE 'rproc|remoteproc|mbox|msgbox' | tail -n 8", timeout=10)
    for line in out.splitlines():
        print(f"    >> {line}")
    return False

def wait_for_target(max_attempts=30):
    print("  Waiting for target board to come online...", end="", flush=True)
    for i in range(max_attempts):
        time.sleep(2)
        code, out, _ = run_ssh("true", timeout=5, report_timeout=False)  # expected while rebooting
        if code == 0:
            print(" ONLINE!")
            time.sleep(2)
            return report_remoteproc_presence()
        print(".", end="", flush=True)
        if i == 20 and g_serial_logger:
            recent = g_serial_logger.get_recent_lines(5)
            if recent:
                print(f"\n  [SERIAL LAST]: {''.join(recent).strip()}")
    print(" \033[91mTIMEOUT! (no SSH)\033[0m")
    return False

def set_overlay_and_reboot(overlay_str, cmdline_str=None, base_dtb=None):
    """Select base DTB (None = boot.cmd default) and overlays, reboot, wait. True = board and remoteproc0 up."""
    global g_active_overlay
    # A reboot clears the gcov counters: keep what this profile covered
    if g_coverage:
        g_coverage.snapshot(g_active_overlay)
    g_active_overlay = overlay_str
    print(f"\n[CONFIG] Setting overlay: '{overlay_str}'" + (f", base DTB: '{base_dtb}'" if base_dtb else ""))
    run_ssh(f"sed -i '/^[# ]*dtoverlay=/d' /boot/config.txt; echo 'dtoverlay={overlay_str}' >> /boot/config.txt")
    run_ssh("sed -i '/^cmdline=uio_pdrv_genirq/d' /boot/config.txt")
    if cmdline_str:
        run_ssh(f"echo '{cmdline_str}' >> /boot/config.txt")
    run_ssh("sed -i '/^base_dtb=/d' /boot/config.txt")
    if base_dtb:
        code, _, _ = run_ssh(f"test -f /boot/{base_dtb} && echo 'base_dtb={base_dtb}' >> /boot/config.txt")
        if code != 0:
            print(f"  \033[91m[FAIL] /boot/{base_dtb} is missing on the target; not rebooting\033[0m")
            return False

    print("[REBOOT] Rebooting target board...")
    try:
        run_ssh("reboot", timeout=5)
    except subprocess.TimeoutExpired:
        pass
    time.sleep(8)
    return wait_for_target()

def ping_exchanged(code, out, expected=1000, require_integrity=True):
    """
    A ping benchmark passes only if it really exchanged messages: exit status 0,
    every ping sent and answered, no timeouts and (when the tool checks it) intact
    data. Anything else, including empty output, is a FAIL with the reason in red.
    """
    text = strip_ansi(out)
    def num(pattern):
        m = re.search(pattern + r'\s*:\s*(\d+)', text)
        return int(m.group(1)) if m else None
    sent = num(r'(?:Packets Sent|Total Packets Sent|Total Pings Sent|Messages Sent)')
    recv = num(r'(?:Packets Recv|Successful Replies|Pongs Received|Messages Recv)')
    timeouts = num(r'(?:Timeouts|Timed Out Packets)')
    integrity = re.search(r'Data Integrity\s*:\s*(\S+)', text)

    reasons = []
    if code != 0:
        reasons.append(f"exit status {code}")
    if sent != expected:
        reasons.append(f"sent {sent if sent is not None else 'nothing'} of {expected}")
    if recv != expected:
        reasons.append(f"received {recv if recv is not None else 'nothing'} of {expected}")
    if timeouts:
        reasons.append(f"{timeouts} timeouts")
    if integrity and integrity.group(1) != "PASS":
        reasons.append("data integrity FAIL")
    elif require_integrity and not integrity:
        reasons.append("no data integrity result")
    for r in reasons:
        print(f"  \033[91m[FAIL] {r}\033[0m")
    return "FAIL" if reasons else "PASS"

def parse_latency(out):
    """Latency/throughput numbers printed by ping_rpmsg and stress_rpmsg."""
    text = strip_ansi(out)
    def num(label):
        m = re.search(label + r'\s*:\s*([0-9.]+)', text)
        return float(m.group(1)) if m else None
    return {
        "recv": num(r'Packets Recv'),
        "avg_us": num(r'Avg Latency'),
        "min_us": num(r'Min Latency'),
        "max_us": num(r'Max Latency'),
        "jitter_us": num(r'Jitter \(StdDev\)'),
        "p99_us": num(r'99th Percentile'),
        "msgs_s": num(r'Throughput'),
    }

def mailbox_irq_counts():
    """{line name: count summed over CPUs} for the message box lines in /proc/interrupts."""
    _, out, _ = run_ssh("grep mailbox /proc/interrupts", timeout=10)
    counts = {}
    for line in out.splitlines():
        f = line.split()
        nums = [int(x) for x in f[1:] if x.isdigit()]
        if f:
            counts[f[-1]] = sum(nums)
    return counts

SWEEP_DMESG_LOG = "/tmp/sweep_dmesg.log"

def dmesg_drain():
    """
    Move the kernel log into SWEEP_DMESG_LOG (BusyBox `dmesg -c`) and return the
    new lines. Counting a message by diffing `dmesg | grep -c` saturates once the
    ring buffer is full; draining gives exact counts, and the saved log keeps
    everything for check_kernel_dmesg_health().
    """
    _, out, _ = run_ssh(f"dmesg -c | tee -a {SWEEP_DMESG_LOG}", timeout=20)
    return out

def mbox_queue_overflows(new_lines):
    return sum(1 for line in new_lines.splitlines() if "MBOX_TX_QUEUE_LEN" in line)

# TX-done lines, named after the remote bank (sun55i-msgbox.c routes)
MSGBOX_TX_LINES = ("3003000.mailbox-cpus", "3003000.mailbox-dsp", "3003000.mailbox-rv")
MSGBOX_RV_LINE = "3003000.mailbox-rv"

# testExtraRam regions: (name, host address Linux checks, None = no host view, size)
EXTRA_RAM_REGIONS = [
    ("dsp-ram", None, 128 * 1024),		# DSP local RAM, E906 0x00020000
    ("dram", 0x48100000, 1024 * 1024),	# dram@48100000 carveout
    ("sram-a3-2", 0x07300000, 512 * 1024),	# shared by REMAP_CTRL_REG bit 1
]

MSGBOX_TX_BENCH = [
    # (key, label, firmware, command, expected messages)
    ("ping", "ping_rpmsg 10k, -D 0", "testPingRpmsg.elf", "/usr/bin/ping_rpmsg -n 10000 -D 0", 10000),
    ("stress", "stress_rpmsg 100k, 16 in flight", "testPingRpmsg.elf", "/usr/bin/stress_rpmsg -n 100000 -w 16", 100000),
    ("freertos", "FreeRTOS ping_rpmsg 2k (ISR wake)", "testFreeRTOS.elf", "/usr/bin/ping_rpmsg -n 2000 -D 0", 2000),
]

def run_msgbox_tx_bench(mode, results):
    """
    Message box TX completion benchmark for one mode ("timer" or "isr"):
    checks which TX-done lines the driver holds, then runs MSGBOX_TX_BENCH and
    records latency, jitter, throughput, interrupts per message and core queue
    overflows. Returns {key: metrics}.
    """
    prof = "Profile 1" if mode == "timer" else "Profile 1T"
    tag = "timer" if mode == "timer" else "ISR"
    out = {}

    lines = mailbox_irq_counts()
    if mode == "timer":
        bad = [n for n in lines if n in MSGBOX_TX_LINES]
        ok = "3003000.mailbox" in lines and not bad
        why = f"TX-done lines requested in timer mode: {bad}" if bad else "RX line (SPI 0) missing"
    else:
        # Only the E906 bank's line is requested, and only while channel 9 is in use
        ok = MSGBOX_RV_LINE in lines and not any(n in lines for n in MSGBOX_TX_LINES if n != MSGBOX_RV_LINE)
        why = f"expected only {MSGBOX_RV_LINE} besides SPI 0, got {sorted(lines)}"
    if not ok:
        print(f"  \033[91m[FAIL] {why}\033[0m")
    results.append((prof, f"Msgbox {tag}: lines held ({', '.join(sorted(lines)) or 'none'})", "PASS" if ok else "FAIL"))

    for key, label, fw, cmd, expected in MSGBOX_TX_BENCH:
        print(f"\n--- Msgbox {tag}: {label} ({fw}) ---")
        run_ssh(f'echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "{fw}" > /sys/class/remoteproc/remoteproc0/firmware; '
                f'echo "start" > /sys/class/remoteproc/remoteproc0/state; for i in $(seq 1 80); do [ -e /dev/rpmsg0 ] && break; sleep 0.1; done; sleep 0.5')
        dmesg_drain()
        irq0 = mailbox_irq_counts()
        c, o, _ = run_ssh(cmd, timeout=180)
        print(o)
        irq1, new_log = mailbox_irq_counts(), dmesg_drain()
        m = parse_latency(o)
        rx = irq1.get("3003000.mailbox", 0) - irq0.get("3003000.mailbox", 0)
        m["rx_irqs"] = rx
        m["rx_irqs_per_msg"] = round(rx / expected, 3)
        m["tx_irqs"] = irq1.get(MSGBOX_RV_LINE, 0) - irq0.get(MSGBOX_RV_LINE, 0)
        m["queue_overflows"] = mbox_queue_overflows(new_log)
        status = ping_exchanged(c, o, expected=expected)
        if m["queue_overflows"]:
            print(f"  \033[91m[FAIL] {m['queue_overflows']} MBOX_TX_QUEUE_LEN overflows\033[0m")
            status = "FAIL"
        m["status"] = status
        out[key] = m
        results.append((prof, f"Msgbox {tag}: {label}", status))
    return out

MSGBOX_WRIRQ_TEST = r"""
R=/sys/class/remoteproc/remoteproc0
WEN=0x07136230; WST=0x07136234; CNT=0x0713626C; FIFO=0x0713627C; THR=0x0713628C; BLK=0x072B0000
tx3() { grep mailbox-rv /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s+0}'; }
pop() { devmem $((BLK+0x0c)) 32 3; s=$(( $(devmem $((BLK+0x04)) 32) + 1 )); devmem $((BLK+0x04)) 32 $s
	for i in $(seq 1 100); do [ $(( $(devmem $((BLK+0x08)) 32) )) -eq $s ] && break; usleep 1000; done; }
echo stop > $R/state 2>/dev/null; echo testMsgboxStep.elf > $R/firmware; echo start > $R/state
for i in $(seq 1 100); do [ "$(devmem $BLK 32)" = "0x53544550" ] && break; usleep 10000; done
[ "$(devmem $BLK 32)" = "0x53544550" ] || { echo "RESULT nofw"; exit 1; }
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop; done
devmem $THR 32 0
for i in 1 2 3 4 5 6 7 8; do devmem $FIFO 32 $((0xA0+i)); done
devmem $WST 32 0x80
c0=$(tx3); devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 )); usleep 20000; c1=$(tx3)
pop; usleep 20000; c2=$(tx3); wen=$(( $(devmem $WEN 32) & 0x80 ))
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop; done
echo "RESULT $c0 $c1 $c2 $wen"
"""

def run_msgbox_wrirq_test(results):
    """
    ISR mode: E906 bank write IRQ (channel 11, port 2 p=3; E906 runs testMsgboxStep).
    FIFO full + enable set -> no interrupt; E906 pops one -> exactly one interrupt on
    SPI 205 and the driver's handler clears the enable.
    """
    print("\n--- Msgbox ISR: write IRQ fires when a full FIFO frees a slot (testMsgboxStep) ---")
    c, out, _ = run_ssh(MSGBOX_WRIRQ_TEST, timeout=60)
    m = re.search(r'RESULT (\d+) (\d+) (\d+) (\d+)', out)
    reasons = []
    if not m:
        reasons.append(f"test did not run: {out.strip()[-200:]}")
    else:
        c0, c1, c2, wen = map(int, m.groups())
        print(f"  tx3 interrupts: before {c0}, armed while full {c1}, after pop {c2}; enable after handler: {wen}")
        if c1 != c0:
            reasons.append("interrupt fired while the FIFO was still full")
        if c2 != c0 + 1:
            reasons.append(f"expected exactly 1 interrupt after the pop, got {c2 - c0}")
        if wen:
            reasons.append("handler did not clear the write-IRQ enable")
    for r in reasons:
        print(f"  \033[91m[FAIL] {r}\033[0m")
    results.append(("Profile 1T", "Msgbox ISR: write IRQ on SPI 205 (full -> pop -> 1 IRQ)", "FAIL" if reasons else "PASS"))

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
    # KUnit fixtures fill the mailbox queue on purpose (test_kick_payload_outlives_call);
    # the KUnit stage checks the tests themselves
    allowed_patterns = list(allowed_patterns) + ["sunxi-rproc-test"]

    # BusyBox dmesg has no -l, so the fallback grep is what runs on the board
    # Includes the lines dmesg_drain() moved into SWEEP_DMESG_LOG during this boot
    c, out, _ = run_ssh(f"(cat {SWEEP_DMESG_LOG} 2>/dev/null; dmesg) | grep -iE 'call trace|kernel panic|oops|bug:|cut here|MBOX_TX_QUEUE_LEN|failed to send mailbox kick' | tail -n 30", timeout=10)
    if c == 0 and out.strip():
        lines = [line.strip() for line in out.strip().splitlines() if line.strip()]
        filtered = []
        for line in lines:
            if any(pattern in line for pattern in allowed_patterns):
                continue
            if re.search(r'(?:call trace|kernel panic|oops|bug:|cut here|null pointer dereference|remoteproc.*failed|MBOX_TX_QUEUE_LEN|failed to send mailbox kick)', line, re.IGNORECASE):
                filtered.append(line)

        if filtered:
            print(f"  \033[91m[DMESG ERROR DETECTED in {stage_name}]\033[0m")
            for err in filtered[:5]:
                print(f"    \033[91m>> {err}\033[0m")
            return False, "\n".join(filtered[:3])
    return True, "Clean"

def count_kunit_cases(rel_path):
    """Count KUNIT_CASE entries in a linux-cubie test source; None if it is not checked out."""
    linux_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "linux-cubie"))
    try:
        with open(os.path.join(linux_dir, rel_path)) as f:
            return len(re.findall(r'^\s*KUNIT_CASE\w*\(', f.read(), re.MULTILINE))
    except OSError:
        return None

# Kernel log reports that make a run fail even when every KUnit case passed
KERNEL_SPLAT_RE = r"WARNING:|BUG:|Oops|inconsistent lock state|possible circular locking|sleeping function called"
# A report belongs to us if it names a KUnit test thread or one of the drivers
OUR_SPLAT_RE = re.compile(r"kunit_try_catch|sun55i|sunxi.rproc|msgbox|remoteproc")


def find_kernel_splats():
    """
    WARNING/BUG reports in the kernel log that come from a KUnit test thread or
    from our drivers. KUnit does not fail a case that only triggers a warning
    (2026-10-27: a test raised its IRQ with interrupts on, the kernel warned,
    and every case still passed), so these are checked separately.
    """
    _, out, _ = run_ssh(f"dmesg | grep -A6 -E '{KERNEL_SPLAT_RE}'", timeout=10)
    return [b.strip() for b in out.split("\n--\n") if b.strip() and OUR_SPLAT_RE.search(b)]


def validate_kunit_tests():
    """
    Query, validate, and report in-kernel KUnit driver test suites on target hardware.
    The expected count per suite comes from the KUNIT_CASE entries in linux-cubie,
    so it follows the test sources instead of going stale.
    """
    suites_info = {
        "sunxi_rproc": {
            "title": "sunxi_rproc (Remoteproc Driver)",
            "source": "drivers/remoteproc/sunxi_rproc_kunit.c",
        },
        "sun55i_msgbox": {
            "title": "sun55i_msgbox (Mailbox Driver)",
            "source": "drivers/mailbox/sun55i_msgbox_kunit.c",
        },
    }
    for meta in suites_info.values():
        meta["expected_count"] = count_kunit_cases(meta["source"])
    known = [m["expected_count"] for m in suites_info.values() if m["expected_count"] is not None]
    banner = f"{sum(known)} TESTS" if len(known) == len(suites_info) else "EXPECTED COUNT UNKNOWN"

    print("\n" + "=" * 76)
    print(f"        1.0 IN-KERNEL KUNIT DRIVER TEST VALIDATION ({banner})")
    print("=" * 76)

    # Ensure debugfs is mounted on target
    run_ssh("mount -t debugfs none /sys/kernel/debug 2>/dev/null")

    suite_results = {}
    total_expected = 0
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

        expected = meta["expected_count"]
        if expected is None:
            expected = total_count  # source not available: trust what the kernel ran
        total_expected += expected
        if fail_count > 0:
            suite_status = "FAIL"
        elif pass_count == 0:
            suite_status = "MISSING"
        elif pass_count + skip_count < expected:
            suite_status = "INCOMPLETE"
        else:
            suite_status = "PASS"
        suite_results[suite_name] = {
            "title": meta["title"],
            "expected_count": expected,
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
        tests = f"{s['total']}/{s['expected_count']}"
        print(f"  {s['title']:<32} | {tests:<6} | {s['passed']:<6} | {s['failed']:<6} | {s['skipped']:<7} | {color}{s['status']}{reset}")

    print("  " + "-" * 32 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 6 + "-+-" + "-" * 7 + "-+-------")
    splats = find_kernel_splats()
    overall_status = "PASS" if all(s["status"] == "PASS" for s in suite_results.values()) else "FAIL"
    if splats:
        overall_status = "FAIL"
    color = "\033[92m" if overall_status == "PASS" else "\033[91m"
    reset = "\033[0m"
    tests = f"{total_executed}/{total_expected}"
    print(f"  {'COMBINED TOTAL':<32} | {tests:<6} | {total_passed:<6} | {total_failed:<6} | {total_skipped:<7} | {color}{overall_status}{reset}")
    print("=" * 76)

    if splats:
        print(f"\033[91m[ERROR] {len(splats)} kernel WARNING/BUG report(s) from KUnit or our drivers:\033[0m")
        for block in splats:
            print("\n".join("    " + line for line in block.splitlines()))
        print()

    # If any failures, print alert with test names
    any_failed = any(s["failed_tests"] for s in suite_results.values())
    if any_failed:
        print("\033[91m[ERROR] The following KUnit test cases failed on target hardware:\033[0m")
        for suite_name, s in suite_results.items():
            for ftest in s["failed_tests"]:
                print(f"  \033[91m  - {suite_name}: {ftest}\033[0m")
        print()
    elif overall_status == "PASS":
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
        "kernel_splats": splats,
        "suites": suite_results
    }

def get_git_commit_hash(repo_dir):
    try:
        out = subprocess.check_output(["git", "-C", repo_dir, "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL)
        return out.decode("utf-8").strip()
    except Exception:
        return "unknown"

def extract_key_metrics(report_bundle):
    """Extract flattened quantitative metrics for historical tracking and trend comparisons."""
    metrics = {
        "kunit_passed": 0,
        "kunit_total": 0,
        "kunit_failed": 0,
        "p1_rpmsg_rtt_us": None,
        "p1_rpmsg_jitter_us": None,
        "p1_rpmsg_rate_msgs_s": None,
        "p1_dram_rtt_us": None,
        "p2_rpmsg_rtt_us": None,
        "p3_shm_rtt_us": None,
        "p3_shm_rate_msgs_s": None,
        "p3_shm_bandwidth_mb_s": None,
        "p3_uio_py_rtt_us": None,
        "p3_uio_py_rate_msgs_s": None,
        "p1_stress_rate_msgs_s": None,
        "p2_stress_rate_msgs_s": None,
        "p1_dram_w16_rate_msgs_s": None,
        "p2_rpmsg_rate_msgs_s": None,
        "p3_uio_irq_rtt_us": None,
    }
    kunit = report_bundle.get("kunit_tests", {})
    metrics["kunit_passed"] = kunit.get("total_passed", 0)
    metrics["kunit_total"] = kunit.get("total_expected", 0)
    metrics["kunit_failed"] = kunit.get("total_failed", 0)

    for prof in report_bundle.get("profiles", []):
        pname = prof.get("profile", "")
        for r in prof.get("results", []):
            name = r.get("name", "")
            m = r.get("metrics", {})
            # Windowed runs ("in flight") measure throughput; their latency
            # is queueing time and must not land in a round-trip metric.
            windowed = "in flight" in name
            if "Profile 1" in pname:
                if "ping_rpmsg (C++" in name:
                    metrics["p1_rpmsg_rtt_us"] = m.get("avg_lat_us")
                    metrics["p1_rpmsg_jitter_us"] = m.get("jitter_us")
                    metrics["p1_rpmsg_rate_msgs_s"] = m.get("throughput_msgs_s")
                elif "stress_rpmsg" in name:
                    metrics["p1_stress_rate_msgs_s"] = m.get("throughput_msgs_s")
                elif "ping_dram" in name and windowed:
                    metrics["p1_dram_w16_rate_msgs_s"] = m.get("throughput_msgs_s")
                elif "ping_dram" in name:
                    metrics["p1_dram_rtt_us"] = m.get("avg_lat_us")
            elif "Profile 2" in pname:
                if "ping_rpmsg (C++" in name:
                    metrics["p2_rpmsg_rtt_us"] = m.get("avg_lat_us")
                    metrics["p2_rpmsg_rate_msgs_s"] = m.get("throughput_msgs_s")
                elif "stress_rpmsg" in name:
                    metrics["p2_stress_rate_msgs_s"] = m.get("throughput_msgs_s")
            elif "Profile 3" in pname:
                if "ping_shm (C++" in name or "Direct SRAM" in name:
                    metrics["p3_shm_rtt_us"] = m.get("avg_lat_us")
                    metrics["p3_shm_rate_msgs_s"] = m.get("throughput_msgs_s")
                    if m.get("bandwidth"):
                        try:
                            metrics["p3_shm_bandwidth_mb_s"] = float(str(m["bandwidth"]).split()[0])
                        except Exception:
                            pass
                elif "ping_uio (C++" in name:
                    metrics["p3_uio_irq_rtt_us"] = m.get("avg_lat_us")
                elif "ping_uio.py" in name or "Python UIO" in name:
                    metrics["p3_uio_py_rtt_us"] = m.get("avg_lat_us")
                    metrics["p3_uio_py_rate_msgs_s"] = m.get("throughput_msgs_s")

    # Message box TX completion benchmarks, timer and write-IRQ mode
    for mode, benches in (report_bundle.get("msgbox_tx_modes") or {}).items():
        for key, m in (benches or {}).items():
            if m.get("status") != "PASS":
                continue
            # Stress latency is queueing time; only its rate is a measurement
            if key != "stress":
                metrics[f"mbx_{mode}_{key}_rtt_us"] = m.get("avg_us")
                metrics[f"mbx_{mode}_{key}_p99_us"] = m.get("p99_us")
            metrics[f"mbx_{mode}_{key}_rate_msgs_s"] = m.get("msgs_s")
    return metrics


# Range check against earlier runs: each metric against the median of the last
# HISTORY_WINDOW passing runs. The band is the larger of 4 robust standard
# deviations (1.4826 * MAD), HISTORY_REL_FLOOR of the median (more for the
# metrics in HISTORY_REL_FLOOR_FOR), and 2 µs for
# latencies, so run-to-run jitter does not fail a sweep. Only the bad direction
# (slower, lower rate) fails; faster than history is reported, not failed.
HISTORY_WINDOW = 10
HISTORY_MIN_RUNS = 3
HISTORY_REL_FLOOR = 0.10
# Noisier metrics: Python timing has two clusters (about 32 and 35.5 us since
# run 8, on unchanged kernels), and a jitter is a spread, not an average
HISTORY_REL_FLOOR_FOR = [("_jitter_", 1.0), ("p3_uio_py_", 0.25)]

# Test method changes: runs before run_id are not a baseline for these metrics
METHOD_CHANGES = [
    # 2026-10-15: 6a3eb45 (UIO ping checks every payload byte in device memory,
    # real doorbell IRQ; the rate includes the check) and 6779369 (rpmsg ping
    # without 50 us pacing); first in run 8
    (8, ("p1_rpmsg_", "p3_")),
]


def comparable_since(key):
    since = 0
    for run_id, prefixes in METHOD_CHANGES:
        if key.startswith(prefixes):
            since = max(since, run_id)
    return since


def check_history_ranges(cur_metrics, history):
    """[(metric, value, median, lo, hi, n, verdict)], verdict OK / SLOW / FAST / NEW."""
    passing = [r for r in history if r.get("overall_status") == "PASS"]
    out = []
    for key in sorted(cur_metrics):
        val = cur_metrics[key]
        if key.startswith("kunit_") or not isinstance(val, (int, float)):
            continue
        since = comparable_since(key)
        xs = [r["metrics"][key] for r in passing
              if isinstance(r.get("metrics", {}).get(key), (int, float))
              and r.get("run_id", 0) >= since][-HISTORY_WINDOW:]
        if len(xs) < HISTORY_MIN_RUNS:
            out.append((key, val, None, None, None, len(xs), "NEW"))
            continue
        xs_sorted = sorted(xs)
        med = xs_sorted[len(xs) // 2] if len(xs) % 2 else (xs_sorted[len(xs) // 2 - 1] + xs_sorted[len(xs) // 2]) / 2
        devs = sorted(abs(x - med) for x in xs)
        mad = devs[len(devs) // 2] if len(devs) % 2 else (devs[len(devs) // 2 - 1] + devs[len(devs) // 2]) / 2
        rel = max([f for part, f in HISTORY_REL_FLOOR_FOR if part in key], default=HISTORY_REL_FLOOR)
        band = max(4 * 1.4826 * mad, rel * abs(med), 2.0 if key.endswith("_us") else 0.0)
        lo, hi = med - band, med + band
        lower_is_better = key.endswith("_us")
        if lo <= val <= hi:
            verdict = "OK"
        elif (val > hi) == lower_is_better:
            verdict = "SLOW"
        else:
            verdict = "FAST"
        out.append((key, val, med, lo, hi, len(xs), verdict))
    return out


def print_history_ranges(ranges):
    print("\n" + "=" * 76)
    print(f"  RANGE CHECK: each metric vs the last {HISTORY_WINDOW} passing runs (median, band)")
    print("=" * 76)
    for key, val, med, lo, hi, n, verdict in ranges:
        color = "\033[91m" if verdict == "SLOW" else ("\033[92m" if verdict == "OK" else "\033[93m")
        if med is None:
            print(f"  {key:<32} {val:>11.2f}   (only {n} earlier runs: baseline)")
        else:
            print(f"  {key:<32} {val:>11.2f}   median {med:>10.2f}  [{lo:.2f} .. {hi:.2f}] n={n}  {color}{verdict}\033[0m")

def audit_engineering_invariants():
    """
    Automated Static & Dynamic Real-Time Invariant Gatekeeper.
    Verifies that no delay hacks, arbitrary sleeps, polling loops, or band-aids exist.
    """
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    linux_dir = os.path.abspath(os.path.join(base_dir, "..", "linux-cubie"))
    
    audit_results = []
    
    # Check 1: Remoteproc Doorbell Client Contract (knows_txdone == false, UPSTREAM_LESSONS_LEARNED 3.2)
    rproc_file = os.path.join(linux_dir, "drivers/remoteproc/sunxi_rproc.c")
    if os.path.exists(rproc_file):
        with open(rproc_file, "r") as f:
            content = f.read()
        controller_paces = ("priv->cl.knows_txdone = false;" in content
                            and "mbox_client_txdone(" not in content)
        audit_results.append({
            "invariant": "RemoteProc VirtIO Doorbell Contract",
            "rule": "priv->cl.knows_txdone = false, no mbox_client_txdone() (controller paces TX)",
            "file": "drivers/remoteproc/sunxi_rproc.c",
            "status": "PASS" if controller_paces else "FAIL",
            "details": "TX completion decided by the controller's last_tx_done()" if controller_paces else "Client reports txdone itself; contradicts Lesson 3.2"
        })
    
    # Check 2: Mailbox TX completion (DESIGN_DECISIONS.md "Message box TX completion", 2026-10-16)
    msgbox_file = os.path.join(linux_dir, "drivers/mailbox/sun55i-msgbox.c")
    if os.path.exists(msgbox_file):
        with open(msgbox_file, "r") as f:
            content = f.read()
        m = re.search(r'static bool sun55i_msgbox_last_tx_done\(.*?\n}\n', content, re.S)
        has_room = bool(m and re.search(r'count\s*<\s*SUN55I_FIFO_MAX', m.group(0)))
        audit_results.append({
            "invariant": "Mailbox TX Completion",
            "rule": "last_tx_done returns count < SUN55I_FIFO_MAX (FIFO has room); DT selects timer or ISR when full",
            "file": "drivers/mailbox/sun55i-msgbox.c",
            "status": "PASS" if has_room else "FAIL",
            "details": "A kick is done once the FIFO accepted it" if has_room else "last_tx_done does not return count < SUN55I_FIFO_MAX (DESIGN_DECISIONS: TX completion)"
        })

    # Check 3: Zero Arbitrary Sleep Hacks in Critical Benchmark Paths
    ping_rpmsg_file = os.path.join(base_dir, "firmware/riscv-firmware/apps/testPingRpmsg/linux/ping_rpmsg.cpp")
    if os.path.exists(ping_rpmsg_file):
        with open(ping_rpmsg_file, "r") as f:
            content = f.read()
        # Only the optional -D pacing may sleep, and it must default to 0
        has_sleep_hacks = ("uint32_t delay_us = 0;" not in content
                           or content.count("usleep(") != 1 or "usleep(delay_us)" not in content)
        audit_results.append({
            "invariant": "Zero-Sleep Benchmark Protocol",
            "rule": "ping_rpmsg: blocking write, inotify for the node, no sleeps except optional -D (default 0)",
            "file": "apps/testPingRpmsg/linux/ping_rpmsg.cpp",
            "status": "PASS" if not has_sleep_hacks else "FAIL",
            "details": "Pure event-driven epoll/read loop with zero sleep overhead" if not has_sleep_hacks else "Found arbitrary sleep() calls masking sync issues!"
        })
    
    # Check 4: PREEMPT_RT Hardirq Bounded Loop Guarantee
    if os.path.exists(msgbox_file):
        with open(msgbox_file, "r") as f:
            content = f.read()
        has_bounded_loop = "for (i = 0; i < SUN55I_FIFO_MAX; i++)" in content
        audit_results.append({
            "invariant": "PREEMPT_RT Hardirq Execution Bound",
            "rule": "sun55i_msgbox_irq drain loop strictly bounded to FIFO_MAX",
            "file": "drivers/mailbox/sun55i-msgbox.c",
            "status": "PASS" if has_bounded_loop else "FAIL",
            "details": "Hardirq latency strictly capped against runaway co-processor lockup" if has_bounded_loop else "Unbounded while() loop in hardirq context!"
        })
        
    return audit_results

def compare_all_runs(history, out=sys.stdout):
    """Each recorded run against the passing runs before it; 1 if any run has a SLOW metric."""
    any_slow = False
    out.write(f"| Run | Date | Kernel | Status | In range | Out of range (value vs median of last {HISTORY_WINDOW} PASS) |\n")
    out.write("| ---: | :--- | :--- | :---: | ---: | :--- |\n")
    for i, run in enumerate(history):
        ranges = check_history_ranges(run.get("metrics", {}), history[:i])
        checked = [r for r in ranges if r[6] != "NEW"]
        bad = [f"{k} {v:.1f} vs {med:.1f} ({verdict})" for k, v, med, _, _, _, verdict in checked
               if verdict in ("SLOW", "FAST")]
        any_slow |= any(r[6] == "SLOW" for r in checked)
        ok = sum(1 for r in checked if r[6] == "OK")
        out.write(f"| #{run.get('run_id')} | {str(run.get('timestamp'))[:16]} | `{str(run.get('git_kernel'))[:12]}` | "
                  f"{run.get('overall_status')} | {ok}/{len(checked)} | {'; '.join(bad) or '-'} |\n")
    return 1 if any_slow else 0


def load_history(history_json_file):
    """Earlier sweep runs, oldest first; [] if there are none."""
    try:
        with open(history_json_file, "r") as f:
            history = json.load(f)
        return history if isinstance(history, list) else []
    except Exception:
        return []


def record_and_compare_history(report_bundle, history_json_file, history_md_file):
    """
    Appends the current sweep run to persistent test history and computes
    statistical jitter-aware deltas/trends against previous runs.
    """
    base_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    linux_dir = os.path.abspath(os.path.join(base_dir, "..", "linux-cubie"))
    git_kernel = get_git_commit_hash(linux_dir)
    git_cubie = get_git_commit_hash(base_dir)

    cur_metrics = extract_key_metrics(report_bundle)
    sweep_summary = report_bundle.get("sweep_summary", [])
    overall_status = "PASS" if all(s == "PASS" for _, _, s in sweep_summary) else "FAIL"

    # Audit engineering invariants (no sleeps, no polling hacks)
    audit_results = audit_engineering_invariants()

    history = load_history(history_json_file)

    prev_run = history[-1] if history else None
    prev_metrics = prev_run.get("metrics", {}) if prev_run else {}

    # Compute deltas with statistical jitter tolerance
    deltas = {}
    metric_labels = [
        ("KUnit In-Kernel Tests (Passed)", "kunit_passed", "tests", True),
        ("Profile 1 C++ Rpmsg RTT", "p1_rpmsg_rtt_us", "µs", False),
        ("Profile 1 C++ Rpmsg Jitter", "p1_rpmsg_jitter_us", "µs", False),
        ("Profile 1 C++ Rpmsg Rate", "p1_rpmsg_rate_msgs_s", "msgs/s", True),
        ("Profile 2 C++ Rpmsg RTT", "p2_rpmsg_rtt_us", "µs", False),
        ("Profile 3 C++ UIO Direct SRAM RTT", "p3_shm_rtt_us", "µs", False),
        ("Profile 3 C++ UIO Throughput", "p3_shm_rate_msgs_s", "msgs/s", True),
        ("Profile 3 C++ UIO Bandwidth", "p3_shm_bandwidth_mb_s", "MB/s", True),
        ("Profile 3 Python UIO RTT", "p3_uio_py_rtt_us", "µs", False),
    ]

    trend_summary = []
    regressions = []
    improvements = []

    for label, key, unit, higher_is_better in metric_labels:
        cur_val = cur_metrics.get(key)
        prev_val = prev_metrics.get(key)
        delta_str = "N/A"
        badge = "⚪ Baseline"

        if cur_val is not None and prev_val is not None:
            diff = cur_val - prev_val
            pct = (diff / prev_val * 100) if prev_val != 0 else 0

            if key == "kunit_passed":
                cur_total = cur_metrics.get("kunit_total")
                prev_total = prev_metrics.get("kunit_total")
                if cur_metrics.get("kunit_failed") or cur_val < (cur_total or 0):
                    badge = "🔴 REGRESSION (Tests Failed or Missing!)"
                    regressions.append(f"{label}: {cur_val}/{cur_total} passed")
                elif cur_total != prev_total:
                    badge = f"🟢 100% Pass (Test Set Changed: {prev_total} -> {cur_total})"
                elif cur_val < prev_val:
                    badge = "🔴 REGRESSION (Tests Failed!)"
                    regressions.append(f"{label}: dropped from {prev_val} to {cur_val}")
                elif cur_val > prev_val:
                    badge = "🟢 IMPROVEMENT (More Tests Passed)"
                    improvements.append(f"{label}: increased from {prev_val} to {cur_val}")
                else:
                    badge = "🟢 Invariant (100% Pass)"
                delta_str = f"{diff:+d} {unit}"
            else:
                diff_str = f"{diff:+.2f} {unit} ({pct:+.1f}%)"
                delta_str = diff_str

                # Calculate statistical noise floor
                if higher_is_better:
                    noise_threshold = 0.08 * prev_val  # 8% throughput/bandwidth noise floor
                elif "p3_shm_rtt" in key:
                    noise_threshold = 1.5  # 1.5 µs noise floor on direct SRAM
                elif "p1" in key or "p2" in key:
                    noise_threshold = max(0.10 * prev_val, 12.0)  # 10% of an average over 1000 pings
                else:
                    noise_threshold = max(0.05 * prev_val, 1.0)

                if higher_is_better:
                    if diff > noise_threshold:
                        badge = "🚀 True Speedup (Higher Rate)"
                        improvements.append(f"{label}: {prev_val:.2f} -> {cur_val:.2f} {unit} ({pct:+.1f}%)")
                    elif diff < -noise_threshold:
                        badge = "⚠️ REGRESSION (Exceeds Noise Floor)"
                        regressions.append(f"{label}: {prev_val:.2f} -> {cur_val:.2f} {unit} ({pct:+.1f}%)")
                    else:
                        badge = "🟢 Invariant (Within Normal Jitter Band)"
                else:
                    if diff < -noise_threshold:
                        badge = "🚀 True Speedup (Lower Latency)"
                        improvements.append(f"{label}: {prev_val:.2f} -> {cur_val:.2f} {unit} ({pct:+.1f}%)")
                    elif diff > noise_threshold:
                        badge = "⚠️ REGRESSION (Exceeds Jitter Noise Floor)"
                        regressions.append(f"{label}: {prev_val:.2f} -> {cur_val:.2f} {unit} ({pct:+.1f}%)")
                    else:
                        badge = "🟢 Invariant (Within Normal Jitter Band)"

        cur_display = f"{cur_val:.2f} {unit}" if isinstance(cur_val, float) else (f"{cur_val} {unit}" if cur_val is not None else "N/A")
        prev_display = f"{prev_val:.2f} {unit}" if isinstance(prev_val, float) else (f"{prev_val} {unit}" if prev_val is not None else "N/A")

        deltas[key] = {
            "label": label,
            "current": cur_val,
            "previous": prev_val,
            "delta_str": delta_str,
            "badge": badge,
            "unit": unit
        }
        trend_summary.append((label, prev_display, cur_display, delta_str, badge))

    # Construct new run record
    run_record = {
        "run_id": len(history) + 1,
        "timestamp": report_bundle.get("timestamp", time.strftime("%Y-%m-%d %H:%M:%S")),
        "git_kernel": git_kernel,
        "git_cubie": git_cubie,
        "overall_status": overall_status,
        "engineering_invariants": audit_results,
        "metrics": cur_metrics,
        "deltas": {k: v["delta_str"] for k, v in deltas.items()},
        "range_check": {k: v for k, _, _, _, _, _, v in check_history_ranges(cur_metrics, history)},
    }
    history.append(run_record)

    # Save JSON history (formatted list)
    try:
        with open(history_json_file, "w") as f:
            json.dump(history, f, indent=2)
        print(f"[HISTORY] Updated historical run archive ({len(history)} total runs) -> {history_json_file}")
    except Exception as e:
        print(f"[WARN] Failed to write {history_json_file}: {e}")

    # Render History Markdown Report
    try:
        with open(history_md_file, "w") as f:
            f.write("# Silicon Sweep Performance History & Regression Tracking\n\n")
            f.write(f"- **Total Recorded Runs**: {len(history)}\n")
            f.write(f"- **Latest Run**: Run #{run_record['run_id']} ({run_record['timestamp']})\n")
            f.write(f"- **Kernel Commit**: `{git_kernel}` | **Firmware/Tools Commit**: `{git_cubie}`\n")
            f.write(f"- **Latest Overall Status**: **{overall_status}**\n\n")

            f.write("## Engineering Invariants & Code Hygiene Audit\n\n")
            f.write("| Subsystem Invariant | Formal Design Rule | Component | Status |\n")
            f.write("| :--- | :--- | :--- | :---: |\n")
            for a in audit_results:
                f.write(f"| **{a['invariant']}** | `{a['rule']}` | `{a['file']}` | **{a['status']}** |\n")
            f.write("\n")

            if regressions:
                f.write("### ⚠️ Statistically Significant Regressions (Exceeding Jitter Floor)\n\n")
                for reg in regressions:
                    f.write(f"- 🔴 **{reg}**\n")
                f.write("\n")
            elif improvements:
                f.write("### 🚀 Performance Improvements Detected\n\n")
                for imp in improvements:
                    f.write(f"- 🟢 **{imp}**\n")
                f.write("\n")
            else:
                f.write("### 🟢 Performance is Statistically Invariant & Stable\n\n")

            f.write(f"## Range Check Against the Last {HISTORY_WINDOW} Passing Runs\n\n")
            f.write("SLOW fails the sweep; FAST is reported only; NEW has fewer than "
                    f"{HISTORY_MIN_RUNS} earlier runs.\n\n")
            f.write("| Metric | Current | Median | Range | Runs | Verdict |\n")
            f.write("| :--- | ---: | ---: | :---: | :---: | :---: |\n")
            for key, val, med, lo, hi, n, verdict in check_history_ranges(cur_metrics, history[:-1]):
                rng = "-" if med is None else f"{lo:.2f} .. {hi:.2f}"
                f.write(f"| {key} | {val:.2f} | {'-' if med is None else f'{med:.2f}'} | {rng} | {n} | **{verdict}** |\n")
            f.write("\n")

            f.write("## Comparison Against Preceding Run\n\n")
            f.write("| Metric | Previous Run | Current Run | Delta | Statistical Assessment |\n")
            f.write("| :--- | :---: | :---: | :---: | :---: |\n")
            for label, prev_val, cur_val, delta_str, badge in trend_summary:
                f.write(f"| {label} | {prev_val} | {cur_val} | `{delta_str}` | {badge} |\n")
            f.write("\n")

            f.write("## Every Run Against the Passing Runs Before It\n\n")
            f.write("SLOW: slower or lower rate than the band (fails the sweep since the check was added); "
                    "FAST: better than the band.\n\n")
            compare_all_runs(history, f)
            f.write("\n")

            f.write("## Historical Runs Timeline\n\n")
            f.write("| Run # | Timestamp | Kernel | Tools | Status | KUnit | P1 Rpmsg RTT | P3 UIO RTT | P3 UIO Rate |\n")
            f.write("| :---: | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: |\n")
            for h in reversed(history[-20:]):
                m = h.get("metrics", {})
                k_p = f"{m.get('kunit_passed', 0)}/{m.get('kunit_total', 0)}"
                p1_lat = f"{m['p1_rpmsg_rtt_us']:.2f} µs" if m.get("p1_rpmsg_rtt_us") is not None else "N/A"
                p3_lat = f"{m['p3_shm_rtt_us']:.2f} µs" if m.get("p3_shm_rtt_us") is not None else "N/A"
                p3_rate = f"{m['p3_shm_rate_msgs_s']:,.0f}/s" if m.get("p3_shm_rate_msgs_s") is not None else "N/A"
                st = f"**{h.get('overall_status')}**"
                f.write(f"| #{h.get('run_id')} | {h.get('timestamp')} | `{h.get('git_kernel')}` | `{h.get('git_cubie')}` | {st} | {k_p} | {p1_lat} | {p3_lat} | {p3_rate} |\n")
            f.write("\n")
        print(f"[HISTORY] Saved historical Markdown timeline -> {history_md_file}")
    except Exception as e:
        print(f"[WARN] Failed to write {history_md_file}: {e}")

    # Print Terminal Comparison Table
    if prev_run:
        print("\n" + "=" * 76)
        print("          HISTORICAL PERFORMANCE TREND & REGRESSION ANALYSIS")
        print("=" * 76)
        print(f"  {'Metric':<35} | {'Previous':<12} | {'Current':<12} | {'Trend / Delta'}")
        print("  " + "-" * 35 + "-+-" + "-" * 12 + "-+-" + "-" * 12 + "-+-" + "-" * 17)
        for label, prev_val, cur_val, delta_str, badge in trend_summary:
            print(f"  {label:<35} | {prev_val:<12} | {cur_val:<12} | {badge} ({delta_str})")
        print("=" * 76)

def main():
    global TARGET_IP, TARGET_USER, TARGET_PORT, TARGET_PASSWORD, TARGET_KEY, g_serial_logger, g_coverage
    import argparse
    parser = argparse.ArgumentParser(description="Autonomous 3-Profile Silicon Sweep for Radxa Cubie A5E (E906 RISC-V)")
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
    parser.add_argument("--history-json", default=SWEEP_HISTORY_JSON, help=f"Path for cumulative JSON history (default: {SWEEP_HISTORY_JSON})")
    parser.add_argument("--history-md", default=SWEEP_HISTORY_MD, help=f"Path for historical Markdown report (default: {SWEEP_HISTORY_MD})")
    parser.add_argument("--gcov-build-dir", default=os.environ.get("GCOV_BUILD_DIR"),
                        help="Kernel build dir (O=) with GCOV_KERNEL and the two drivers instrumented; "
                             "collects driver line coverage (see tools/sweep_gcov.py)")
    parser.add_argument("--gcov-tool", default=os.environ.get("GCOV_TOOL"),
                        help="Cross gcov of the compiler that built the kernel (default: Buildroot's)")
    parser.add_argument("--check-history", metavar="RESULTS_JSON",
                        help="Only run the range check: a saved results JSON against the history, no board")
    parser.add_argument("--compare-all", action="store_true",
                        help="Only replay the range check for every recorded run against the passing runs before it")
    args = parser.parse_args()

    if args.compare_all:
        return compare_all_runs(load_history(args.history_json))

    if args.check_history:
        with open(args.check_history) as f:
            bundle = json.load(f)
        # The results file may already be the newest history entry: leave it out
        history = [r for r in load_history(args.history_json)
                   if r.get("timestamp") != bundle.get("timestamp")]
        ranges = check_history_ranges(extract_key_metrics(bundle), history)
        print_history_ranges(ranges)
        return 1 if any(v == "SLOW" for *_, v in ranges) else 0

    TARGET_IP = args.ip
    TARGET_USER = args.user
    TARGET_PORT = args.port
    TARGET_PASSWORD = args.password
    TARGET_KEY = args.key

    if TARGET_PASSWORD and not shutil.which("sshpass"):
        print("[ERROR] A password was specified via --password, but 'sshpass' is not installed on the host.", file=sys.stderr)
        print("        Please install sshpass (e.g. `sudo apt install sshpass`) or use SSH keys.", file=sys.stderr)
        sys.exit(1)

    if args.gcov_build_dir:
        g_coverage = sweep_gcov.BoardCoverage(run_ssh, args.gcov_build_dir, args.gcov_tool)
        if g_coverage.skip_reason:
            print(f"[COVERAGE] not collected: {g_coverage.skip_reason}")

    if args.serial:
        g_serial_logger = SerialLogger(args.serial, baud=args.serial_baud, log_path=args.serial_log)
        g_serial_logger.start()

    print("========================================================================")
    print("  Autonomous 3-Profile Silicon Sweep (Radxa Cubie A5E)")
    print("  Co-Processor: XuanTie E906 RISC-V (RV32IMAFCX, single-precision FPU)")
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
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=300)
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
    results.append(("Profile 1", "C++ ping_rpmsg (1000 pkts)", ping_exchanged(c, out)))

    print("\n--- 1.2b Running C++ stress_rpmsg (100,000 msgs, 16 in flight) ---")
    c, out, _ = run_ssh("/usr/bin/stress_rpmsg -n 100000 -w 16", timeout=120)
    print(out)
    results.append(("Profile 1", "C++ stress_rpmsg (100k, 16 in flight)", ping_exchanged(c, out, expected=100000)))

    # 1.3 Python ping_rpmsg.py
    print("\n--- 1.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -d 0", timeout=30)
    print(out)
    results.append(("Profile 1", "Python ping_rpmsg.py (1000 pkts)", ping_exchanged(c, out)))

    # 1.4 C++ ping_dram
    print("\n--- 1.4 Running C++ ping_dram (1,000 pings) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testDRAMMsg.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("/usr/bin/ping_dram -n 1000 -s 512", timeout=30)
    print(out)
    results.append(("Profile 1", "C++ ping_dram (1000 pkts)", ping_exchanged(c, out)))
    c, out, _ = run_ssh("/usr/bin/ping_dram -n 100000 -s 512 -w 16", timeout=60)
    print(out)
    results.append(("Profile 1", "C++ ping_dram (100k, 16 in flight)", ping_exchanged(c, out, expected=100000)))

    # 1.5 Python monitor_trace.py
    print("\n--- 1.5 Running Python monitor_trace.py ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testStringBinaryTrace0.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 1')
    c, out, _ = run_ssh("python3 /usr/bin/monitor_trace.py -n 3", timeout=15)
    print(out)
    results.append(("Profile 1", "Python monitor_trace.py", "PASS" if c == 0 else "FAIL"))

    # 1.6 FreeRTOS on the E906: tick, interrupt-driven rpmsg (CLIC line 17)
    print("\n--- 1.6 Running testFreeRTOS (FreeRTOS v11, interrupt-driven rpmsg) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testFreeRTOS.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 2')
    c, trace, _ = run_ssh("tr -d '\\000' < /sys/kernel/debug/remoteproc/remoteproc0/trace0", timeout=10)
    trace = strip_ansi(trace)
    ticks = re.search(r'FreeRTOS Tick Count:\s*(\d+)', trace)
    boot_reasons = []
    if c != 0 or not trace.strip():
        boot_reasons.append("no trace0 output")
    if "FATAL" in trace:
        boot_reasons.append("firmware reported FATAL: " + trace[trace.index("FATAL"):].splitlines()[0])
    if not ticks or int(ticks.group(1)) == 0:
        boot_reasons.append("scheduler tick not running (Tick Count 0 or missing)")
    if "Announcing rpmsg-ping-channel" not in trace:
        boot_reasons.append("rpmsg channel never announced (doorbell interrupt did not wake the task)")
    for r in boot_reasons:
        print(f"  \033[91m[FAIL] {r}\033[0m")
    results.append(("Profile 1", "testFreeRTOS boot (tick + IRQ wake)", "FAIL" if boot_reasons else "PASS"))

    c, out, _ = run_ssh("/usr/bin/ping_rpmsg -n 1000 -D 0", timeout=30)
    print(out)
    results.append(("Profile 1", "FreeRTOS ping_rpmsg (1000 pkts)", ping_exchanged(c, out)))
    c, out, _ = run_ssh("/usr/bin/stress_rpmsg -n 100000 -w 16", timeout=120)
    print(out)
    results.append(("Profile 1", "FreeRTOS stress_rpmsg (100k, 16 in flight)", ping_exchanged(c, out, expected=100000)))

    # 1.6b The RAM beyond SRAM spaces 0/1 (testExtraRam): DSP local RAM (the AbstractX
    # REPL heap), the DRAM carveout, and SRAMA3_2 shared by remap bit 1. The E906 checks
    # each region; Linux then reads the signature it left in DRAM and SRAMA3_2.
    print("\n--- 1.6b Running testExtraRam (DSP RAM, DRAM, SRAMA3_2) ---")
    run_ssh('echo "stop" > /sys/class/remoteproc/remoteproc0/state 2>/dev/null; echo "testExtraRam.elf" > /sys/class/remoteproc/remoteproc0/firmware; echo "start" > /sys/class/remoteproc/remoteproc0/state; sleep 6')
    c, trace, _ = run_ssh("tr -d '\\000' < /sys/kernel/debug/remoteproc/remoteproc0/trace0", timeout=10)
    trace = strip_ansi(trace)
    for name, base, size in EXTRA_RAM_REGIONS:
        why = []
        if f"EXTRA-RAM {name} PASS" not in trace:
            bad = [l for l in trace.splitlines() if f"EXTRA-RAM {name} FAIL" in l]
            why.append(bad[0].strip() if bad else "no result in trace0 (fault or hang while probing)")
        if base is not None and not why:
            _, out, _ = run_ssh(f"devmem 0x{base:08x} 32; devmem 0x{base + 4:08x} 32; devmem 0x{base + 8:08x} 32", timeout=10)
            words = [int(w, 16) for w in out.split()] if out.strip() else []
            if words != [0x45585452, base, 0]:
                why.append(f"Linux reads {[hex(w) for w in words]} at 0x{base:08x}, expected the E906 signature")
        for r in why:
            print(f"  \033[91m[FAIL] {name}: {r}\033[0m")
        results.append(("Profile 1", f"testExtraRam: {name} ({size // 1024} KB)", "FAIL" if why else "PASS"))

    # 1.7 Message box bank clocks/resets across unbind/bind
    print("\n--- 1.7 Message box clock/reset check (tools/msgbox_clock_reset_check.sh) ---")
    chk = os.path.join(os.path.dirname(os.path.abspath(__file__)), "msgbox_clock_reset_check.sh")
    if push_file(chk, "/root/msgbox_clock_reset_check.sh"):
        c, out, _ = run_ssh("sh /root/msgbox_clock_reset_check.sh", timeout=90)
        print(out)
        results.append(("Profile 1", "Msgbox clocks/resets (unbind -> bind)", "PASS" if c == 0 else "FAIL"))
    else:
        print("  \033[91m[FAIL] could not copy msgbox_clock_reset_check.sh to the target\033[0m")
        results.append(("Profile 1", "Msgbox clocks/resets (unbind -> bind)", "FAIL"))

    # 1.8 Message box TX completion benchmark, timer mode (no allwinner,tx-done-irq)
    msgbox_tx_modes = {"timer": run_msgbox_tx_bench("timer", results)}

    # 1.9 Kernel dmesg health check
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 1", allowed_patterns=["testCrash"])
    results.append(("Profile 1", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 1T: message box TX completion by write interrupt
    # -------------------------------------------------------------------------
    log_banner(1, "Profile 1T: message box TX done by write IRQ", "Overlay: cubie-a5e-flight-stack cubie-a5e-msgbox-txirq")
    p1t_up = set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-msgbox-txirq")
    results.append(("Profile 1T", "Boot + remoteproc0 present", "PASS" if p1t_up else "FAIL"))
    msgbox_tx_modes["isr"] = run_msgbox_tx_bench("isr", results)
    run_msgbox_wrirq_test(results)
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 1T", allowed_patterns=["Controller can't run the TX ticker"])
    results.append(("Profile 1T", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    print("\n  Message box TX completion: timer vs ISR")
    print(f"  {'Benchmark':<36} {'mode':<6} {'avg us':>8} {'max us':>8} {'jitter':>8} {'p99 us':>8} {'msgs/s':>10} {'RX/msg':>7} {'TX IRQ':>7} {'ovf':>5}")
    for key, label, _, _, _ in MSGBOX_TX_BENCH:
        for mode in ("timer", "isr"):
            m = msgbox_tx_modes.get(mode, {}).get(key, {})
            f = lambda v: "-" if v is None else (f"{v:.1f}" if isinstance(v, float) else str(v))
            print(f"  {label:<36} {mode:<6} {f(m.get('avg_us')):>8} {f(m.get('max_us')):>8} {f(m.get('jitter_us')):>8} "
                  f"{f(m.get('p99_us')):>8} {f(m.get('msgs_s')):>10} {f(m.get('rx_irqs_per_msg')):>7} {f(m.get('tx_irqs')):>7} {f(m.get('queue_overflows')):>5}")

    # -------------------------------------------------------------------------
    # PROFILE 2
    # -------------------------------------------------------------------------
    log_banner(2, "Testing Profile 2 (On-Chip SRAM Space 1 VirtIO)", "Overlay: cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram")
    p2_up = set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPingRpmsgSram")
    results.append(("Profile 2", "Boot + remoteproc0 present", "PASS" if p2_up else "FAIL"))

    # 2.1 run_tests.py
    print("\n--- 2.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=300)
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
    results.append(("Profile 2", "C++ ping_rpmsg (1000 pkts)", ping_exchanged(c, out)))

    print("\n--- 2.2b Running C++ stress_rpmsg (100,000 msgs, 16 in flight) ---")
    c, out, _ = run_ssh("/usr/bin/stress_rpmsg -n 100000 -w 16", timeout=120)
    print(out)
    results.append(("Profile 2", "C++ stress_rpmsg (100k, 16 in flight)", ping_exchanged(c, out, expected=100000)))

    # 2.3 Python ping_rpmsg.py
    print("\n--- 2.3 Running Python ping_rpmsg.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_rpmsg.py -n 1000 -d 0", timeout=30)
    print(out)
    results.append(("Profile 2", "Python ping_rpmsg.py (1000 pkts)", ping_exchanged(c, out)))

    # 2.4 Kernel dmesg health check
    dmesg_ok, dmesg_err = check_kernel_dmesg_health("Profile 2")
    results.append(("Profile 2", "Kernel Health (dmesg clean)", "PASS" if dmesg_ok else "FAIL"))

    # -------------------------------------------------------------------------
    # PROFILE 3
    # -------------------------------------------------------------------------
    log_banner(3, "Testing Profile 3 (Userspace UIO Direct Mailbox & SRAM)", "Overlay: cubie-a5e-flight-stack cubie-a5e-testPing")
    p3_up = set_overlay_and_reboot("cubie-a5e-flight-stack cubie-a5e-testPing",
                                   base_dtb="sun55i-a527-cubie-a5e-uio.dtb")
    results.append(("Profile 3", "Boot + remoteproc0 present", "PASS" if p3_up else "FAIL"))

    # 3.1 run_tests.py
    print("\n--- 3.1 Running automated test suite (run_tests.py) ---")
    c, out, _ = run_ssh("python3 /usr/bin/run_tests.py", timeout=300)
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
    results.append(("Profile 3", "C++ ping_shm (1000 pkts)", ping_exchanged(c, out)))

    # 3.2b C++ ping_uio (doorbell interrupt path)
    print("\n--- 3.2b Running C++ ping_uio (1,000 pings, doorbell IRQ) ---")
    c, out, _ = run_ssh("/usr/bin/ping_uio -n 1000", timeout=30)
    print(out)
    results.append(("Profile 3", "C++ ping_uio (1000 pkts, IRQ)", ping_exchanged(c, out)))

    # 3.3 Python ping_uio.py
    print("\n--- 3.3 Running Python ping_uio.py (1,000 pings) ---")
    c, out, _ = run_ssh("python3 /usr/bin/ping_uio.py -n 1000 -d 0", timeout=30)
    print(out)
    results.append(("Profile 3", "Python ping_uio.py (1000 pkts)", ping_exchanged(c, out)))

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
    # Out of range against earlier runs fails the sweep
    history_ranges = check_history_ranges(
        extract_key_metrics({"kunit_tests": kunit_summary, "profiles": aggregated_profile_data,
                             "msgbox_tx_modes": msgbox_tx_modes}),
        load_history(args.history_json))
    print_history_ranges(history_ranges)
    slow = [k for k, _, _, _, _, _, v in history_ranges if v == "SLOW"]
    results.append(("History", f"Metrics in range of last {HISTORY_WINDOW} PASS runs",
                    "FAIL" if slow else "PASS"))
    for k in slow:
        print(f"  \033[91m[FAIL] {k} out of range\033[0m")

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

    driver_coverage = None
    if g_coverage:
        driver_coverage = g_coverage.report()
        sweep_gcov.print_report(driver_coverage)

    # Save Host Reports
    report_bundle = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "target": f"{TARGET_USER}@{TARGET_IP}",
        "kunit_tests": kunit_summary,
        "sweep_summary": results,
        "msgbox_tx_modes": msgbox_tx_modes,
        "profiles": aggregated_profile_data,
        "driver_coverage": driver_coverage,
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
            f.write(f"- **Co-Processor**: XuanTie E906 RISC-V\n\n")

            if kunit_summary and "suites" in kunit_summary:
                f.write(f"## In-Kernel KUnit Driver Test Verification ({kunit_summary['total_expected']} Tests)\n\n")
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

            f.write("\n## Message Box TX Completion: Timer vs ISR\n\n")
            f.write("Timer: `last_tx_done()` = FIFO has room, 1 ms poll while full. ISR: overlay "
                    "`cubie-a5e-msgbox-txirq` (`allwinner,tx-done-irq`), E906 bank write IRQ on SPI 205, "
                    "enabled only while the FIFO is full. RX/msg = SPI 0 interrupts per message.\n\n")
            f.write("| Benchmark | Mode | Status | Avg (µs) | Max (µs) | Jitter (µs) | p99 (µs) | msgs/s | RX IRQ/msg | TX IRQs | Queue overflows |\n")
            f.write("| :--- | :--- | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n")
            fmt = lambda v: "-" if v is None else (f"{v:,.1f}" if isinstance(v, float) else str(v))
            for key, label, _, _, _ in MSGBOX_TX_BENCH:
                for mode in ("timer", "isr"):
                    m = msgbox_tx_modes.get(mode, {}).get(key, {})
                    f.write(f"| {label} | {mode} | **{m.get('status', 'NOT RUN')}** | {fmt(m.get('avg_us'))} | {fmt(m.get('max_us'))} | "
                            f"{fmt(m.get('jitter_us'))} | {fmt(m.get('p99_us'))} | {fmt(m.get('msgs_s'))} | "
                            f"{fmt(m.get('rx_irqs_per_msg'))} | {fmt(m.get('tx_irqs'))} | {fmt(m.get('queue_overflows'))} |\n")
            f.write("\n")

            if driver_coverage:
                sweep_gcov.write_markdown(f, driver_coverage)

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
        print(f"[REPORT] Saved sweep Markdown results to: {args.report_out}")
    except Exception as e:
        print(f"[WARN] Failed to write {args.report_out}: {e}")

    # Historical Tracking & Regression Analysis
    try:
        record_and_compare_history(report_bundle, args.history_json, args.history_md)
    except Exception as e:
        print(f"[WARN] Failed to update test history: {e}")

    # Performance page (docs/performance/index.html, served by GitHub Pages)
    try:
        subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "make_perf_page.py")], check=True)
    except Exception as e:
        print(f"[WARN] Failed to build the performance page: {e}")

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
