#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"

# Compile boot.cmd into boot.scr using the host tool path guaranteed by host-uboot-tools
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "$(dirname $0)/boot.cmd" "${BINARIES_DIR}/boot.scr"

# Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "$(dirname $0)/uboot-env.txt"

# Stage verified vendor A733 bootloader into BINARIES_DIR
if [ ! -f "${BOARD_DIR}/radxa_a733_bootloader.bin" ]; then
    echo ">>> Staging Radxa A733 bootloader blob..."
    curl -sL "https://github.com/radxa-build/radxa-a733/releases/download/rsdk-t5/radxa-a733_trixie_cli_t5.output_512.img.xz" | xz -dc 2>/dev/null | head -c 16777216 > "${BOARD_DIR}/radxa_a733_bootloader.bin"
fi

cp -f "${BOARD_DIR}/radxa_a733_bootloader.bin" "${BINARIES_DIR}/radxa_a733_bootloader.bin"

# Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0

