# Capture the A7A console across a reboot. Usage:
#   python3 bootcap.py /dev/ttyUSB1 LOGFILE [--reboot CMD] [--uboot CMDFILE] [--timeout S]
# --reboot: log in (PW env) and send CMD (e.g. 'reboot').
# --uboot: interrupt autoboot, then send each line of CMDFILE at the U-Boot prompt.
# Stops at 'login:' (after the kernel booted) or at the timeout.
import serial,time,sys,os,re,argparse
a=argparse.ArgumentParser(); a.add_argument('dev'); a.add_argument('log')
a.add_argument('--reboot'); a.add_argument('--uboot'); a.add_argument('--timeout',type=float,default=240)
a=a.parse_args()
p=serial.Serial(a.dev,115200,timeout=0.2); log=open(a.log,'ab'); buf=b''
def pump():
    global buf
    d=p.read(4096)
    if d: log.write(d); log.flush(); buf+=d
    return d
def wait(pat,t):
    end=time.time()+t
    while time.time()<end:
        pump()
        if re.search(pat,buf[-4000:]): return True
    return False
t0=time.time()
if a.reboot:
    p.write(b'\r\n'); time.sleep(1); pump()
    if b'login:' in buf[-200:]:
        p.write(b'root\r\n'); wait(rb'Password: $',5); p.write(os.environ.get('PW','').encode()+b'\r\n'); wait(rb'# $',6)
    buf=b''; p.write(a.reboot.encode()+b'\r\n')
if a.uboot:
    # once U-Boot prints its banner, send spaces until the prompt shows (autoboot delay is 1 s)
    end=time.time()+a.timeout; ok=False; armed=False
    while time.time()<end:
        pump()
        if b'U-Boot 20' in buf: armed=True
        if armed: p.write(b' '); time.sleep(0.03)
        if armed and re.search(rb'=> *$',buf[-20:]):
            time.sleep(0.5); pump(); p.write(b'\n'); time.sleep(0.5); pump(); ok=True; break
    if not ok: print('NO UBOOT PROMPT'); sys.exit(2)
    for line in open(a.uboot):
        line=line.rstrip('\n')
        if not line or line.startswith('#'): continue
        p.write(line.encode()+b'\n')
        wait(rb'=> $',30 if not line.startswith('booti') else 0.5)
        if line.startswith('booti'): break
seen_linux=False
end=t0+a.timeout
while time.time()<end:
    pump()
    if b'Linux version' in buf: seen_linux=True
    if seen_linux and re.search(rb'login: ?$',buf[-50:]): break
print('elapsed %.0fs linux=%s login=%s'%(time.time()-t0,seen_linux,bool(re.search(rb'login: ?$',buf[-50:]))))
