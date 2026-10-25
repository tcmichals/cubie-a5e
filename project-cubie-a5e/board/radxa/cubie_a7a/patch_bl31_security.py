#!/usr/bin/env python3
"""
patch_bl31_security.py: Un-gate R_SPC Port 3 (E902_CFG @ 0x07032000) in Allwinner A733 BL31.

Context:
In the Radxa Cubie A7A vendor bootloader blob (radxa_a733_bootloader.bin), ARM Trusted Firmware
(BL31) is embedded in a FIT image loaded to DRAM 0x48000000. During cold boot, sunxi_security_setup()
at VA 0x48013180 un-gates:
  - 0x02054000 (SPC ports 0..7)
  - 0x07003000 (R_TZMA registers 0, 4, 8)
  - 0x07002004 (R_SPC port 0)
  - 0x07002014 (R_SPC port 1)
  - 0x07002024 (R_SPC port 2)
However, it omits R_SPC port 3 (0x07002034), which secures the XuanTie E902 configuration space
(E902_CFG @ 0x07032000, including STA_ADD_REG @ 0x07032204). Any Linux access to 0x07032000
triggers an immediate hardware SError / bus abort on the Cortex-A76/A55 cores.

This script patches BL31 in radxa_a733_bootloader.bin using an in-segment executable trampoline:
  1. At VA 0x480131c8 (file offset 0x15e96c), replace:
       str w0, [x1, #32]          (opcode: 0xb9002020)
     with:
       b   0x48015570             (opcode: 0x140008ea)
  2. At VA 0x48015570 (file offset 0x160d14, inside an unused .align 11 zero padding gap in .text):
       str w0, [x1, #32]          (opcode: 0xb9002020, original R_SPC port 2 write)
       str w0, [x1, #48]          (opcode: 0xb9003020, R_SPC port 3 UN-GATE!)
       b   0x480131cc             (opcode: 0x17fff715, return to next instruction)
"""

import sys
import struct
import os

EXPECTED_ORIGINAL_OPCODE = 0xb9002020  # str w0, [x1, #32]
PATCH_HOOK_OFFSET = 0x15e96c          # VA 0x480131c8
TRAMPOLINE_OFFSET = 0x160d14          # VA 0x48015570

B_TO_TRAMPOLINE = 0x140008ea          # b 0x48015570
TRAMPOLINE_CODE = struct.pack("<III",
    0xb9002020,                       # str w0, [x1, #32]
    0xb9003020,                       # str w0, [x1, #48]  (R_SPC Port 3 un-gate)
    0x17fff715                        # b 0x480131cc
)

def patch_bootloader(filename):
    if not os.path.exists(filename):
        print(f"Error: file {filename} does not exist", file=sys.stderr)
        sys.exit(1)

    with open(filename, "r+b") as f:
        # 1. Verify hook location
        f.seek(PATCH_HOOK_OFFSET)
        hook_val = struct.unpack("<I", f.read(4))[0]

        if hook_val == B_TO_TRAMPOLINE:
            print(f"[BL31-SECURITY] {filename}: Already patched with R_SPC Port 3 un-gating.")
            return

        if hook_val != EXPECTED_ORIGINAL_OPCODE:
            # Fallback search if offset shifted slightly
            f.seek(0)
            data = f.read()
            pattern = struct.pack("<IIII", 0xb9000020, 0xb9001020, 0xb9002020, 0xd2805201)
            pos = data.find(pattern)
            if pos == -1:
                print(f"[BL31-SECURITY] Warning: Expected pattern not found in {filename}, skipping patch.", file=sys.stderr)
                return
            hook_offset = pos + 8
            print(f"[BL31-SECURITY] Dynamic offset detected: {hex(hook_offset)}")
        else:
            hook_offset = PATCH_HOOK_OFFSET

        # 2. Verify trampoline area is zeroed padding
        f.seek(TRAMPOLINE_OFFSET)
        tramp_check = f.read(12)
        if tramp_check != b"\x00" * 12 and tramp_check != TRAMPOLINE_CODE:
            print(f"[BL31-SECURITY] Error: Trampoline area at {hex(TRAMPOLINE_OFFSET)} is not zero padding!", file=sys.stderr)
            sys.exit(1)

        # 3. Write trampoline
        f.seek(TRAMPOLINE_OFFSET)
        f.write(TRAMPOLINE_CODE)

        # 4. Write hook branch
        f.seek(hook_offset)
        f.write(struct.pack("<I", B_TO_TRAMPOLINE))
        f.flush()

        print(f"[BL31-SECURITY] Successfully patched {filename}:")
        print(f"  - Hook at offset {hex(hook_offset)} (VA 0x480131c8): b 0x48015570")
        print(f"  - Trampoline at offset {hex(TRAMPOLINE_OFFSET)} (VA 0x48015570): str w0, [x1, #32]; str w0, [x1, #48]; b 0x480131cc")
        print(f"  - R_SPC Port 3 (0x07002034) is now configured to un-gate E902_CFG (0x07032000).")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: patch_bl31_security.py <path_to_radxa_a733_bootloader.bin>")
        sys.exit(1)
    patch_bootloader(sys.argv[1])
