#!/bin/sh
# Does the E906 (RV) bank's WRITE IRQ reach the A55 GIC, and on which line?
# T527 User Manual V0.94 Table 2-15: GIC ID 237 (SPI 205) = RISCV_MSGBOX_CPUX.
# Uses Linux channel 11 (RV bank port 2, p=3); needs testMsgboxStep running on the E906.
GICD=0x03400000
WEN=0x07136230; WST=0x07136234; CNT=0x0713626C; FIFO=0x0713627C; THR=0x0713628C
BLK=0x072B0000
pend() { id=$1; w=$(( $(devmem $((GICD + 0x200 + 4*(id/32))) 32) )); echo $(( (w >> (id%32)) & 1 )); }
allpend() { i=1; while [ $i -lt 10 ]; do printf "%d %s\n" $i $(devmem $((GICD + 0x200 + 4*i)) 32); i=$((i+1)); done; }
pop() {
	devmem $((BLK+0x0c)) 32 3
	s=$(( $(devmem $((BLK+0x04)) 32) + 1 )); devmem $((BLK+0x04)) 32 $s
	for i in $(seq 1 100); do [ $(( $(devmem $((BLK+0x08)) 32) )) -eq $s ] && break; usleep 1000; done
}
diffpend() {
	paste $1 $2 | while read n a m b; do
		x=$(( a ^ b )); bit=0
		while [ $x -ne 0 ]; do
			[ $((x & 1)) -eq 1 ] && echo "    GIC ID $((n*32+bit)) (SPI $((n*32+bit-32))) changed"
			x=$((x >> 1)); bit=$((bit+1))
		done
	done
}
[ "$(devmem $BLK 32)" = "0x53544550" ] || { echo "testMsgboxStep not running"; exit 1; }
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop; done
devmem $THR 32 0
devmem $WST 32 0x80                      # clear p=3 write status (condition true -> stays set)
echo "empty FIFO: WR_STATUS=$(devmem $WST 32)  GIC237 pending=$(pend 237)"
allpend > /tmp/w0
devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 ))
allpend > /tmp/w1
echo "A) write IRQ enabled, slot free: WR_EN=$(devmem $WEN 32)  GIC237 pending=$(pend 237)"
diffpend /tmp/w0 /tmp/w1
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
echo "   disabled again: GIC237 pending=$(pend 237)"
for i in 1 2 3 4 5 6 7 8; do devmem $FIFO 32 $((0xE0+i)); done
devmem $WST 32 0x80
echo "B) FIFO full ($(( $(devmem $CNT 32) ))), status cleared: WR_STATUS=$(devmem $WST 32)"
devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 ))
echo "   enable set while full: GIC237 pending=$(pend 237)  (expect 0)"
pop
echo "   E906 popped one (count $(( $(devmem $CNT 32) ))): WR_STATUS=$(devmem $WST 32)  GIC237 pending=$(pend 237)  (expect 1)"
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop; done
echo "cleanup: count=$(( $(devmem $CNT 32) ))  WR_EN=$(devmem $WEN 32)  GIC237 pending=$(pend 237)"
