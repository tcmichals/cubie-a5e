# Case Study: Migrating to a Mainline FOSS NPU Stack on Allwinner T527 (ARM64 / Linux 7.1)

This article documents the engineering journey of bringing up hardware-accelerated TinyML on the Allwinner T527 (Radxa Cubie A5E) using a fully open-source, mainline-compliant graphics and compute stack. It details the transition from legacy, out-of-tree proprietary vendor drivers to the built-in Linux **Etnaviv** driver and **Mesa Teflon** TensorFlow Lite delegate.

---

## 1. The Starting Point: The Proprietary Vendor Trap

Traditionally, hardware acceleration for Vivante-based Neural Processing Units (NPUs) — like the Vivante VIP9000-series GC9000 compute engine inside the Allwinner T527 SoC — relies on a vendor-supplied software stack:
* **Kernel Space:** The out-of-tree, proprietary **`sunxi-galcore`** Vivante kernel module (exposing `/dev/galcore`).
* **Userspace:** Proprietary precompiled HAL libraries and VeriSilicon's closed-source **TIM-VX** TensorFlow Lite delegate.

### The Problem: Legacy Code vs. Modern Kernels
When targeting modern mainline kernels (such as **Linux 7.1** with `PREEMPT_RT` enabled for flight controller applications), this proprietary setup quickly falls apart:
1. **Kbuild Disconnect:** The vendor's `Kbuild` system relies on `EXTRA_CFLAGS` to pass compiler include directories. Modern kernel build systems ignore `EXTRA_CFLAGS` in favor of `ccflags-y` and `subdir-ccflags-y`, resulting in immediate compiler aborts because the compiler cannot locate critical header files:
   ```text
   hal/os/linux/kernel/allocator/default/gc_hal_kernel_allocator_user_memory.c:56:10: fatal error: gc_hal_kernel_linux.h: No such file or directory
   ```
2. **Kernel API Drift:** Functions and macros like `in_irq()` or `MAX_ORDER` are deprecated or removed in newer kernels (e.g. `in_irq()` was removed in favor of `in_hardirq()`, and `MAX_ORDER` was renamed to `MAX_PAGE_ORDER`), causing implicit definition errors.
3. **Incompatible Callbacks:** The `.remove` callback signature for `struct platform_driver` was changed to return `void` instead of `int` in kernel 6.11, breaking the driver's platform registration.
4. **Toolchain Strictness:** Modern toolchains enforce `-Werror=missing-prototypes`, causing warnings about missing Vivante internal function definitions to treat warnings as fatal errors.

While we successfully patched these build issues by creating local Buildroot patches, compiling a massive, out-of-tree vendor driver that works against the grain of the mainline kernel represents a long-term maintenance nightmare.

---

## 2. The Mainline Pivot: Embracing FOSS

To achieve a stable, maintainable, and reproducible build system, we pivoted to a **Mainline-First & FOSS** architecture:
* **Completely reject** the proprietary closed-source `sunxi-galcore` driver and TIM-VX userspace bundle.
* **Adopt the built-in `etnaviv` DRM driver** which resides natively in the upstream Linux kernel source tree.
* **Adopt Mesa Teflon** — an open-source TensorFlow Lite compute delegate merged directly into the Mesa 3D Graphics Library.

This approach guarantees zero-copy image ingestion using standard userspace `dma-buf` file descriptors, fits natively into the kernel's Direct Rendering Manager (DRM) framework, and completely removes proprietary binary blobs from the filesystem.

---

## 3. Under the Hood: How the Mainline NPU Stack Works

To understand how hardware-accelerated machine learning functions without proprietary binary blobs, we must examine the data flow across the FOSS stack when running an inference workload:

```text
 ┌────────────────────────────────────────────────────────┐
 │        TensorFlow Lite Interpreter Application        │
 └───────────────────────────┬────────────────────────────┘
                             │ (1) load_delegate()
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │          Mesa Teflon Delegate (libteflon.so)           │
 └───────────────────────────┬────────────────────────────┘
                             │ (2) parse and lower graph
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │         Mesa Etnaviv Gallium Compute Driver            │
 └───────────────────────────┬────────────────────────────┘
                             │ (3) compile to Vivante ISA
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │    libdrm_etnaviv (Userspace DRM Command Submission)   │
 └───────────────────────────┬────────────────────────────┘
                             │ (4) ioctl(DRM_IOCTL_ETNAVIV_GEM_SUBMIT)
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │    etnaviv Kernel DRM Module (Memory / IRQ / MMU)      │
 └───────────────────────────┬────────────────────────────┘
                             │ (5) trigger hardware execution
                             ▼
 ┌────────────────────────────────────────────────────────┐
 │          Vivante GC9000 Compute Hardware               │
 └────────────────────────────────────────────────────────┘
```

1. **Delegate Handshake:** The TensorFlow Lite interpreter initializes the model graph and registers `/usr/lib/libteflon.so` as a hardware accelerator.
2. **Graph Translation:** Teflon parses the neural network graph, grouping supported operators (such as 2D convolutions, max pooling, and fully connected layers) into compute nodes. Teflon translates these nodes into standard **Gallium3D compute shaders** representing the mathematical operations.
3. **Instruction Compilation:** The Mesa `etnaviv` Gallium compiler lowers the compute shaders into the target hardware's Instruction Set Architecture (ISA). It outputs raw Vivante VIP9000 compute instructions and sets up the execution parameter matrices.
4. **Command Buffer Packaging:** Userspace libraries (via `libdrm_etnaviv`) package the compiled instructions, execution offsets, and memory pointers into a standard DRM command buffer. Input and output arrays reside in **Graphics Execution Manager (GEM)** buffer objects.
5. **Kernel Submission:** Userspace submits the command buffer to the kernel using the standard `ioctl(DRM_IOCTL_ETNAVIV_GEM_SUBMIT)` system call on `/dev/dri/renderD128`.
6. **Hardware Execution:** The kernel's `etnaviv` module handles scheduling:
   - It maps the memory buffer addresses into the NPU's virtual memory space (MMU).
   - It writes the command buffers into the hardware FIFO rings to start NPU execution.
   - It suspends userspace thread execution, waiting for the hardware interrupt.
   - Once the NPU asserts its completion interrupt, the kernel resumes userspace and frees/unmaps the buffers.
