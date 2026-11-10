#!/usr/bin/env python3
"""
patch_fit_no_scp.py: drop "scp" from the boot-package FIT loadables in radxa_a733_bootloader.bin.

The Radxa A733 bootloader blob carries a FIT at sector 848 (byte 0x6a000) whose config-1 has
    loadables = "scp", "uboot";
With "scp" listed, BL31 loads scp.fex and starts the E902 as its arisc/SCP. Linux remoteproc
needs the E902 left alone, so the list becomes
    loadables = "uboot";

The property is edited in place: its length goes from 10 to 6 and the freed 4 bytes become an
FDT_NOP token, as libfdt's fdt_nop_property() does. The FIT keeps its size and every image
keeps its offset, so patch_bl31_security.py (fixed offsets inside the FIT's atf data) still
applies. Idempotent; exits non-zero if the property is not in either known form.

Usage: patch_fit_no_scp.py radxa_a733_bootloader.bin
"""

import struct
import sys

FIT_OFFSET = 848 * 512
FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE, FDT_END_NODE, FDT_PROP, FDT_NOP, FDT_END = 1, 2, 3, 4, 9

NODE_PATH = ("configurations", "config-1")
PROP_NAME = "loadables"
OLD_VALUE = b"scp\0uboot\0"
NEW_VALUE = b"uboot\0"


def align4(n):
    return (n + 3) & ~3


def find_prop(fit):
    """Return (offset of the FDT_PROP token, value) for NODE_PATH/PROP_NAME, or None."""
    magic, totalsize, off_struct, off_strings = struct.unpack_from(">IIII", fit, 0)
    if magic != FDT_MAGIC:
        sys.exit(f"[FIT-NO-SCP] no FDT magic at 0x{FIT_OFFSET:x} (got 0x{magic:08x})")
    size_struct = struct.unpack_from(">I", fit, 36)[0]

    def name_at(off):
        return fit[off:fit.index(b"\0", off)].decode()

    path = []
    pos = off_struct
    end = off_struct + size_struct
    while pos < end:
        token = struct.unpack_from(">I", fit, pos)[0]
        if token == FDT_BEGIN_NODE:
            name = name_at(pos + 4)
            path.append(name)
            pos = align4(pos + 4 + len(name) + 1)
        elif token == FDT_END_NODE:
            path.pop()
            pos += 4
        elif token == FDT_PROP:
            length, nameoff = struct.unpack_from(">II", fit, pos + 4)
            if tuple(path[1:]) == NODE_PATH and name_at(off_strings + nameoff) == PROP_NAME:
                return pos, bytes(fit[pos + 12:pos + 12 + length])
            pos = align4(pos + 12 + length)
        elif token == FDT_NOP:
            pos += 4
        elif token == FDT_END:
            break
        else:
            sys.exit(f"[FIT-NO-SCP] bad FDT token 0x{token:x} at struct offset 0x{pos:x}")
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    filename = sys.argv[1]

    with open(filename, "r+b") as f:
        f.seek(FIT_OFFSET)
        hdr = f.read(8)
        totalsize = struct.unpack(">I", hdr[4:8])[0]
        f.seek(FIT_OFFSET)
        fit = bytearray(f.read(totalsize))

        found = find_prop(fit)
        if found is None:
            sys.exit(f"[FIT-NO-SCP] {filename}: /{'/'.join(NODE_PATH)}/{PROP_NAME} not found")
        pos, value = found

        if value == NEW_VALUE:
            print(f"[FIT-NO-SCP] {filename}: already patched (loadables = \"uboot\")")
            return
        if value != OLD_VALUE:
            sys.exit(f"[FIT-NO-SCP] {filename}: unexpected loadables {value!r}, not patching")

        old_span = align4(len(OLD_VALUE))
        new_span = align4(len(NEW_VALUE))
        struct.pack_into(">I", fit, pos + 4, len(NEW_VALUE))
        fit[pos + 12:pos + 12 + new_span] = NEW_VALUE.ljust(new_span, b"\0")
        for nop in range(pos + 12 + new_span, pos + 12 + old_span, 4):
            struct.pack_into(">I", fit, nop, FDT_NOP)

        if find_prop(fit) != (pos, NEW_VALUE):
            sys.exit(f"[FIT-NO-SCP] {filename}: verification after edit failed")

        f.seek(FIT_OFFSET)
        f.write(fit)
    print(f"[FIT-NO-SCP] {filename}: loadables = \"scp\", \"uboot\" -> \"uboot\" "
          f"(FIT at 0x{FIT_OFFSET:x}, size unchanged)")


if __name__ == "__main__":
    main()
