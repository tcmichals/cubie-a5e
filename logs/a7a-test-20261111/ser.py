# Serial command helper for the A7A console. Usage: PW=... python3 ser.py /dev/ttyUSB1 'cmd1' 'cmd2'
# Env: W = per-command timeout (s), N = tail chars to print, LOG = append log file.
import serial,time,sys,os,re
dev=sys.argv[1]; cmds=sys.argv[2:]
p=serial.Serial(dev,115200,timeout=0.3)
log=open(os.environ.get('LOG',os.path.join(os.path.dirname(os.path.abspath(__file__)),'serial-cmds.log')),'a')
def rd(t,until=None):
    end=time.time()+t; o=b''
    while time.time()<end:
        d=p.read(4096); o+=d
        if d: log.write(d.decode(errors='replace')); log.flush()
        if until and re.search(until,o.decode(errors='replace')): break
    return o.decode(errors='replace')
rd(0.5); p.reset_input_buffer()
p.write(b'\r\n'); o=rd(3,r'(login: $|# $)')
if 'login:' in o:
    p.write(b'root\r\n'); o2=rd(4,r'(Password: $|# $)')
    if 'Password' in o2: p.write(os.environ.get('PW','').encode()+b'\r\n'); o2+=rd(6,r'# $')
    o+=o2
for c in cmds:
    p.write(c.encode()+b'\r\n'); o+=rd(float(os.environ.get('W','20')),r'\n[^\n]*# $')
print(o[-int(os.environ.get('N','8000')):])
