#!/bin/bash
# dt_error_tests.sh - boot the A5E once per malformed device tree and check that the
# message box / remoteproc probe rejects it with the expected error.
#
# Each overlay in tools/dt-error-overlays/ replaces one property of &msgbox or
# &rproc on top of cubie-a5e-flight-stack (expected.txt lists the message).
# Per test: copy the overlay to /boot, add it to config.txt, reboot, check that
# U-Boot applied it (boot.scr falls back to the plain base DTB when an overlay
# fails, so the marker property /dt-error-test is read back), grep dmesg, and,
# with --gcov-build-dir, save the driver line counters (tools/sweep_gcov.py board).
# config.txt is restored and the overlays removed on exit, also after a failure.
#
# Usage: tools/dt_error_tests.sh [--gcov-build-dir O] [--out-dir DIR] [test ...]
# Needs the cross dtc of a kernel build: DTC (default <gcov dir or KOBJ>/scripts/dtc/dtc).

set -u
HERE=$(cd "$(dirname "$0")" && pwd)
OVL=$HERE/dt-error-overlays
TARGET=${TARGET:-root@192.168.3.3}
KSRC=${KSRC:-$HERE/../../linux-v4-71}
GCOV_DIR=""
OUT=$PWD/dt-error-results
while [ $# -gt 0 ]; do
	case $1 in
	--gcov-build-dir) GCOV_DIR=$(cd "$2" && pwd); shift 2 ;;
	--out-dir) OUT=$2; shift 2 ;;
	*) break ;;
	esac
done
DTC=${DTC:-${GCOV_DIR:-${KOBJ:-}}/scripts/dtc/dtc}
[ -x "$DTC" ] || { echo "no dtc at '$DTC' (set DTC, KOBJ or --gcov-build-dir)"; exit 2; }
mkdir -p "$OUT"

ssh_b() { ssh -o BatchMode=yes -o ConnectTimeout=5 "$TARGET" "$@"; }

wait_board() {
	sleep 15
	for _ in $(seq 1 40); do
		ssh_b true 2>/dev/null && return 0
		sleep 5
	done
	return 1
}

restore() {
	echo "restoring config.txt and removing the test overlays"
	ssh_b 'cp /boot/config.txt.dterr-backup /boot/config.txt && rm -f /boot/cubie-a5e-dterr-*.dtbo /boot/config.txt.dterr-backup && sync' &&
		ssh_b 'sync; reboot' 2>/dev/null
	wait_board && echo "board back on: $(ssh_b 'grep dtoverlay /boot/config.txt')"
}

base=$(ssh_b "sed -n 's/^dtoverlay=//p' /boot/config.txt")
[ "$base" = "cubie-a5e-flight-stack" ] || { echo "expected dtoverlay=cubie-a5e-flight-stack, got '$base'"; exit 2; }
ssh_b 'cp /boot/config.txt /boot/config.txt.dterr-backup' || exit 2
trap restore EXIT

if [ $# -gt 0 ]; then tests="$*"; else tests=$(grep -v '^#' "$OVL/expected.txt" | cut -f1); fi
pass=0; fail=0
for t in $tests; do
	name=cubie-a5e-dterr-$t
	msg=$(awk -F'\t' -v t="$t" '$1 == t { print $2 }' "$OVL/expected.txt")
	cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I "$KSRC/include" \
	    -I "$KSRC/scripts/dtc/include-prefixes" "$OVL/$name.dtso" -o "$OUT/$name.pp" &&
		"$DTC" -@ -q -I dts -O dtb -o "$OUT/$name.dtbo" "$OUT/$name.pp" ||
		{ echo "FAIL $t: overlay does not build"; fail=$((fail + 1)); continue; }
	ssh_b "cat > /boot/$name.dtbo && sed -i 's/^dtoverlay=.*/dtoverlay=cubie-a5e-flight-stack $name/' /boot/config.txt && sync" \
		< "$OUT/$name.dtbo"
	ssh_b 'sync; reboot' 2>/dev/null
	if ! wait_board; then
		echo "FAIL $t: board did not come back"; fail=$((fail + 1)); break
	fi
	applied=$(ssh_b 'tr -d "\0" < /proc/device-tree/dt-error-test 2>/dev/null')
	ssh_b dmesg > "$OUT/$t.dmesg"
	if [ "$applied" != "$t" ]; then
		echo "FAIL $t: overlay not applied (marker '$applied')"; fail=$((fail + 1))
	elif grep -qF -- "$msg" "$OUT/$t.dmesg"; then
		echo "PASS $t: $(grep -F -- "$msg" "$OUT/$t.dmesg" | head -1 | sed 's/^\[[ 0-9.]*\] //')"
		pass=$((pass + 1))
	else
		echo "FAIL $t: '$msg' not in dmesg"; fail=$((fail + 1))
	fi
	if grep -qE 'WARNING:|BUG:|Oops|trying to register non-static key|ODEBUG:' "$OUT/$t.dmesg"; then
		echo "  kernel report during $t:"; grep -E 'WARNING:|BUG:|Oops|non-static key|ODEBUG:' "$OUT/$t.dmesg" | head -3
	fi
	if [ -n "$GCOV_DIR" ]; then
		python3 "$HERE/sweep_gcov.py" board --build-dir "$GCOV_DIR" --target "$TARGET" \
			--label "dterr-$t" --out "$OUT/$t.cov.json" > /dev/null || echo "  (no coverage saved)"
	fi
done
echo "dt error tests: $pass passed, $fail failed"
[ $fail -eq 0 ]
