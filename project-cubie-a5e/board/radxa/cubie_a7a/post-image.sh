#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"
PACK_DIR="${BUILD_DIR}/pack_tmp"

# 1. Compile boot.cmd into boot.scr using host mkimage
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "${BOARD_DIR}/boot.cmd" "${BINARIES_DIR}/boot.scr"

# 2. Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"

# Copy plain-text runtime configuration templates into BINARIES_DIR for genimage FAT packaging
cp -f "${BOARD_DIR}/config.txt" "${BINARIES_DIR}/config.txt"
cp -f "${BOARD_DIR}/uEnv.txt" "${BINARIES_DIR}/uEnv.txt"

# Stage device tree overlays if available
if [ -f "${BOARD_DIR}/../../dts-overlay/allwinner/cubie-a7a-rproc.dtbo" ]; then
    cp -f "${BOARD_DIR}/../../dts-overlay/allwinner/cubie-a7a-rproc.dtbo" "${BINARIES_DIR}/cubie-a7a-rproc.dtbo"
fi

# 3. Stage the Radxa A733 16MB bootloader blob into BINARIES_DIR.
#    The FIT edit below changes the blob in place, so every build starts from a pristine copy
#    (radxa_a733_bootloader.orig.bin); a blob left by an older build may carry the dropped BL31 patch.
BL_ORIG="${BINARIES_DIR}/radxa_a733_bootloader.orig.bin"
if [ -f "${BOARD_DIR}/radxa_a733_bootloader.bin" ]; then
    cp -f "${BOARD_DIR}/radxa_a733_bootloader.bin" "${BL_ORIG}"
elif [ ! -s "${BL_ORIG}" ]; then
    echo ">>> Fetching Radxa A733 bootloader blob..."
    curl -sL "https://github.com/radxa-build/radxa-a733/releases/download/rsdk-t5/radxa-a733_trixie_cli_t5.output_512.img.xz" | xz -dc 2>/dev/null | head -c 16777216 > "${BL_ORIG}"
fi
if [ "$(stat -c %s "${BL_ORIG}" 2>/dev/null)" != "16777216" ]; then
    echo "ERROR: ${BL_ORIG} missing or not 16 MiB" >&2
    rm -f "${BL_ORIG}"
    exit 1
fi
cp -f "${BL_ORIG}" "${BINARIES_DIR}/radxa_a733_bootloader.bin"

# 3a. Keep BL31 from starting the E902 as arisc/SCP: drop "scp" from the FIT loadables
#     (lost in 4a276a2, SCP-1). Edits in place; nothing else in the vendor boot package changes.
#     The BL31 binary patch (patch_bl31_security.py, R_SPC port 3) is dropped: stock vendor BL31
#     (owner, 2026-11-14, DESIGN_DECISIONS.md "A7A: drop the BL31 binary patch").
python3 "${BOARD_DIR}/patch_fit_no_scp.py" "${BINARIES_DIR}/radxa_a733_bootloader.bin" || exit 1

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
PATH="${HOST_DIR}/bin:$PATH" ${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0

