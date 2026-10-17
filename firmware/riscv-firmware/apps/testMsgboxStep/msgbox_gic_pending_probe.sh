#!/bin/sh
# Which GIC SPI does the E906 (RV) bank's interrupt drive? Diff GICD_ISPENDR
# with the bank's read IRQ (port 2, p=3) asserted vs. not asserted.
GICD=0x03400000
REN=0x07136220; RST=0x07136224; FIFO=0x0713627C; CNT=0x0713626C
snap() { i=1; while [ $i -lt 10 ]; do printf "%d %s\n" $i $(devmem $((GICD + 0x200 + 4*i)) 32); i=$((i+1)); done; }
[ $(( $(devmem $CNT 32) )) -eq 0 ] && devmem $FIFO 32 0xC1
echo "count=$(( $(devmem $CNT 32) )) RD_STATUS=$(devmem $RST 32)"
snap > /tmp/p0
devmem $REN 32 $(( $(devmem $REN 32) | 0x40 ))
snap > /tmp/p1
devmem $REN 32 $(( $(devmem $REN 32) & ~0x40 ))
snap > /tmp/p2
echo "ISPENDR words that changed (n: off -> asserted -> off):"
paste /tmp/p0 /tmp/p1 /tmp/p2 | awk '$2!=$4 || $4!=$6 {print "  ISPENDR"$1": "$2" -> "$4" -> "$6}'
paste /tmp/p0 /tmp/p1 | while read n a m b; do
	x=$(( a ^ b )); bit=0
	while [ $x -ne 0 ]; do
		if [ $((x & 1)) -eq 1 ]; then id=$((n*32+bit)); echo "  GIC ID $id = SPI $((id-32)) changed"; fi
		x=$((x >> 1)); bit=$((bit+1))
	done
done
