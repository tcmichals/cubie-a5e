#!/bin/sh
# TX FIFO write-IRQ test on Linux channel 11 (E906 bank 0x07136000, port 2, p=3)
R=/sys/class/remoteproc/remoteproc0
WEN=0x07136230      # WRITE_IRQ_ENABLE, port 2
WST=0x07136234      # WRITE_IRQ_STATUS, port 2
CNT=0x0713626C      # MSG_STATUS p=3
FIFO=0x0713627C     # MSG_FIFO   p=3
THR=0x0713628C      # WRITE_IRQ_THRESHOLD p=3
BLK=0x072B0000      # testMsgboxStep command block
tx3() { grep "mailbox-tx3" /proc/interrupts | awk '{s=0; for(i=2;i<=9;i++) s+=$i; print s}'; }
pop() {   # ask the E906 to pop one message from p=3
	devmem $((BLK+0x0c)) 32 3
	s=$(( $(devmem $((BLK+0x04)) 32) + 1 ))
	devmem $((BLK+0x04)) 32 $s
	for i in $(seq 1 100); do [ $(( $(devmem $((BLK+0x08)) 32) )) -eq $s ] && break; usleep 1000; done
	echo "  E906 popped: before=$(( $(devmem $((BLK+0x10)) 32) )) after=$(( $(devmem $((BLK+0x14)) 32) )) value=$(devmem $((BLK+0x18)) 32)"
}

echo stop > $R/state 2>/dev/null; echo testMsgboxStep.elf > $R/firmware; echo start > $R/state
for i in $(seq 1 100); do [ "$(devmem $BLK 32)" = "0x53544550" ] && break; usleep 10000; done
echo "firmware magic: $(devmem $BLK 32)   state: $(cat $R/state)"

devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))   # write IRQ p=3 off
while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop >/dev/null; done
devmem $THR 32 0                                 # threshold field 0 = one free slot
for i in 1 2 3 4 5 6 7 8; do devmem $FIFO 32 $((0xA0 + i)); done
echo "1) FIFO filled: count=$(( $(devmem $CNT 32) ))  WR_STATUS=$(devmem $WST 32)  tx3 irqs=$(tx3)"

devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 ))
usleep 20000
echo "2) enable set while FULL: WR_EN=$(devmem $WEN 32)  tx3 irqs=$(tx3)   (expect no change, enable still 0x80)"

pop
usleep 20000
echo "3) after E906 pop: count=$(( $(devmem $CNT 32) ))  WR_EN=$(devmem $WEN 32)  tx3 irqs=$(tx3)   (expect +1, enable cleared by handler)"

devmem $WEN 32 $(( $(devmem $WEN 32) | 0x80 ))
usleep 20000
echo "4) enable set with a slot free (level): WR_EN=$(devmem $WEN 32)  tx3 irqs=$(tx3)   (expect +1 at once)"

while [ $(( $(devmem $CNT 32) )) -gt 0 ]; do pop >/dev/null; done
devmem $WEN 32 $(( $(devmem $WEN 32) & ~0x80 ))
echo "cleanup: count=$(( $(devmem $CNT 32) ))  WR_EN=$(devmem $WEN 32)"
dmesg | tail -6
