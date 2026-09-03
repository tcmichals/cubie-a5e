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

# 3b. Decouple E902 from vendor scp.fex:
# Remove 'scp' from the embedded FIT image (sector 848) so the E902 stays in cold reset for Linux remoteproc
if [ -f "${BINARIES_DIR}/radxa_a733_bootloader.bin" ]; then
    python3 - << 'EOF'
import subprocess, os

bin_path = os.environ.get('BINARIES_DIR', '') + '/radxa_a733_bootloader.bin'
dtc_path = os.environ.get('HOST_DIR', '') + '/bin/dtc'
tmp_dir = os.environ.get('GENIMAGE_TMP', '/tmp/genimage.tmp')
os.makedirs(tmp_dir, exist_ok=True)

with open(bin_path, 'rb') as f:
    f.seek(848 * 512)
    hdr = f.read(8)
    if hdr[:4] == b'\xd0\x0d\xfe\xed':
        total_size = int.from_bytes(hdr[4:8], 'big')
        f.seek(848 * 512)
        fit_bytes = f.read(total_size)
        
        itb_path = os.path.join(tmp_dir, 'fit.itb')
        its_path = os.path.join(tmp_dir, 'fit.its')
        clean_itb = os.path.join(tmp_dir, 'fit_clean.itb')
        
        with open(itb_path, 'wb') as itb_f:
            itb_f.write(fit_bytes)
            
        subprocess.check_call([dtc_path, '-I', 'dtb', '-O', 'dts', itb_path, '-o', its_path])
        
        with open(its_path, 'r') as its_f:
            its_content = its_f.read()
            
        its_content = its_content.replace('loadables = "scp", "uboot";', 'loadables = "uboot";')
        
        with open(its_path, 'w') as its_f:
            its_f.write(its_content)
            
        subprocess.check_call([dtc_path, '-I', 'dts', '-O', 'dtb', its_path, '-o', clean_itb])
        
        with open(clean_itb, 'rb') as c_f:
            clean_bytes = c_f.read()
            
        with open(bin_path, 'r+b') as out_f:
            out_f.seek(848 * 512)
            out_f.write(clean_bytes)
            if len(clean_bytes) < total_size:
                out_f.write(b'\x00' * (total_size - len(clean_bytes)))
        print(">>> Successfully decoupled A733 bootloader from scp.fex (E902 held in clean cold reset)")
EOF
fi

# 4. Run genimage packaging pipeline
rm -rf "${GENIMAGE_TMP}"
PATH="${HOST_DIR}/bin:$PATH" ${HOST_DIR}/bin/genimage --config "${GENIMAGE_CFG}" --rootpath "${TARGET_DIR}" --tmppath "${GENIMAGE_TMP}" --inputpath "${BINARIES_DIR}" --outputpath "${BINARIES_DIR}"

exit 0
