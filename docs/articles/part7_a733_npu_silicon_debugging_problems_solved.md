# Freeing the Allwinner A733 NPU on Mainline Linux 7.1: The 8 Real-World Problems We Solved on Live Silicon

**By Tim Michals**  
*Embedded Linux, Edge AI & Robotics Engineering Blog*

---

Spend ten minutes browsing the **Radxa Forums**, **Armbian Forums**, or **GitHub** issues for the **Allwinner A733** (Radxa Cubie A7A / A7Z / A7S, Orange Pi 4 Pro), and you will see developers hitting the exact same brick walls over and over:

* *"Why does `vpm_run` crash with `*_t527.nb` models?"*
* *"Why did `vip_init` break after updating my OS image (`2.0.3.4` vs `2.0.3.2`)?"*
* *"Why are my Frigate NVR / YOLOv8 bounding boxes completely scrambled?"*
* *"Why won't `aw_nna_vip` compile on Linux 6.6 / 6.18 / 7.1?"*
* *"Why does Mesa Teflon output all `128` (`0x80`) on `0x9202`?"*

When we brought up the A733 NPU from scratch on **mainline Linux `7.1.0 PREEMPT_RT` + Mesa 26.1.8 Teflon** on live Radxa Cubie A7A hardware, we didn't paper over these symptoms—we hunted down every single silicon root cause.

Here are the **8 concrete problems we ran into and solved**, why they happen in hardware, and how our open-source mainline stack fixes them for the entire A733 ecosystem.

---

## Problem 1: Marketing Specs vs. Live Silicon Identity (and Why T527 `.nb` Files Crash A733)

**The Problem:** Many early community notes assumed the A733 used Allwinner's 12-core `0x9113` (`VIP9000UL-DI+`) NPU, and users routinely tried running Allwinner T527 (`*_t527.nb`) pre-compiled models on A733 boards—only to hit immediate `vpm_run` crashes.

**What Live Silicon Proved:** Reading `/sys/kernel/debug/dri/0/gpu` directly from the Radxa Cubie A7A proved the A733 silicon is actually **`model 0x9000`, `revision 0x9202`**, `product_id 0x05090009`, **`customer_id 0x1000003b`** (`VeriSilicon VIP9000Nano-DI`, **`HW_V83`**, chip date `2023-05-18`).

Comparing it to the T527 (`0x9003`, `cid 0x10000016`) explains why `.nb` binaries can never cross between them:
* **T527 (`0x9003`)**: 6 NN cores, **2 TP cores**, `NN_CMD_SUPPORT_SLICE = 0`, `FP_INIMAGE_POST_SCALE = 0`, `MMU_PAGE_DESCRIPTOR = 0`.
* **A733 (`0x9202`)**: 8 NN cores, **0 TP cores (`TP_ENGINE = 0`)**, `HW_V83 = 1`, `NN_CMD_SUPPORT_SLICE = 1`, `FP_INIMAGE_POST_SCALE = 1`, `MMU_PAGE_DESCRIPTOR = 1`.

**How We Solved It:** Added exact `0x9202` HWDB entries to both mainline kernel `etnaviv_hwdb.c` and Mesa 26.1.8 (`0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch`). Because Mesa Teflon compiles standard **`.tflite` flatbuffers on-device at runtime**, the exact same `.tflite` model works across A733, T527, i.MX8MP, and Amlogic—no x86 Acuity Docker container and no per-SoC `.nb` exports.

---

## Problem 2: Vendor `aw_nna_vip` Build Failures & VIPLite Version Hell (`2.0.3.4` vs `2.0.3.2`)

**The Problem:** On vendor BSP images, users routinely hit `vip_init` failures from kernel/userspace version drift (`2.0.3.4-AW` vs `2.0.3.2`), missing `LD_LIBRARY_PATH` exports, and missing VIPLite packages on Debian R6 / Trixie. Meanwhile, Armbian and mainline developers trying to compile `aw_nna_vip` on Linux 6.6–7.1 hit missing `sunxi-autogen.h` headers and broken kernel APIs.

