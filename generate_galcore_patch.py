#!/usr/bin/env python3
import os
import subprocess

src_dir = "/home/tcmichals/projects/cubie/bld/build/sunxi-galcore-4d035200e7b15d2713d49979a1d05f201b92cf4c/kernel-module-imx-gpu-viv-src"
patch_file = "/home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/package/sunxi-galcore/0002-fix-allocator-gfp-compile.patch"

files_to_patch = [
    "Kbuild",
    "hal/os/linux/kernel/gc_hal_kernel_linux.h",
    "hal/os/linux/kernel/gc_hal_kernel_os.c",
    "hal/os/linux/kernel/allocator/default/gc_hal_kernel_allocator_dma.c",
    "hal/os/linux/kernel/allocator/default/gc_hal_kernel_allocator_gfp.c",
    "hal/os/linux/kernel/gc_hal_kernel_driver.c"
]

# Ensure we start clean by copying original files
for f in files_to_patch:
    path = os.path.join(src_dir, f)
    orig_path = path + ".orig"
    if not os.path.exists(orig_path):
        subprocess.run(["cp", path, orig_path], check=True)

# 1. Kbuild edit: append -Wno-error to EXTRA_CFLAGS and make sure it is in ccflags
kbuild_path = os.path.join(src_dir, "Kbuild")
with open(kbuild_path, "r") as file:
    content = file.read()
if "EXTRA_CFLAGS += -Wno-error" not in content:
    content = content.replace(
        "$(MODULE_NAME)-objs  = $(OBJS)",
        "$(MODULE_NAME)-objs  = $(OBJS)\n\nEXTRA_CFLAGS += -Wno-error"
    )
    with open(kbuild_path, "w") as file:
        file.write(content)

# 2. gc_hal_kernel_linux.h edit: map in_irq() to in_hardirq()
linux_h_path = os.path.join(src_dir, "hal/os/linux/kernel/gc_hal_kernel_linux.h")
with open(linux_h_path, "r") as file:
    content = file.read()
if "in_hardirq()" not in content:
    content = content.replace(
        "#endif /* __gc_hal_kernel_linux_h_ */",
        "#ifndef in_irq\n#define in_irq() in_hardirq()\n#endif\n\n#endif /* __gc_hal_kernel_linux_h_ */"
    )
    with open(linux_h_path, "w") as file:
        file.write(content)

# 3. gc_hal_kernel_os.c edit: include <linux/mm.h>
os_c_path = os.path.join(src_dir, "hal/os/linux/kernel/gc_hal_kernel_os.c")
with open(os_c_path, "r") as file:
    content = file.read()
if "<linux/mm.h>" not in content:
    content = content.replace(
        "#include <linux/semaphore.h>",
        "#include <linux/semaphore.h>\n#include <linux/mm.h>"
    )
    with open(os_c_path, "w") as file:
        file.write(content)

# 4. gc_hal_kernel_allocator_dma.c edit: include <linux/mm.h>
dma_c_path = os.path.join(src_dir, "hal/os/linux/kernel/allocator/default/gc_hal_kernel_allocator_dma.c")
with open(dma_c_path, "r") as file:
    content = file.read()
if "<linux/mm.h>" not in content:
    content = content.replace(
        "#include <linux/atomic.h>",
        "#include <linux/atomic.h>\n#include <linux/mm.h>"
    )
    with open(dma_c_path, "w") as file:
        file.write(content)

# 5. gc_hal_kernel_allocator_gfp.c edit: include <linux/mm.h> and define MAX_ORDER
gfp_c_path = os.path.join(src_dir, "hal/os/linux/kernel/allocator/default/gc_hal_kernel_allocator_gfp.c")
with open(gfp_c_path, "r") as file:
    content = file.read()
if "<linux/mm.h>" not in content:
    content = content.replace(
        "#include <linux/slab.h>",
        "#include <linux/slab.h>\n#include <linux/mm.h>\n#ifndef MAX_ORDER\n#define MAX_ORDER MAX_PAGE_ORDER\n#endif"
    )
    with open(gfp_c_path, "w") as file:
        file.write(content)

# 6. gc_hal_kernel_driver.c edit: comment out MODULE_IMPORT_NS and fix remove callback
driver_c_path = os.path.join(src_dir, "hal/os/linux/kernel/gc_hal_kernel_driver.c")
with open(driver_c_path, "r") as file:
    content = file.read()
if "/* MODULE_IMPORT_NS(VFS_internal" not in content:
    content = content.replace(
        "MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);",
        "/* MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver); */"
    )
    content = content.replace(
        """#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int viv_dev_remove(struct platform_device *pdev)
#else
static int __devexit viv_dev_remove(struct platform_device *pdev)
#endif""",
        """#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
static void viv_dev_remove(struct platform_device *pdev)
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
static int viv_dev_remove(struct platform_device *pdev)
#else
static int __devexit viv_dev_remove(struct platform_device *pdev)
#endif"""
    )
    content = content.replace(
        """    gcmkFOOTER_NO();
    return 0;
}""",
        """    gcmkFOOTER_NO();
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0)
    return;
#else
    return 0;
#endif
}"""
    )
    with open(driver_c_path, "w") as file:
        file.write(content)

# Generate patch file using diff -urN
full_patch = ""
for f in files_to_patch:
    path = os.path.join(src_dir, f)
    orig_path = path + ".orig"
    
    # We run diff relative to the build directory root so paths match kernel-module-imx-gpu-viv-src/...
    rel_path_orig = os.path.join("kernel-module-imx-gpu-viv-src", f + ".orig")
    rel_path_new = os.path.join("kernel-module-imx-gpu-viv-src", f)
    
    # Run diff command from the parent directory of kernel-module-imx-gpu-viv-src
    parent_dir = os.path.dirname(src_dir)
    res = subprocess.run(
        ["diff", "-urN", rel_path_orig, rel_path_new],
        cwd=parent_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    
    # Process output line by line to safely rename headers
    lines = res.stdout.splitlines()
    for i, line in enumerate(lines):
        if line.startswith("--- " + rel_path_orig):
            lines[i] = "--- a/" + rel_path_new + line[len("--- " + rel_path_orig):]
        elif line.startswith("+++ " + rel_path_new):
            lines[i] = "+++ b/" + rel_path_new + line[len("+++ " + rel_path_new):]
    
    full_patch += "\n".join(lines) + "\n"

with open(patch_file, "w") as file:
    file.write(full_patch)

print("Patch generated successfully at:", patch_file)
