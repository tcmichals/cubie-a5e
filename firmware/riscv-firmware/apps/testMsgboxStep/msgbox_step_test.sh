#!/bin/sh
# msgbox_step_test.sh - measure the message box write-IRQ status at every
# FIFO fill level, with the E906 popping exactly one message per command
# (firmware testMsgboxStep.elf). Run on the board as root.
#
# Uses Linux channel 11 (E906 bank, port 2, channel 3), which nothing else uses.
# Enables the E906 bank's bus clock and releases its reset by hand (MCU CCU
# 0x128); Linux does not do that today, see ALLWINNER_SUN55I_MSGBOX_REFERENCE.md.

set -e
R=/sys/class/remoteproc/remoteproc0
CCU=0x07102128
P=3
B=$((0x07136000 + 0x200))           # E906 bank, port 2 (ARM link)
CNT=$((B + 0x60 + 4 * P))
FIFO=$((B + 0x70 + 4 * P))
THR=$((B + 0x80 + 4 * P))
WST=$((0x07136000 + 0x34 + 0x200))  # WRITE_IRQ_STATUS(2)
WBIT=$((1 << (2 * P + 1)))
BLK=$((0x072B0000))                 # command block (core 0x3FFF0000)

count()  { echo $(( $(devmem $CNT 32) & 0xf )); }
status() { echo $(( ($(devmem $WST 32) & WBIT) != 0 )); }
clear_status() { devmem $WST 32 $WBIT; }

pop() {   # ask the E906 to pop one message, wait until it has
    seq=$(( $(devmem $((BLK + 4)) 32) + 1 ))
    devmem $((BLK + 0x0c)) 32 $P
    devmem $((BLK + 4)) 32 $seq
    n=0
    while [ $(( $(devmem $((BLK + 8)) 32) )) -ne $seq ]; do
        n=$((n + 1)); [ $n -gt 2000 ] && { echo "E906 did not answer"; exit 1; }
    done
    echo "e906: before $(( $(devmem $((BLK + 0x10)) 32) )) after $(( $(devmem $((BLK + 0x14)) 32) )) value $(devmem $((BLK + 0x18)) 32)"
}

row() {   # step label: count, raw status, status right after clearing
    raw=$(status); clear_status; printf "  %-22s count %d  raw %d  after-clear %d\n" "$1" "$(count)" "$raw" "$(status)"
}

devmem $CCU 32 0x00010001
echo stop > $R/state 2>/dev/null || true
echo testMsgboxStep.elf > $R/firmware
echo start > $R/state
n=0; while [ $(( $(devmem $BLK 32) )) -ne $((0x53544550)) ]; do n=$((n + 1)); [ $n -gt 2000 ] && { echo "firmware not ready"; exit 1; }; done
echo "E906 ready; channel 11 (E906 port 2, channel $P)"

for t in 0 1 2 3; do
    while [ "$(count)" -gt 0 ]; do pop >/dev/null; done
    devmem $THR 32 $t
    echo "== threshold field $t: write status when free slots >= $((1 << t))  (readback $(devmem $THR 32))"
    row "empty"
    for i in 1 2 3 4 5 6 7 8; do devmem $FIFO 32 $i; row "posted $i"; done
    for i in 1 2 3 4 5 6 7 8; do e=$(pop); row "popped $i ($e)"; done
done

devmem $THR 32 0
echo stop > $R/state
devmem $CCU 32 0x00000000
echo "restored: E906 stopped, MCU CCU 0x128 = $(devmem $CCU 32)"