7. **Zero-Copy Ingestion:** Because both the camera capture interface (`v4l2`) and the DRM graphics memory buffers (`etnaviv` GEM) support standard Linux **dma-buf** sharing, camera frames are handed directly to the NPU's virtual memory space without copy overhead (zero-copy), achieving maximum performance and low latency.

---

## 4. Step-by-Step Implementation

### Step 1: Device Tree Integration
Since the NPU is an internal system bus block (unlike headers or buses that map to external physical pins), it does not require board-level pin configuration inside a custom device tree overlay (`.dtso`). 

Instead, the NPU node is defined at the SoC level in the mainline kernel DTS tree ([sun55i-a523.dtsi](file:///home/tcmichals/projects/cubie/bld/build/linux-7.1/arch/arm64/boot/dts/allwinner/sun55i-a523.dtsi#L1078-L1088)):
```dts
npu: npu@7122000 {
    compatible = "vivante,gc";
    reg = <0x07122000 0x1000>;
    interrupts = <GIC_SPI 199 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&mcu_ccu CLK_BUS_MCU_NPU_ACLK>,
             <&ccu CLK_NPU>,
             <&mcu_ccu CLK_BUS_MCU_NPU_HCLK>;
    clock-names = "bus", "core", "reg";
    resets = <&mcu_ccu RST_BUS_MCU_NPU>;
    power-domains = <&ppu PD_NPU>;
};
```
The mainline kernel's `etnaviv` driver binds directly to the `"vivante,gc"` compatible string automatically.

### Step 2: Kernel Driver Configuration
We enabled the mainline `etnaviv` DRM driver fragment in our kernel config [linux.config](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/linux.config):
```ini
CONFIG_DRM=y
CONFIG_DRM_ETNAVIV=y
```

### Step 3: Upgrading Buildroot Mesa3D Package
To compile the Teflon delegate userspace library (`libteflon.so`), we extended Buildroot's standard `mesa3d` package:

1. **Config option added** to [Config.in](file:///home/tcmichals/projects/cubie/buildroot/package/mesa3d/Config.in):
   ```config
   config BR2_PACKAGE_MESA3D_TEFLON
       bool "Teflon TensorFlow Lite delegate"
       select BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_ETNAVIV
       help
         Enable the Teflon TensorFlow Lite delegate frontend for
         Vivante/Etnaviv NPU compute cores.
   ```
2. **Meson flag mapped** in [mesa3d.mk](file:///home/tcmichals/projects/cubie/buildroot/package/mesa3d/mesa3d.mk):
   ```makefile
   ifeq ($(BR2_PACKAGE_MESA3D_TEFLON),y)
   MESA3D_CONF_OPTS += -Dteflon=true
   else
   MESA3D_CONF_OPTS += -Dteflon=false
   endif
   ```

### Step 4: Disabling Legacy Packages
In our Buildroot defconfig [cubie_a5e_defconfig](file:///home/tcmichals/projects/cubie/cubie-a5e/project-cubie-a5e/configs/cubie_a5e_defconfig), we stripped the vendor stubs and enabled the open-source packages:
```diff
-BR2_PACKAGE_SUNXI_GALCORE=y
-BR2_PACKAGE_TIMVX_DELEGATE=y
+BR2_PACKAGE_MESA3D=y
+BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_ETNAVIV=y
+BR2_PACKAGE_MESA3D_TEFLON=y
```

---

## 4. Verification on Target

Once booted, the system is verified in three simple steps:

1. **Driver Probing:** Check that the driver successfully registered the NPU device over the system bus:
   ```bash
   dmesg | grep -i etnaviv
   ```
2. **Device Nodes:** Verify the presence of the standard DRM render node:
   ```bash
   ls -la /dev/dri/renderD128
   ```
3. **TensorFlow Lite Execution:** Load the open-source delegate directly in your model interpreter script:
   ```python
   import tflite_runtime.interpreter as tflite

   teflon_delegate = tflite.load_delegate("/usr/lib/libteflon.so")
   interpreter = tflite.Interpreter(
       model_path="my_model_quantized.tflite",
       experimental_delegates=[teflon_delegate]
   )
   ```

---

## 5. Architectural Benefits & Key Takeaways

| Feature | Legacy Vendor Stack | Mainline FOSS Stack |
|---|---|---|
| **Kernel Modules** | Out-of-tree (`sunxi-galcore`) | In-tree (`etnaviv` DRM driver) |
| **API Compliance** | Custom ioctl device node | Standard DRM/DRI interface |
| **Userspace Delegate** | Closed TIM-VX binary blobs | Open-source Mesa Teflon |
| **Kernel Upgrades** | Broken on every minor version | Works out-of-the-box (zero maintenance) |
| **Source Auditing** | Impossible (proprietary binaries) | 100% auditable and reproducible |

By migrating to this FOSS stack, we ensure that the Radxa Cubie A5E flight controller remains maintainable over years of mainline Linux kernel upgrades, keeping real-time flight logic safe, deterministic, and free of vendor lock-in.
