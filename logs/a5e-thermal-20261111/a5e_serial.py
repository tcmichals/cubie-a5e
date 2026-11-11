#!/usr/bin/env python3
"""Run shell commands on the A5E serial console; log all bytes with timestamps.

    a5e_serial.py login
    a5e_serial.py run 'cmd' [--timeout S]
Output of `run` is printed between unique markers. Port is checked by banner.
"""
import sys, time, serial, os, argparse

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'serial-console.log')
ap = argparse.ArgumentParser()
ap.add_argument('mode'); ap.add_argument('cmd', nargs='?')
ap.add_argument('--dev', default='/dev/ttyUSB0'); ap.add_argument('--timeout', type=float, default=20)
a = ap.parse_args()
p = serial.Serial(a.dev, 115200, timeout=0.2)
logf = open(LOG, 'ab')

def rd(until, tmo):
    buf = b''; t = time.time()
    while time.time() - t < tmo:
        d = p.read(4096)
        if d:
            buf += d; logf.write(d); logf.flush()
            if until and until in buf:
                break
    return buf

def wr(s):
    logf.write(('\n[%s >>> %r]\n' % (time.strftime('%H:%M:%S'), s)).encode()); logf.flush()
    p.write(s.encode())

if a.mode == 'login':
    rd(None, 0.5); wr('\r'); b = rd(b'login: ', 4)
    if b'cubie-a5e-flight' not in b and b'# ' not in b:
        sys.exit('not the A5E or no prompt: %r' % b)
    if b'login: ' in b:
        wr('root\r'); b = rd(b'Password:', 4)
        if b'Password:' in b:
            wr('\r'); b = rd(b'# ', 6)
        if b'# ' not in b:
            sys.exit('login failed: %r' % b)
    print('logged in')
elif a.mode == 'run':
    tag = 'MK%d' % int(time.time() * 1000)
    rd(None, 0.3)
    wr("stty -echo 2>/dev/null; echo %s_S; %s; echo %s_E\r" % (tag, a.cmd, tag))
    b = rd((tag + '_E\r').encode(), a.timeout).decode('utf-8', 'replace')
    s = b.rfind(tag + '_S\r'); e = b.rfind(tag + '_E')
    print(b[s + len(tag) + 4:e].strip('\r\n') if s >= 0 and e > s else 'INCOMPLETE:\n' + b)
