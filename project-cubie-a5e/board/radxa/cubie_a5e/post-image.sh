#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# Compile boot.cmd into boot.scr using the host tool path guaranteed by host-uboot-tools
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "$(dirname $0)/boot.cmd" "${BINARIES_DIR}/boot.scr"

# Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0

