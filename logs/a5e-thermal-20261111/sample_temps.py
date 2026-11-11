#!/usr/bin/env python3
"""Sample A5E thermal zones + PMIC temp + loadavg over the serial console.

    sample_temps.py LABEL SECONDS [PERIOD]
Appends CSV rows to temps.csv next to this script. Prints each row.
Exits with status 3 as soon as any zone exceeds 85 C (caller aborts load).
"""
import subprocess, sys, time, os
LIMIT = int(os.environ.get("LIMIT_MC", "85000"))

here = os.path.dirname(os.path.abspath(__file__))
label, dur = sys.argv[1], float(sys.argv[2])
period = float(sys.argv[3]) if len(sys.argv) > 3 else 15
cmd = ('echo $(cut -d" " -f1-3 /proc/loadavg) $(cat /sys/class/thermal/thermal_zone[0-3]/temp) '
       '$(cat /sys/class/hwmon/hwmon0/temp1_input) $(cat /sys/devices/system/cpu/online)')
csv = os.path.join(here, 'temps.csv')
new = not os.path.exists(csv)
f = open(csv, 'a')
if new:
    f.write('time,phase,load1,load5,load15,cpu4_thermal_mC,cpu0_thermal_mC,gpu_thermal_mC,ddr_thermal_mC,axp717_mC,cpus_online\n')
end = time.time() + dur
rc = 0
while True:
    t0 = time.time()
    out = subprocess.run([sys.executable, os.path.join(here, 'a5e_serial.py'), 'run', cmd],
                         capture_output=True, text=True).stdout.strip().splitlines()
    v = out[-1].split() if out else []
    row = [time.strftime('%H:%M:%S'), label] + v
    f.write(','.join(row) + '\n'); f.flush(); print(','.join(row), flush=True)
    try:
        if max(int(x) for x in v[3:7]) > LIMIT:
            rc = 3
            print('ZONE ABOVE LIMIT', flush=True)
            if label == "load" or os.environ.get("STOP_ON_LIMIT"):
                break
    except (ValueError, IndexError):
        print('bad sample', out, flush=True)
    if time.time() >= end:
        break
    time.sleep(max(0, period - (time.time() - t0)))
sys.exit(rc)