**How We Solved It:** Dropped the 44,000-line out-of-tree `aw_nna_vip` module entirely and bound `npu@3600000` directly to **in-tree mainline `drivers/gpu/drm/etnaviv`** (`/dev/dri/renderD128`, standard DRM UAPI) paired with Buildroot-compiled **Mesa 26.1.8 `libteflon.so`**. Zero proprietary blobs, zero ABI drift.

---

## Problem 3: `0x03600000` Bus Hangs During Bring-Up (4 Clocks, 3 Resets & Power Domain)

**The Problem:** Accessing the NPU MMIO window (`0x03600000`) on the A733 hangs the SoC if *any* piece of its power/clock/reset hierarchy is missing:
1. PCK-600 power domain **`PD_NPU` (domain 4)** and AXP8191 **`dcdc2` (`800 mV`)** must be active.
2. Register access traverses **two series AHB gates**: `CLK_AHB_NPU` (`0x05c0` bit 6) **and** `CLK_BUS_NPU` (`0x0b04` bit 0).
3. DMA stalls silently if **`CLK_MBUS_NPU`** (`0x05e0` bit 18, `600 MHz`) isn't enabled.
4. Unlike T527's single reset bit, A733 splits resets across **3 separate bits** in `0x0b04` (`CORE` bit 16, `AXI` bit 17, `AHB` bit 18)—and mainline `etnaviv` only fetched one reset line!

**How We Solved It:**
* Parented `bus_npu_clk` (`CLK_BUS_NPU`) directly to `ahb_npu_clk` (`CLK_AHB_NPU`) in `ccu-sun60i-a733.c`, added `CLK_SET_RATE_PARENT` to `npu_clk` (`840 MHz` `pll-npu`), and synced with upstream A733 CCU v7.
* Switched `etnaviv_gpu_platform_probe()` to `devm_reset_control_array_get_optional_exclusive()` and updated `vivante,gc.yaml` to `maxItems: 3`.
* Verified live readbacks: `0x02002b04 = 0x000F0000`, `GIC_SPI 65` (GICv3 hwirq 97) firing cleanly on every job, NPU thermal zone `35.9 °C`.

---

## Problem 4: Cracking MMUv2 `PDMODE` (`0x003B4`, `0x00388 = 0x21`, and the `0x00184` TLB Flush Trap)

**The Problem:** Even after `etnaviv` probed cleanly and executed PPU shaders, controlled silicon experiments revealed a wild hardware quirk: under legacy PTA mode (`0x003B4 = 0x1`, `0x00388 = 0x01`), NN descriptor reads and output writes hit DRAM properly, **yet NN input and compressed-coefficient DMA streams both returned `0xFF`** (even though unmapping them triggered exact MMU 0 page faults!).

Cross-checking Allwinner's vendor `vip_drv_hardware.c` and `vip_drv_mmu.c`—and verifying on live silicon—uncovered **three coupled MMUv2 PDMODE bugs** we had to fix in `linux-cubie`:
1. **`0x003B4` Mode Bit (`MODE4_K` vs `MODE1_K`)**: In PDMODE, `0x003B4 = ((mtlb_dma >> 12) << 4) | mode`. Vendor `vip2` uses 1 KB / 16 MB MTLBs (`mode = 1`), whereas mainline `etnaviv_iommu_v2.c` allocates 4 KB / 4 MB MTLBs (`MTLB_SHIFT = 22`, **`mode = 0 = MODE4_K`**). Setting bit 0 = `1` made every VA $\ge 4\text{ MB}$ (`0x10000000`, `0xfff00000`) fault with `slave not present`!
2. **Don't mix `PTA_CONTROL` with `PDMODE`**: On `0x9000 rev >= 0x9100`, skip writing `PTA_ADDRESS_*` / `PTA_CONTROL = 1`; instead program `0x0055C = 0x00FFFFFF` (`MC_L2_CACHE_CONFIG`), `0x00090 |= BIT(6)` (`bypass_reorder`), `0x003B4 = ((mtlb_dma >> 12) << 4) | MODE4_K`, and `0x00388 = 0x21` (`PDMODE | ENABLE`).
3. **The `VIVS_MMUv2_CONFIGURATION` (`0x00184`) TLB Flush Trap**: Mainline `etnaviv_buffer.c` flushes MMUv2 TLBs by writing `0x00000018` (`MODE_MASK | FLUSH`) to `0x00184`—leaving bit 8 (`ADDRESS_MASK`) as **`0`**, which **zeroed the MTLB base address** on every dynamic BO flush! Changing the PDMODE flush word to **`0xffffff7f`** (exact value from `vip_drv_mmu.c`, setting `ADDRESS_MASK = 1`, `MODE_MASK = 1`, `FLUSH_MASK = 0`, `FLUSH = 1`) preserves the page-table base cleanly.

