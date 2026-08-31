#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"
PACK_DIR="${BUILD_DIR}/pack_tmp"

# 1. Compile boot.cmd into boot.scr using host mkimage
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "${BOARD_DIR}/boot.cmd" "${BINARIES_DIR}/boot.scr"

# 2. Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"

# 3. Stage verified Radxa A733 16MB bootloader blob into BINARIES_DIR
if [ -f "${BOARD_DIR}/radxa_a733_bootloader.bin" ]; then
    cp -f "${BOARD_DIR}/radxa_a733_bootloader.bin" "${BINARIES_DIR}/radxa_a733_bootloader.bin"
elif [ -f "${BINARIES_DIR}/radxa_a733_bootloader.bin" ]; then
    :
else
    echo ">>> Fetching Radxa A733 bootloader blob..."
    curl -sL "https://github.com/radxa-build/radxa-a733/releases/download/rsdk-t5/radxa-a733_trixie_cli_t5.output_512.img.xz" | xz -dc 2>/dev/null | head -c 16777216 > "${BINARIES_DIR}/radxa_a733_bootloader.bin"
fi

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
PATH="${HOST_DIR}/bin:$PATH" ${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0
