#!/bin/sh
# Pulse the read IRQ (port n, p=3) of a remote bank with messages waiting and
# see which tx line counts. args: name base port
tx(){ grep mailbox-tx /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; printf "%s=%s ", $NF, s}'; echo; }
probe() {
	name=$1; base=$2; n=$3; off=$((0x100*n))
	REN=$((base+0x20+off)); FIFO=$((base+0x7C+off)); CNT=$((base+0x6C+off)); RST=$((base+0x24+off))
	[ $(( $(devmem $CNT 32) )) -eq 0 ] && devmem $FIFO 32 0xD1
	echo "$name bank port $n: count=$(( $(devmem $CNT 32) )) RD_STATUS=$(devmem $RST 32)"
	echo -n "  before: "; tx
	devmem $REN 32 $(( $(devmem $REN 32) | 0x40 )); devmem $REN 32 $(( $(devmem $REN 32) & ~0x40 ))
	echo -n "  after : "; tx
}
probe DSP  0x07120000 0
probe CPUS 0x07094000 0
probe RV   0x07136000 2
