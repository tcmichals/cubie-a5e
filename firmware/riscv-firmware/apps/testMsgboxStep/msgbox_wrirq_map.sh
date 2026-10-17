#!/bin/sh
# Map each message box bank's WRITE IRQ (ARM link, p=3) to the A55 GIC line it drives.
# Lines not requested by Linux stay disabled, so a level source shows as pending.
GICD=0x03400000
allpend() { i=1; while [ $i -lt 10 ]; do printf "%d %s\n" $i $(devmem $((GICD + 0x200 + 4*i)) 32); i=$((i+1)); done; }
diffpend() {
	paste $1 $2 | while read n a m b; do
		x=$(( a ^ b )); bit=0
		while [ $x -ne 0 ]; do
			[ $((x & 1)) -eq 1 ] && echo "    -> GIC ID $((n*32+bit)) (SPI $((n*32+bit-32)))"
			x=$((x >> 1)); bit=$((bit+1))
		done
	done
}
probe() {   # name base port
	off=$((0x100*$3)); WEN=$(($2+0x30+off)); WST=$(($2+0x34+off)); CNT=$(($2+0x6C+off)); THR=$(($2+0x8C+off))
	devmem $THR 32 0; devmem $WST 32 0x80
	echo "$1 bank port $3: count=$(( $(devmem $CNT 32) )) WR_STATUS=$(devmem $WST 32)"
	allpend > /tmp/m0
	devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 ))
	allpend > /tmp/m1
	devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
	diffpend /tmp/m0 /tmp/m1
}
probe DSP  0x07120000 0
probe CPUS 0x07094000 0
probe RV   0x07136000 2
