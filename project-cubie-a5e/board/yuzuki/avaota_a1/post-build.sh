#!/bin/sh
set -e

BOARD_DIR="$(dirname "$0")"
MKIMAGE="${HOST_DIR}/bin/mkimage"

if [ -f "${BOARD_DIR}/boot.cmd" ]; then
    "${MKIMAGE}" -C none -A arm64 -T script -d "${BOARD_DIR}/boot.cmd" "${BINARIES_DIR}/boot.scr"
fi
