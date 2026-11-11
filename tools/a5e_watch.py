#!/usr/bin/env python3
"""Watch a board: timestamped serial log plus a temperature/load sample over ssh.

Both logs live on the PC, so they survive a board crash. Usage:
    a5e_watch.py --serial /dev/ttyUSB0 --ip 192.168.1.13 --hours 8 --out DIR
"""
import argparse, subprocess, threading, time, os, serial

def serial_log(dev, path, end):
    p = serial.Serial(dev, 115200, timeout=0.5)
    buf = b''
    with open(path, 'ab') as f:
        while time.time() < end:
            buf += p.read(4096)
            while b'\n' in buf:
                line, buf = buf.split(b'\n', 1)
                f.write(time.strftime('[%H:%M:%S] ').encode() + line + b'\n')
                f.flush()

def temp_log(ip, path, end, period):
    cmd = ("echo $(cat /proc/loadavg | cut -d' ' -f1-3) "
           "$(for z in /sys/class/thermal/thermal_zone*; do cat $z/type $z/temp; done)")
    with open(path, 'a') as f:
        while time.time() < end:
            r = subprocess.run(['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=5',
                                f'root@{ip}', cmd], capture_output=True, text=True)
            out = r.stdout.strip() if r.returncode == 0 else f'NO ANSWER ({r.returncode})'
            f.write(time.strftime('%H:%M:%S ') + out + '\n'); f.flush()
            time.sleep(period)

ap = argparse.ArgumentParser()
ap.add_argument('--serial', default='/dev/ttyUSB0'); ap.add_argument('--ip', required=True)
ap.add_argument('--hours', type=float, default=8); ap.add_argument('--period', type=float, default=30)
ap.add_argument('--out', required=True)
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)
end = time.time() + a.hours * 3600
stamp = time.strftime('%Y%m%d-%H%M')
t = threading.Thread(target=serial_log, args=(a.serial, f'{a.out}/serial-{stamp}.log', end), daemon=True)
t.start()
temp_log(a.ip, f'{a.out}/temps-{stamp}.log', end, a.period)
