#!/usr/bin/env python3
import sys
import struct

def sign_egon(bin_path):
    with open(bin_path, "r+b") as f:
        data = bytearray(f.read())

        # Ensure minimum 512 bytes aligned to 512
        if len(data) % 512 != 0:
            data += b'\x00' * (512 - (len(data) % 512))

        length = len(data)
        # Magic 'eGON.BT0'
        data[4:12] = b'eGON.BT0'
        # Size
        data[16:20] = struct.pack('<I', length)
        # Header size (0x20)
        data[20:24] = struct.pack('<I', 0x20)
        # Checksum calculation: replace 12:16 with STAMP 0x5F0A6C39
        data[12:16] = struct.pack('<I', 0x5F0A6C39)
        
        chksum = 0
        for i in range(0, len(data), 4):
            chksum = (chksum + int.from_bytes(data[i:i+4], 'little')) & 0xFFFFFFFF
        
        data[12:16] = struct.pack('<I', chksum)

        f.seek(0)
        f.write(data)

    print(f"[+] Signed eGON header on {bin_path}: length={length} bytes, checksum=0x{chksum:08x}")
    return chksum, length

def sign_elf(elf_path, chksum, length):
    with open(elf_path, "r+b") as f:
        content = bytearray(f.read())
        # Find 'eGON.BT0' in the ELF file
        idx = content.find(b'eGON.BT0')
        if idx != -1:
            base = idx - 4
            content[base+12:base+16] = struct.pack('<I', chksum)
            content[base+16:base+20] = struct.pack('<I', length)
            f.seek(0)
            f.write(content)
            print(f"[+] Patched eGON header in ELF {elf_path} at offset 0x{base:x}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: mksunxiriscv.py <firmware.bin> [firmware.elf]")
        sys.exit(1)
    chksum, length = sign_egon(sys.argv[1])
    if len(sys.argv) >= 3:
        sign_elf(sys.argv[2], chksum, length)
