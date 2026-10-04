#!/usr/bin/env bash
# Cubie A7A: per-boot USB health check + reboot test, driven over SSH.
#
# Usage:
#   a7a_usb_reboot_test.sh check            # USB check only (no reboot)
#   a7a_usb_reboot_test.sh reboot [N]       # N cycles: USB check -> traced reboot -> wait
#
# Each cycle: wait for SSH, verify USB (FE1.1S hub @ HS, flash disk, AIC8800,
# no -71/EMI errors, PORTSC speed), then enable initcall_debug so the serial
# console shows every device ->shutdown() and issue 'reboot'.
# If the board does not return, the last "calling ... shutdown" line on the
# serial console identifies the hanging driver.

BOARD=${BOARD:-root@192.168.3.4}
WAIT_UP=${WAIT_UP:-180}
LOG=${LOG:-a7a_usb_reboot_$(date +%Y%m%d_%H%M%S).log}
SSH="ssh -o ConnectTimeout=3 -o BatchMode=yes -o StrictHostKeyChecking=no"

log() { echo "[$(date +%T)] $*" | tee -a "$LOG"; }

wait_up() {
	local t=0
	while ! $SSH "$BOARD" true 2>/dev/null; do
		sleep 2; t=$((t + 3))
		[ $t -ge "$WAIT_UP" ] && return 1
	done
	sleep 5   # let udev/usb enumeration settle
	return 0
}

usb_check() {
	local out pass=1
	out=$($SSH "$BOARD" '
		echo "UPTIME $(cut -d. -f1 /proc/uptime)"
		echo "KERNEL $(uname -v)"
		echo "PORTSC $(devmem 0x06a00430 32)"
		echo "MFCLK  $(devmem 0x02003354 32)"
		lsusb -t
		dmesg | grep -cE "error -71|not responding to setup|EMI\?|can.t read hub descriptor" | sed "s/^/USBERR /"
		ls /sys/block | grep -q "^sd" && echo "DISK yes" || echo "DISK no"
		lsusb | grep -qi "1a40:0101" && echo "HUB yes" || echo "HUB no"
		lsusb | grep -qi "a69c:" && echo "WIFI yes" || echo "WIFI no"
	' 2>&1)
	echo "$out" >> "$LOG"

	grep -q "HUB yes" <<<"$out"   || { log "FAIL: FE1.1S hub (1a40:0101) not enumerated"; pass=0; }
	grep -q "480M" <<<"$out"      || { log "FAIL: no 480M (high-speed) device in lsusb -t"; pass=0; }
	grep -q "WIFI yes" <<<"$out"  || { log "FAIL: AIC8800 not enumerated"; pass=0; }
	grep -q "DISK yes" <<<"$out"  || log "WARN: no USB disk present (ok if nothing plugged in)"
	grep -q "USBERR 0" <<<"$out"  || { log "FAIL: USB errors in dmesg"; pass=0; }

	log "$(grep -E 'UPTIME|KERNEL|PORTSC|MFCLK' <<<"$out" | tr '\n' ' ')"
	[ $pass -eq 1 ] && log "USB: PASS" || log "USB: FAIL"
	return $((1 - pass))
}

traced_reboot() {
	$SSH "$BOARD" '
		echo 1 > /sys/module/kernel/parameters/initcall_debug
		echo 8 > /proc/sys/kernel/printk
		sync
		(sleep 1; reboot) >/dev/null 2>&1 &
	' 2>/dev/null
	log "reboot issued (initcall_debug on: watch serial for last \"shutdown\" line)"
	sleep 10   # make sure it actually went down before polling
}

case "${1:-check}" in
check)
	wait_up || { log "board not reachable"; exit 1; }
	usb_check
	;;
reboot)
	n=${2:-1}
	for i in $(seq 1 "$n"); do
		log "=== cycle $i/$n ==="
		wait_up || { log "board not reachable"; exit 1; }
		usb_check
		traced_reboot
		if ! wait_up; then
			log "REBOOT: FAIL (board did not return within ${WAIT_UP}s) - check serial"
			exit 2
		fi
		log "REBOOT: PASS"
	done
	usb_check
	;;
*)
	echo "usage: $0 check | reboot [N]"; exit 1
	;;
esac