---

## Problem 5: `TP_ENGINE = 0` — Why Frigate/YOLO Layouts Scramble and Why Mesa Teflon Zeroed Buffers

**The Problem:** Community developers building Frigate NVR and YOLOv8/YOLO11 plugins on A733 repeatedly hit scrambled bounding boxes because the VIP9000 NN engine uses planar **`NCHW`** internally while TFLite / Frigate expect **`NHWC`**. Worse, upstream Mesa `etna_hwdb.c` had:
```c
info->npu.tp_core_count = db->TPEngine_CoreCount ? db->TPEngine_CoreCount : 1;
```
That forced `tp_core_count = 1` on A733 even though `db->TP_ENGINE == 0`—turning Mesa's `TRANSPOSE` and `DETRANSPOSE` jobs into silent hardware no-ops that left output tensors all zeros!

**How We Solved It (`0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch`):**
* Report `tp_core_count = 0` whenever `db->TP_ENGINE == 0`.
* Execute internal layout conversions (`ETNA_ML_TP_TRANSPOSE`, `ETNA_ML_TP_DETRANSPOSE`, `ETNA_ML_TP_RESHUFFLE`) cleanly on CPU with proper GPU batch flushing/synchronization, so TFLite callers always get exact `NHWC` tensors.
* Decline standalone TP ops (`PAD`, `RELU`, `ABSOLUTE`, `LOGISTIC`) in `etna_ml_operation_supported()` when `tp_core_count == 0`, letting TFLite's NEON CPU kernels run them natively.

---

## Problem 6: Cracking `HW_V83` NN Descriptors (`FP_INIMAGE_POST_SCALE` & `NN_CMD_SUPPORT_SLICE`)

**The Problem:** Comparing all 21 NPU entries in Mesa's HWDB revealed that **A733 `0x9202` is the very first chip in Mesa** with `FP_INIMAGE_POST_SCALE = 1` (`NN_INT8_SCALE = 0`) and one of only three with `NN_CMD_SUPPORT_SLICE = 1`. Unmodified Mesa 26.1.8 produced `128` (`0x80`, the output zero-point) everywhere and only wrote 1 of 4 output channels!

