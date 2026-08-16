#!/bin/sh
#
# XuanTie E907 RISC-V Co-Processor Loader Wrapper for Allwinner A527/T527/A733
VERSION="1.1.0"

# Look for riscv-load binary in:
# 1. Same directory as this script (e.g. ./riscv-load when run from local folder)
# 2. Standard system location /usr/bin/riscv-load
# 3. System PATH
SCRIPT_DIR="$(dirname "$0")"

if [ -x "$SCRIPT_DIR/riscv-load" ]; then
    exec "$SCRIPT_DIR/riscv-load" "$@"
elif [ -x /usr/bin/riscv-load ]; then
    exec /usr/bin/riscv-load "$@"
elif command -v riscv-load >/dev/null 2>&1; then
    exec riscv-load "$@"
fi

echo "load-riscv.sh version $VERSION"
echo "Error: riscv-load binary not found or not executable."
echo "Searched: $SCRIPT_DIR/riscv-load, /usr/bin/riscv-load, and PATH"
exit 1

