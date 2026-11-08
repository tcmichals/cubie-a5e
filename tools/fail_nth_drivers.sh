#!/bin/sh
# fail_nth_drivers.sh - allocation-failure sweep of the A5E message box and
# remoteproc drivers (runs on the board, BusyBox ash).
#
# Needs a kernel with FAULT_INJECTION, FAILSLAB and FAULT_INJECTION_DEBUG_FS.
# For each operation, N = 1, 2, ...: write N to /proc/self/fail-nth, run the
# operation from this shell (echo is a builtin, so the write runs in this task),
# and the Nth slab allocation of that call fails. Reading fail-nth back gives 0
# when the fault was injected; a positive value means the call made fewer than
# N allocations, and the loop stops. After every failure the operation is run
# again without injection and must succeed, so a failed unwind shows up.
#
# Usage: fail_nth_drivers.sh [firmware]   (default testPingRpmsg.elf)
# Output: a summary per operation (stdout; the failed writes report on stderr);
# exit 1 if any recovery failed.
#
# Combine with gcov (tools/sweep_gcov.py board) to see which error paths ran.

FW=${1:-testPingRpmsg.elf}
MAXN=${MAXN:-600}
RPROC_DRV=/sys/bus/platform/drivers/sunxi-rproc
MBOX_DRV=/sys/bus/platform/drivers/sun55i-msgbox
RPROC_DEV=7130000.remoteproc
MBOX_DEV=3003000.mailbox
D=/sys/kernel/debug
bad=0

mountpoint -q $D || mount -t debugfs none $D
echo N > $D/failslab/ignore-gfp-wait	# GFP_KERNEL allocations may fail too
echo 0 > $D/failslab/verbose		# no stack dump per injected failure

# The remoteproc index can change across unbind/bind: look it up every time
rp() {
	for d in /sys/bus/platform/devices/$RPROC_DEV/remoteproc/remoteproc*; do
		[ -e "$d/state" ] && { echo "$d"; return; }
	done
	echo /nonexistent
}
rproc_state() { cat "$(rp)/state" 2>/dev/null || echo absent; }

# wait_running: virtio devices appear after start; give it 5 s
wait_running() {
	i=0
	while [ $i -lt 50 ]; do
		[ "$(rproc_state)" = running ] && return 0
		sleep 0.1; i=$((i + 1))
	done
	return 1
}

# inject <label> <value> <file>: write <value> to <file> with fail-nth N for
# N = 1..; <pre> runs first, then after each injected failure <recover> runs and
# <ok> must hold (all three set by the caller). <file> may name $RP, the
# remoteproc sysfs dir, which is looked up before fail-nth is set: a $(...)
# forks, and the fork's allocations would take the failure. The write is a
# plain echo, not eval: a redirection that fails under eval (a special builtin)
# exits the shell, under echo it only fails the command.
inject() {
	label=$1; val=$2; file=$3
	n=1; hit=0; failed=0
	while [ $n -le $MAXN ]; do
		eval "$pre"
		RP=$(rp)	# no subshell while fail-nth is set: a fork allocates too
		f=$(eval echo "$file")
		echo $n > /proc/self/fail-nth
		echo "$val" > "$f"
		rc=$?
		read left < /proc/self/fail-nth
		echo 0 > /proc/self/fail-nth
		if [ "$left" != 0 ]; then
			echo "$label: $((n - 1)) allocations, all failed once (N=$n not reached, rc=$rc)"
			break
		fi
		hit=$((hit + 1))
		[ $rc -eq 0 ] || failed=$((failed + 1))
		eval "$recover"
		if ! eval "$ok"; then
			echo "$label: N=$n rc=$rc: RECOVERY FAILED ($(rproc_state))"
			bad=$((bad + 1))
		fi
		n=$((n + 1))
	done
	[ $n -gt $MAXN ] && echo "$label: stopped at MAXN=$MAXN"
	echo "$label: injected $hit, call failed $failed"
}

# 1. remoteproc start (rproc_boot: firmware load, parse_fw, prepare, start)
pre='echo stop > $(rp)/state 2>/dev/null; echo $FW > $(rp)/firmware'
recover='echo stop > $(rp)/state 2>/dev/null; echo start > $(rp)/state; wait_running'
ok='[ "$(rproc_state)" = running ]'
inject "rproc start" start '$RP/state'

# 2. remoteproc stop
pre='[ "$(rproc_state)" = running ] || { echo start > $(rp)/state; wait_running; }'
recover='[ "$(rproc_state)" = offline ] || echo stop > $(rp)/state 2>/dev/null; echo start > $(rp)/state; wait_running'
ok='[ "$(rproc_state)" = running ]'
inject "rproc stop" stop '$RP/state'

# 3. remoteproc probe (bind); auto boot loads the firmware in another task
echo stop > $(rp)/state 2>/dev/null
echo $RPROC_DEV > $RPROC_DRV/unbind
pre='[ -e $RPROC_DRV/$RPROC_DEV ] && echo $RPROC_DEV > $RPROC_DRV/unbind'
recover='[ -e $RPROC_DRV/$RPROC_DEV ] && echo $RPROC_DEV > $RPROC_DRV/unbind; echo $RPROC_DEV > $RPROC_DRV/bind'
ok='[ -e $RPROC_DRV/$RPROC_DEV ] && [ -e "$(rp)/state" ]'
inject "rproc probe" $RPROC_DEV $RPROC_DRV/bind

# 4. message box probe (the remoteproc, its only consumer, unbound first)
[ -e $RPROC_DRV/$RPROC_DEV ] && echo $RPROC_DEV > $RPROC_DRV/unbind
echo $MBOX_DEV > $MBOX_DRV/unbind
pre='[ -e $MBOX_DRV/$MBOX_DEV ] && echo $MBOX_DEV > $MBOX_DRV/unbind'
recover='[ -e $MBOX_DRV/$MBOX_DEV ] && echo $MBOX_DEV > $MBOX_DRV/unbind; echo $MBOX_DEV > $MBOX_DRV/bind'
ok='[ -e $MBOX_DRV/$MBOX_DEV ]'
inject "msgbox probe" $MBOX_DEV $MBOX_DRV/bind

# Back to normal: both bound, firmware running
[ -e $MBOX_DRV/$MBOX_DEV ] || echo $MBOX_DEV > $MBOX_DRV/bind
[ -e $RPROC_DRV/$RPROC_DEV ] || echo $RPROC_DEV > $RPROC_DRV/bind
sleep 1
echo stop > $(rp)/state 2>/dev/null; echo $FW > $(rp)/firmware; echo start > $(rp)/state
if wait_running; then
	echo "final: remoteproc running $FW"
else
	echo "final: remoteproc NOT running ($(rproc_state))"; bad=$((bad + 1))
fi

echo "recovery failures: $bad"
[ $bad -eq 0 ]
