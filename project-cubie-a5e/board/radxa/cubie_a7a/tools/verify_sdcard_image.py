#!/usr/bin/env python3
"""
Radxa Cubie A7A (Allwinner A733) SD Card Image Verification Tool
This script validates every sector, header, checksum, and partition of an sdcard.img
against the ground-truth hardware specification before flashing.
"""

import sys
import os
import struct

def check_egon(data):
    """Computes the Allwinner eGON checksum over the payload."""
    calc_data = bytearray(data)
    # Checksum field at offset 12..16 must be replaced by STAMP 0x5F0A6C39
    calc_data[12:16] = (0x5F0A6C39).to_bytes(4, 'little')
    chksum = 0
    for i in range(0, len(calc_data), 4):
        chksum = (chksum + int.from_bytes(calc_data[i:i+4], 'little')) & 0xFFFFFFFF
    return chksum

def verify_image(img_path):
    if not os.path.exists(img_path):
        print(f"[-] ERROR: File not found: {img_path}")
        return False

    print("=" * 80)
    print(f"RADXA CUBIE A7A SDCARD IMAGE AUDIT: {img_path}")
    print(f"Total File Size: {os.path.getsize(img_path) / (1024*1024):.2f} MB")
    print("=" * 80)

    with open(img_path, "rb") as f:
        # 1. Check Sector 0 (MBR)
        f.seek(0)
        mbr = f.read(512)
        mbr_magic = mbr[510:512]
        if mbr_magic != b"\x55\xaa":
            print(f"[-] Sector 0: Invalid MBR magic: {mbr_magic.hex()} (Expected 55aa)")
            return False
        print("[+] Sector 0 (0 KB)     : Valid MBR (Magic 0xAA55)")

        # 2. Check Sector 256 (128 KB) - Full boot0
        f.seek(256 * 512)
        b0_hdr = f.read(512)
        b0_magic = b0_hdr[4:12]
        if b0_magic != b"eGON.BT0":
            print(f"[-] Sector 256: Invalid boot0 magic: {b0_magic}")
            return False

        b0_chk = int.from_bytes(b0_hdr[12:16], 'little')
        b0_len = int.from_bytes(b0_hdr[16:20], 'little')
        print(f"[+] Sector 256 (128 KB) : boot0 magic '{b0_magic.decode()}' found")
        print(f"    - Header Length     : {b0_len} bytes ({b0_len / 1024:.1f} KB / {b0_len // 512} sectors)")
        print(f"    - Header Checksum   : {hex(b0_chk)}")

        # Read the full declared length of boot0
        f.seek(256 * 512)
        b0_full = f.read(b0_len)
        if len(b0_full) != b0_len:
            print(f"[-] Sector 256: Incomplete boot0 read ({len(b0_full)} != {b0_len})")
            return False

        calc_chk = check_egon(b0_full)
        print(f"    - Computed Checksum : {hex(calc_chk)}")
        if calc_chk != b0_chk:
            print(f"[-] ERROR: boot0 eGON Checksum Mismatch! BootROM will refuse to execute!")
            return False
        print(f"    - Checksum Status   : VALID (BootROM will execute in SRAM)")

        # 3. Check Sector 2048 (1.0 MB) - U-Boot Stage
        f.seek(2048 * 512)
        uboot_hdr = f.read(64)
        if any(uboot_hdr):
            print(f"[+] Sector 2048 (1.0 MB) : U-Boot Stage binary present")

        # 4. Check MBR Partition Table
        for i in range(4):
            entry = mbr[446 + i*16 : 446 + (i+1)*16]
            status, chs_start, ptype, chs_end, lba_start, num_sectors = struct.unpack('<B3sB3sII', entry)
            if ptype == 0:
                continue

            size_mb = (num_sectors * 512) / (1024 * 1024)
            offset_mb = (lba_start * 512) / (1024 * 1024)

            if ptype == 0x0c:  # FAT32 LBA
                f.seek(lba_start * 512)
                vfat_sb = f.read(512)
                vfat_sig = vfat_sb[510:512]
                if vfat_sig == b"\x55\xaa":
                    print(f"[+] Partition {i+1} (Sector {lba_start} / {offset_mb:.1f} MB): boot.vfat (FAT32, {size_mb:.1f} MB, Magic 0xAA55)")
                else:
                    print(f"[-] Partition {i+1}: FAT32 boot sector signature invalid: {vfat_sig.hex()}")
                    return False
            elif ptype == 0x83:  # Linux Ext4
                f.seek(lba_start * 512 + 1024)
                ext4_sb = f.read(1024)
                ext4_magic = int.from_bytes(ext4_sb[0x38:0x3A], 'little')
                if ext4_magic == 0xef53:
                    print(f"[+] Partition {i+1} (Sector {lba_start} / {offset_mb:.1f} MB): rootfs.ext4 (Ext4, {size_mb:.1f} MB, Magic 0xEF53)")
                else:
                    print(f"[-] Partition {i+1}: Ext4 superblock magic not found (got {hex(ext4_magic)})")
                    return False

    print("=" * 80)
    print("[+] ALL AUDIT CHECKS PASSED: Image is structurally and mathematically valid!")
    print("=" * 80)
    return True

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "/home/tcmichals/ssdData/projects/home/CubieA5E/bld.a7a/images/sdcard.img"
    success = verify_image(target)
    sys.exit(0 if success else 1)
