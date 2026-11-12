#!/bin/sh
# Reset-under-load diagnostic heartbeat (A5E, 2026-11-11). Runs on the board.
# Prints one line per second to /dev/kmsg (so it reaches the serial console):
#   uptime, watchdogd schedstat (run ns, wait ns, slices) and every runnable
#   SCHED_FIFO/RR task (comm, rt priority, cpu), and WDT_MODE/WDT_IRQ_STAT of
#   the four SoC watchdogs (CPUX 0x02050000, CPUS 0x07020400, DSP 0x07100400,
#   RISCV 0x07132000; T527 User Manual 2.18.5).
# Start with: chrt -f 99 sh /root/hb.sh rt &
tag=${1:-hb}
wd=$(for d in /proc/[0-9]*; do [ "$(cat $d/comm 2>/dev/null)" = watchdogd ] && echo ${d#/proc/}; done)
n=0
while :; do
	n=$((n + 1))
	up=$(cut -d' ' -f1 /proc/uptime)
	ws=$(cat /proc/$wd/schedstat 2>/dev/null)
	rt=""
	for d in /proc/[0-9]*; do
		s=$(cat $d/stat 2>/dev/null) || continue
		# comm may not contain spaces for the tasks of interest
		set -- $s
		[ "$3" = R ] || continue
		[ "${41}" = 1 -o "${41}" = 2 ] || continue
		rt="$rt ${2}p${40}c${39}"
	done
	# WDT_MODE (+0x14) and WDT_IRQ_STAT (+0x04): CPUX, CPUS, DSP, RISCV
	w=""
	for b in 0x02050000 0x07020400 0x07100400 0x07132000; do
		w="$w $(devmem $((b + 0x14)) 32)/$(devmem $((b + 0x04)) 32)"
	done
	echo "$tag $n up=$up wd=[$ws] wdt:$w R_rt:$rt" > /dev/kmsg
	sleep 1
done
