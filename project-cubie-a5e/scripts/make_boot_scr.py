#!/usr/bin/env python3
import struct
import zlib
import sys
import time

def make_uboot_script(input_cmd, output_scr):
    with open(input_cmd, 'rb') as f:
        data = f.read()

    magic = 0x27051956
    time_stamp = int(time.time())
    data_size = len(data)
    load_addr = 0x00000000
    entry_point = 0x00000000
    data_crc = zlib.crc32(data) & 0xffffffff
    os_type = 5      # IH_OS_LINUX
    arch = 22        # IH_ARCH_ARM64
    img_type = 6     # IH_TYPE_SCRIPT
    comp = 0         # IH_COMP_NONE
    name = b"U-Boot script"[:32].ljust(32, b'\x00')

    # Build header with header_crc = 0 to calculate header CRC
    header = struct.pack(
        ">IIIIIIIBBBB32s",
        magic,
        0, # placeholder
        time_stamp,
        data_size,
        load_addr,
        entry_point,
        data_crc,
        os_type,
        arch,
        img_type,
        comp,
        name
    )

    header_crc = zlib.crc32(header) & 0xffffffff

    final_header = struct.pack(
        ">IIIIIIIBBBB32s",
        magic,
        header_crc,
        time_stamp,
        data_size,
        load_addr,
        entry_point,
        data_crc,
        os_type,
        arch,
        img_type,
        comp,
        name
    )

    with open(output_scr, 'wb') as f:
        f.write(final_header)
        f.write(data)

    print(f"Successfully generated {output_scr} ({len(final_header) + len(data)} bytes) from {input_cmd}")

if __name__ == '__main__':
    in_file = sys.argv[1] if len(sys.argv) > 1 else 'project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd'
    out_file = sys.argv[2] if len(sys.argv) > 2 else 'boot.scr'
    make_uboot_script(in_file, out_file)