**What Live Silicon Bit-Sweeps Proved:**
1. **`FP_INIMAGE_POST_SCALE = 1`**: Older V8 chips took `shift = 127 - exp` (e.g. `8`) in `post_shift`. On `0x9202`, `HW_V83` reconstructs a full **IEEE-754 float32** directly from `{sign(w27[28]), biased_exp(w15[30:28], w2[31:27]), mantissa(w22[25:18], w17[31:24], w15[27:22], w2[26])}`! Writing `8` gave $2^{8-127} \approx 0$, zeroing every accumulator to `0 + 128 = 128`. Passing the **raw IEEE-754 biased exponent (`scale_bits >> 23`)** immediately produced real spatially-structured 3×3 `SAME`-padded Conv2D outputs on live silicon (`0.11 ms` per dispatch).
2. **`NN_CMD_SUPPORT_SLICE = 1`**: `w8[31:16]` (`out_image_y_stride`) and `w16[31:16]` (`in_image_y_stride`) are **channel-slice byte strides (`x * y`)**, not row counts (`y`). Setting `out_image_y_stride = 64` (`8 * 8`) immediately wrote all 4 output channels cleanly (`256/256` bytes, `TotalWrites = 0x20`).
3. **`NN_TENSOR_ADD_FIELD_MOVE_TO_EXT_CMD = 1`**: `0x9202` moved NN tensor-add fields into an extended command format, causing Mesa's legacy v8 add lowering to hang the NN core. Declining `ADD`/`SUBTRACT` when `ETNA_FEATURE_NN_TENSOR_ADD_EXT_CMD` is set routes them cleanly to TFLite CPU with zero hangs until ext-cmd lowering is implemented.

---

## Problem 7: Post-Processing Accuracy Loss (`NMS`, `TopK`, `argmax`, `Softmax`)

**The Problem:** Vendor tutorials often struggle with NMS, `TopK`, or `Softmax` losing bounding-box precision when forced through INT8 quantization inside a monolithic `.nb` binary.

**How We Solved It:** Because Mesa Teflon plugs in as a standard **TFLite external delegate**, `etna_ml_operation_supported()` automatically partitions any `.tflite` graph: heavy quantized convolutions run on the 8× NN cores while precision-sensitive float32 post-processing (`NMS`, `TopK`, `Softmax`) stays seamlessly on the Arm Cortex-A76/A55 CPU—zero manual model surgery.

---

## Problem 8: Unlocking General-Purpose Shader Compute (`0.052–0.950 ms` & V4L2 `dma-buf` Verified Live)

**The Problem:** Proprietary VIPLite `.nb` blobs only run static neural graphs and completely hide the NPU's programmable Vivante PPU shader core. Additionally, an earlier 16-DWORD `dp2x8` test shader read an uninitialized `c3_512` uniform and had overlapping X-thread writes, causing cold-boot non-determinism.

**How We Solved It:** Built and verified a standalone, zero-header-dependency mainline `etnaviv` DRM compute tool—[`npu_ppu_compute_test.c`](../../project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c)—using a 28-DWORD shader with an explicitly loaded uniform `u2` (`VIVS_SH_HALTI5_UNIFORMS(8..11)`) and non-overlapping per-row thread writes. On live Cubie A7A silicon, it passes **10/10 consecutive runs** in **`0.052–0.950 ms`** with **5/5 bit-exact IEEE-754 float32 outputs**, and [`npu_v4l2_dmabuf_test.c`](../../project-cubie-a5e/board/radxa/cubie_a7a/src/npu_v4l2_dmabuf_test.c) proves zero-copy V4L2 (`0.206 ms`) and CMA `dma-heap` (`0.220 ms`) `dma-buf` PRIME import into the PPU shader. (Meanwhile, Path A `HW_V83` NN inference dispatches Conv2D in `0.105 ms` and passes `quantized_add` `192/192`, with the remaining NN DRAM-read bug tracked as WIP.)

---

## Where to Grab the Code & Patches

* **Mainline Linux 7.1 Kernel (`etnaviv` + A733 CCU/DT/ISP/VE/RemoteProc)**: [linux-cubie (`a7a_remoteproc_camera_usb`)](https://github.com/tcmichals/linux-cubie)
* **Mesa 26.1.8 Teflon Patches & PPU Utility**:
  * [`0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch`](../../project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch)
  * [`0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch`](../../project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch)
  * [`npu_ppu_compute_test.c`](../../project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c)
* **Companion Guides**:
  * [Part 6: NN Cores, Shaders & Best Uses on the Allwinner A733](part6_a733_npu_nn_vs_shaders_best_uses.md)
  * [Hackster.io Full NPU Architecture & Zero-Copy Vision Deep Dive](hackster_a733_vip9000_npu_etnaviv_teflon.md)
