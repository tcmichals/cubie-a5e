#!/bin/sh
BOARD_DIR="$(dirname $0)"
GENIMAGE_CFG="${BOARD_DIR}/genimage.cfg"
GENIMAGE_TMP="${BUILD_DIR}/genimage.tmp"
PACK_DIR="${BUILD_DIR}/pack_tmp"

# 1. Compile boot.cmd into boot.scr using host mkimage
${HOST_DIR}/bin/mkimage -A arm64 -T script -C none -d "${BOARD_DIR}/boot.cmd" "${BINARIES_DIR}/boot.scr"

# 2. Compile uboot-env.txt into uboot.env binary using host mkenvimage
${HOST_DIR}/bin/mkenvimage -s 0x10000 -o "${BINARIES_DIR}/uboot.env" "${BOARD_DIR}/uboot-env.txt"

# 3. Pack Mainline U-Boot (4KB Page-Aligned at 0x4a001000) + TF-A BL31 + OP-TEE + ARISC + DTB into TOC1 boot_package.fex
mkdir -p "${PACK_DIR}"

python3 -c "
with open('${BOARD_DIR}/tools/header-info.bin', 'rb') as f:
    hdr = bytearray(f.read())
# Patch branch opcode at byte 0 to 'b +0x1000' (0x14000400)
hdr[0:4] = bytes([0x00, 0x04, 0x00, 0x14])
# Ensure DRAM load address at 0x2c is 0x4a000000
hdr[0x2c:0x30] = bytes([0x00, 0x00, 0x00, 0x4a])
# Pad header to exactly 4096 bytes (4KB page aligned)
if len(hdr) < 4096:
    hdr.extend(b'\x00' * (4096 - len(hdr)))
with open('${PACK_DIR}/header-info-4k.bin', 'wb') as f:
    f.write(hdr)
"

cat "${PACK_DIR}/header-info-4k.bin" "${BINARIES_DIR}/u-boot.bin" > "${PACK_DIR}/u-boot-with-head.bin"
cp -f "${BOARD_DIR}/tools/bl31.bin" "${PACK_DIR}/bl31.bin"
cp -f "${BOARD_DIR}/tools/optee.bin" "${PACK_DIR}/optee.bin"
cp -f "${BOARD_DIR}/tools/scp.bin" "${PACK_DIR}/scp.bin"
cp -f "${BINARIES_DIR}/sun60i-a733-cubie-a7z.dtb" "${PACK_DIR}/dtb.bin"

cat > "${PACK_DIR}/boot_package.cfg" << EOF
[package]
item=u-boot, u-boot-with-head.bin
item=monitor, bl31.bin
item=optee, optee.bin
item=scp, scp.bin
item=dtb, dtb.bin
EOF

(cd "${PACK_DIR}" && "${BOARD_DIR}/tools/dragonsecboot" -pack ./boot_package.cfg)
cp -f "${PACK_DIR}/boot_package.fex" "${BINARIES_DIR}/boot_package.fex"

# 4. Copy full 240 KB boot0 binary to images
cp -f "${BOARD_DIR}/bin/boot0_sdcard.bin" "${BINARIES_DIR}/boot0_sdcard.bin"

# 5. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

# 6. Strict Automated Binary Audit & Checksum Gate
python3 "${BOARD_DIR}/tools/verify_sdcard_image.py" "${BINARIES_DIR}/sdcard.img" || {
    echo "[-] ERROR: Automated sdcard.img verification failed! Aborting build."
    exit 1
}

exit 0
