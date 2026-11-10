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

# 3. Stage verified Radxa A733 16MB bootloader blob into BINARIES_DIR
if [ -f "${BOARD_DIR}/radxa_a733_bootloader.bin" ]; then
    cp -f "${BOARD_DIR}/radxa_a733_bootloader.bin" "${BINARIES_DIR}/radxa_a733_bootloader.bin"
elif [ -f "${BINARIES_DIR}/radxa_a733_bootloader.bin" ]; then
    :
else
    echo ">>> Fetching Radxa A733 bootloader blob..."
    curl -sL "https://github.com/radxa-build/radxa-a733/releases/download/rsdk-t5/radxa-a733_trixie_cli_t5.output_512.img.xz" | xz -dc 2>/dev/null | head -c 16777216 > "${BINARIES_DIR}/radxa_a733_bootloader.bin"
fi

# 3a. Keep BL31 from starting the E902 as arisc/SCP: drop "scp" from the FIT loadables
#     (lost in 4a276a2, SCP-1). Edits in place so the BL31 offsets below do not move.
python3 "${BOARD_DIR}/patch_fit_no_scp.py" "${BINARIES_DIR}/radxa_a733_bootloader.bin" || exit 1

# 3b. WORKAROUND (not the solution): binary-patch the closed vendor BL31 to un-gate R_SPC Port 3
#     (E902_CFG @ 0x07032000) for XuanTie E902 remoteproc. To be replaced by a BL31 built from
#     tracked source; see DESIGN_DECISIONS.md "A7A BL31: source code in git" and TODO.md.
python3 "${BOARD_DIR}/patch_bl31_security.py" "${BINARIES_DIR}/radxa_a733_bootloader.bin" || exit 1

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
PATH="${HOST_DIR}/bin:$PATH" ${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0

