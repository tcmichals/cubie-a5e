#!/bin/sh
# Board check: message box bank clocks, resets and liveness, across a driver
# unbind/bind cycle. Run on the Cubie A5E as root (needs devmem, debugfs).
#
# Gate = bit 0, reset (1 = deasserted) = bit 16 of each bank's BGR register
# (ccu-sun55i-a523{,-mcu,-r}.c). A bank is "alive" when its register at 0x010
# reads 0x00020000; a gated bank reads 0. (The write-IRQ threshold registers
# cannot be written from the A55, so they are no use as a liveness probe.)

RPROC_DEV=7130000.remoteproc
MBOX_DEV=3003000.mailbox
R=/sys/class/remoteproc/remoteproc0

bank_list="ARM:0x0200171C:0x03003000:0 DSP:0x07102068:0x07120000:0 CPUS:0x0701017C:0x07094000:0 RV:0x07102128:0x07136000:2"

mountpoint -q /sys/kernel/debug || mount -t debugfs none /sys/kernel/debug

FAIL=0
fail() { echo "  FAIL: $*"; FAIL=1; }

alive() {	# base -> 1 if the bank answers
	[ "$(devmem $(( $1 + 0x10 )) 32)" = "0x00020000" ] && echo 1 || echo 0
}

show() {	# expect_on(1/0)
	for b in $bank_list; do
		IFS=: read name bgr base port <<EOF
$b
EOF
		r=$(( $(devmem $bgr 32) ))
		gate=$(( r & 1 )); rst=$(( (r >> 16) & 1 ))
		a=$(alive $base)
		printf "  %-5s BGR=0x%08x gate=%d reset_deasserted=%d alive=%s\n" $name $r $gate $rst "$a"
		if [ "$1" = 1 ]; then
			[ $gate -eq 1 ] && [ $rst -eq 1 ] && [ "$a" = 1 ] || fail "$name bank not clocked/out of reset/alive"
		else
			# The reset is never asserted (the other banks carry
			# traffic between the other processors); clocks are released
			[ $gate -eq 0 ] || fail "$name gate still on after unbind"
			[ $rst -eq 1 ] || fail "$name reset asserted on unbind"
			[ "$a" = 0 ] || fail "$name bank still answers after unbind"
		fi
	done
	grep -E " (bus-msgbox|bus-dsp-msgbox|bus-r-msgbox|bus-riscv-msgbox) " /sys/kernel/debug/clk/clk_summary |
		awk '{printf "  clk %-18s enable_count=%s prepare_count=%s\n", $1, $2, $3}'
}

echo "== 1. Bound, running"
show 1

echo "== 2. Unbind remoteproc and message box"
echo stop > $R/state 2>/dev/null
echo $RPROC_DEV > /sys/bus/platform/devices/$RPROC_DEV/driver/unbind
echo $MBOX_DEV > /sys/bus/platform/devices/$MBOX_DEV/driver/unbind
show 0

echo "== 3. Bind again"
echo $MBOX_DEV > /sys/bus/platform/drivers/sun55i-msgbox/bind
sleep 1
# fw_devlink re-probes the remoteproc (a consumer of the mailbox) on its own
[ -e /sys/bus/platform/devices/$RPROC_DEV/driver ] ||
	echo $RPROC_DEV > /sys/bus/platform/drivers/sunxi-rproc/bind
sleep 1
echo "  remoteproc: $(cat $R/state)"
show 1
grep mailbox /proc/interrupts

[ $FAIL -eq 0 ] && echo "RESULT: PASS" || echo "RESULT: FAIL"
exit $FAIL
