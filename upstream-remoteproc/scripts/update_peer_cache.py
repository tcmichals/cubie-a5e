#!/usr/bin/env python3
"""update_peer_cache.py - Refresh cached mainline peer drivers from linux-cubie."""

import os
import shutil
import sys

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE_DIR = os.path.join(BASE_DIR, "peer-cache")
REPO_DIR = os.path.abspath(os.path.join(BASE_DIR, "..", "..", "linux-cubie"))

FILES_TO_CACHE = [
    "drivers/remoteproc/imx_rproc.c",
    "drivers/remoteproc/stm32_rproc.c",
    "drivers/remoteproc/ti_k3_common.c",
    "drivers/mailbox/sun6i-msgbox.c",
    "include/linux/remoteproc.h",
    "include/linux/mailbox_client.h",
    "include/linux/mailbox_controller.h",
    "Documentation/devicetree/bindings/remoteproc/st,stm32-rproc.yaml",
    "Documentation/devicetree/bindings/mailbox/allwinner,sun6i-a31-msgbox.yaml",
]

def main():
    if not os.path.isdir(REPO_DIR):
        sys.exit(f"error: linux-cubie repo not found at {REPO_DIR}")

    os.makedirs(CACHE_DIR, exist_ok=True)
    print(f"Refreshing peer-cache from {REPO_DIR}...")

    for rel_path in FILES_TO_CACHE:
        src = os.path.join(REPO_DIR, rel_path)
        dst = os.path.join(CACHE_DIR, os.path.basename(rel_path))
        if os.path.exists(src):
            shutil.copy2(src, dst)
            print(f"  [+] Cached {os.path.basename(rel_path)}")
        else:
            print(f"  [!] Missing: {src}", file=sys.stderr)

    print("Done.")

if __name__ == "__main__":
    main()
