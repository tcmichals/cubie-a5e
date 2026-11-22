# Common Subsystem Guide: Mainline NPU & TinyML Acceleration (Allwinner T527 / A5E & A733 / A7A)

This is the canonical guide for the **VeriSilicon Vivante VIP9000 Neural Processing Unit (NPU)** on both **Radxa Cubie A5E** (Allwinner T527 / A527) and **Radxa Cubie A7A** (Allwinner A733), covering hardware architecture, Buildroot/kernel configuration, verified benchmarks, and copy-pasteable **How-To Use** examples using **mainline Linux 7.1 `etnaviv` DRM** and **Mesa 26.1.8 Teflon** (`/dev/dri/renderD128`).

> **Deep-Dive Hackster.io Article & SVG Diagrams**: For a complete visual walkthrough of the `VIP9000Nano-DI` (`0x9202`) silicon dataflow, MMUv2 & DMA architecture, the 4-layer `etnaviv` + Mesa Teflon software stack, and zero-copy `dma-buf` video processing with MIPI CSI-2, Hardware ISP 602, Cedrus H.264/H.265 Video Encoder, and the E902 RISC-V coprocessor, see **[Freeing the 3.0 TOPS NPU on the Allwinner A733](../articles/hackster_a733_vip9000_npu_etnaviv_teflon.md)**.

---

## 1. Hardware Architecture & Platform Comparison

| Parameter | Radxa Cubie A7A (Allwinner A733 — Live Silicon Verified) | Radxa Cubie A5E (Allwinner A527 / T527) |
| :--- | :--- | :--- |
| **NPU Silicon IP** | VeriSilicon Vivante **VIP9000Nano-DI (`HW_V83`)** | VeriSilicon Vivante **VIP9000Nano-SI+** |
| **Chip ID / Revision** | Model `0x9000`, Rev **`0x9202`** (date `2023-05-18`) | Model `0x9000`, Rev **`0x9003`** |
| **Product / Customer / ECO ID** | `0x05090009` / **`0x1000003b`** / `0x08000000` | `0x05090009` / **`0x10000016`** / `0x06000000` |
| **Peak Performance** | **3.0 TOPS class (INT8)** (`420–1120 MHz` OPP) | **2.0 TOPS (INT8)** |
| **NN / TP / Shader Cores** | **8 NN cores** (64 MAD/core), **0 TP cores (`TP_ENGINE = 0`)**, **1 PPU shader core** | **6 NN cores** (64 MAD/core), **2 TP cores**, **1 PPU shader core** |
| **On-Chip VIP SRAM** | **512 KB** (`0x80000`, 128-byte physical width) | **512 KB** (`0x80000`, 64-byte physical width) |
| **MMU Architecture** | MMUv2 Secure AHB + **Page Descriptor (`0x003b4`)** | MMUv2 Secure AHB (`0x00388..0x003a8`) |
| **Physical MMIO Base** | **`0x03600000`** (`0x1000` byte window) | **`0x07122000`** (`0x2000` byte window) |
| **GIC Interrupt Vector** | **`GIC_SPI 65`** (`IRQ_TYPE_LEVEL_HIGH`) | **`GIC_SPI 211`** (`IRQ_TYPE_LEVEL_HIGH`) |
| **Clocks (`bus`, `core`, `reg`)** | `CLK_MBUS_NPU`, `CLK_NPU` (`pll-npu`, 420–840 MHz), `CLK_BUS_NPU` (parented to `CLK_AHB_NPU`) | `CLK_BUS_NPU_AXI`, `CLK_NPU`, `CLK_BUS_NPU_AHB` |
| **Resets** | 4 resets in `0x0b04`: `RST_BUS_NPU_CORE`, `RST_BUS_NPU_AXI`, `RST_BUS_NPU_AHB`, `RST_BUS_NPU_SRAM` | 1 reset: `RST_BUS_NPU` |
| **Power Domain & Supply** | `PD_NPU` (PCK-600 domain 4), AXP8191 `dcdc2` (`vdd-gpu-npu`, `800 mV`) | `PD_NPU` (PCK-600 domain 1), `dcdc3` |

---

## 2. Open-Source Software Stack & Buildroot Configuration

```text
┌─────────────────────────────────────────────────────────┐
│    Application (Python tflite_runtime / C++ / DRM)      │
├─────────────────────────────────────────────────────────┤
│       TensorFlow Lite Engine (libtensorflow-lite.so)    │
├─────────────────────────────────────────────────────────┤
│       Mesa Teflon NPU Delegate (libteflon.so)           │
├─────────────────────────────────────────────────────────┤
│       Mesa 26.1.8 Gallium Etnaviv Driver                │
├─────────────────────────────────────────────────────────┤
│       Mainline Linux 7.1 etnaviv DRM (/dev/dri/renderD128) │
└─────────────────────────────────────────────────────────┘
```

In this Buildroot tree (`project-cubie-a5e`):
* `CONFIG_DRM_ETNAVIV=m` (or `y`) is enabled in `linux.config` to build the open-source kernel DRM driver.
* `BR2_PACKAGE_MESA3D=y` and `BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_ETNAVIV=y` build Mesa's Etnaviv backend.
* `BR2_PACKAGE_MESA3D_TEFLON=y` builds the Teflon TensorFlow Lite external delegate (`/usr/lib/libteflon.so`).
* `BR2_PACKAGE_TENSORFLOW_LITE=y` builds the TensorFlow Lite runtime (`/usr/lib/libtensorflow-lite.so`).
* Cubie A7A Mesa 26.1.8 patches live in `project-cubie-a5e/board/radxa/cubie_a7a/patches/packages/mesa3d/`:
  * `0001-etnaviv-hwdb-add-allwinner-npu-feature-database.patch`: Allwinner `0x9000` feature database (`0x8303`, `0x9003`, `0x9113`, `0x9202`).
  * `0002-etnaviv-teflon-add-hw-v83-and-no-tp-support.patch`: `TP_ENGINE = 0` CPU fallback (`TRANSPOSE`, `DETRANSPOSE`, `RESHUFFLE`), `NN_CMD_SUPPORT_SLICE = 1` slice strides (`x * y`), `FP_INIMAGE_POST_SCALE = 1` raw IEEE-754 biased exponent (`scale_bits >> 23`, incl. bit 7 in `w15[30]`), and CPU fallback for `HW_V83` ext-cmd `ADD`/`SUBTRACT`.

---

## 3. Dual Mainline Compute Paths & Live Silicon Status (Cubie A7A)

### Quick On-Target Sanity Checks
```sh
dmesg | grep -i etnaviv
ls -l /dev/dri/renderD128
```

### Path B: Direct PPU Compute Shaders (`etnaviv` DRM — **PASS**)
The NPU's programmable Vivante PPU shader core runs directly via `DRM_IOCTL_ETNAVIV_GEM_SUBMIT` on `/dev/dri/renderD128`:
* **Tool**: [`project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c`](../../project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c) (`/usr/bin/npu-ppu-test`)
* **Verified Result (Cubie A7A, Linux 7.1 `PREEMPT_RT`, `0x9202`)**:
  * **10/10 consecutive runs PASS**, **`0.052–0.950 ms`** per dispatch, **5/5 bit-exact IEEE-754 float32 outputs** (`fill = 1, 2, 3, 5, 7` → `0x3d808000`, `0x3e008000`, `0x3e40c000`, `0x3ea0a000`, `0x3ee0e000`), **0 MMU faults**, **0 hangs**, clean `GIC_SPI 65` completion.
  * Cold-boot determinism fix: replaced the uninitialized `c3_512` uniform in `dp2x8` with a 28-DWORD shader (`img_load.u8` → `lshift.s32` → `rshift.s32` → `i2f.s32` → `mov t2, u2` → `mul` → `img_store.u8`) using an explicitly loaded uniform `u2` (`VIVS_SH_HALTI5_UNIFORMS(8..11)`) and non-overlapping per-row thread writes (`WORKGROUP_COUNT_X = 0`, `WORKGROUP_COUNT_Y = 5`).

### Zero-Copy V4L2 / CMA `dma-buf` PRIME → NPU (**PASS**)
Video and camera buffers (`V4L2` or CMA `dma-heap`) import into `etnaviv` without CPU `memcpy` via `DRM_IOCTL_PRIME_FD_TO_HANDLE`:
* **Tool**: [`project-cubie-a5e/board/radxa/cubie_a7a/src/npu_v4l2_dmabuf_test.c`](../../project-cubie-a5e/board/radxa/cubie_a7a/src/npu_v4l2_dmabuf_test.c) (`/usr/bin/npu-v4l2-dmabuf-test`)
* **Verified Result (Cubie A7A)**:
  * **V4L2 `VIDIOC_EXPBUF` (`/dev/video0`, `sun6i-csi-capture`) → PRIME → PPU shader**: **`0.206 ms`**, bit-exact `0x3ea0a000` (`fill = 5`), **PASS**.
  * **CMA `dma-heap` (`/dev/dma_heap/default_cma_region`) → PRIME → PPU shader**: **`0.220 ms`**, bit-exact `0x3ea0a000` (`fill = 5`), **PASS**.

### Path A: `HW_V83` NN Engine + Mesa 26.1.8 Teflon (**WIP**)
* **Tool**: `/usr/bin/npu-smoke-test`
* **Current Live Result (Cubie A7A, `0x9202`)**:
  * `quantized_add_8x8x3`: **`0.002 ms`** NPU/Teflon, **192/192 exact match** (`max_abs_diff = 0`).
  * `quantized_conv2d_8x8x3x4`: **`0.105 ms`** dispatch with `MMU_PAGE_DESCRIPTOR` (`0x003B4`), `TP_ENGINE = 0` CPU fallback, `NN_CMD_SUPPORT_SLICE`, and `FP_INIMAGE_POST_SCALE`, but fails accuracy (**7/256 exact match**, `max_abs_diff = 31`, constant per-channel output `[104, 104, 104, 104, 92, 92, 92, 92]`).
  * **Root-cause status**: The `HW_V83` NN engine executes and writes output to DRAM, but does not read input tensor data from DRAM (`run_lenet0` on official `lenet_v3.nb` produces constant `62` for `in=0x00`, `0x80`, `0xff`). Testing the vendor `#[initial states kernel]` sequence plus `0x038C0 = 0x0`, `0x038C4 = 0x1`, `0x03818 = 0x1` on live hardware confirmed it does not yet resolve the NN DRAM-read bug.

---

## 4. How-To Use Examples

### A. Python `tflite_runtime` + Mesa Teflon Delegate
*(Requires `tflite_runtime` Python package in rootfs; the default A7A Buildroot image ships C/C++ `libtensorflow-lite.so`.)*
```python
import numpy as np
import tflite_runtime.interpreter as tflite

# 1. Load Mesa Teflon NPU delegate
teflon_delegate = tflite.load_delegate("/usr/lib/libteflon.so")

# 2. Create interpreter with NPU delegate
interpreter = tflite.Interpreter(
    model_path="model_quant.tflite",
    experimental_delegates=[teflon_delegate],
)
interpreter.allocate_tensors()

in_det = interpreter.get_input_details()[0]
out_det = interpreter.get_output_details()[0]

# 3. Run inference
x = np.zeros(in_det["shape"], dtype=in_det["dtype"])
interpreter.set_tensor(in_det["index"], x)
interpreter.invoke()
y = interpreter.get_tensor(out_det["index"])
print("Output shape:", y.shape, "sample:", y.flat[:8])
```

### B. C/C++ TFLite External Delegate (`libteflon.so`)
```cpp
#include <cstdint>
#include <cstdio>
#include <vector>
#include "tensorflow/lite/c/c_api.h"
#include "tensorflow/lite/delegates/external/external_delegate.h"

int main() {
    TfLiteModel* model = TfLiteModelCreateFromFile("model_quant.tflite");
    TfLiteInterpreterOptions* opts = TfLiteInterpreterOptionsCreate();

    TfLiteExternalDelegateOptions ext_opts =
        TfLiteExternalDelegateOptionsDefault("/usr/lib/libteflon.so");
    TfLiteDelegate* delegate = TfLiteExternalDelegateCreate(&ext_opts);
    TfLiteInterpreterOptionsAddDelegate(opts, delegate);

    TfLiteInterpreter* interp = TfLiteInterpreterCreate(model, opts);
    TfLiteInterpreterAllocateTensors(interp);

    TfLiteTensor* in = TfLiteInterpreterGetInputTensor(interp, 0);
    std::vector<uint8_t> zeros(TfLiteTensorByteSize(in), 0);
    TfLiteTensorCopyFromBuffer(in, zeros.data(), zeros.size());

    TfLiteInterpreterInvoke(interp);

    const TfLiteTensor* out = TfLiteInterpreterGetOutputTensor(interp, 0);
    std::vector<uint8_t> res(TfLiteTensorByteSize(out));
    TfLiteTensorCopyToBuffer(out, res.data(), res.size());
    std::printf("First output byte: %u\n", res[0]);

    TfLiteInterpreterDelete(interp);
    TfLiteExternalDelegateDelete(delegate);
    TfLiteInterpreterOptionsDelete(opts);
    TfLiteModelDelete(model);
    return 0;
}
```
Cross-compile:
```sh
aarch64-buildroot-linux-gnu-g++ -O2 -o tflite_teflon_demo demo.cpp \
    -ltensorflow-lite -ldl
```

### C. Standalone Vivante PPU Compute Shader (`etnaviv` DRM)
Build and run the verified cold-boot-deterministic PPU compute shader tool (`npu_ppu_compute_test.c`):
```sh
aarch64-buildroot-linux-gnu-gcc -O2 -Wall -Wextra -o npu-ppu-test \
    project-cubie-a5e/board/radxa/cubie_a7a/src/npu_ppu_compute_test.c
scp -O npu-ppu-test root@192.168.0.194:/usr/bin/npu-ppu-test
ssh root@192.168.0.194 "/usr/bin/npu-ppu-test"
```

### D. Zero-Copy V4L2 / CMA `dma-buf` PRIME → NPU PPU Shader
Import a V4L2 capture buffer (`VIDIOC_EXPBUF`) or CMA `dma-heap` buffer directly into `etnaviv` via `DRM_IOCTL_PRIME_FD_TO_HANDLE` and run a PPU compute shader without CPU copies (`npu_v4l2_dmabuf_test.c`):
```sh
aarch64-buildroot-linux-gnu-gcc -O2 -Wall -Wextra -o npu-v4l2-dmabuf-test \
    project-cubie-a5e/board/radxa/cubie_a7a/src/npu_v4l2_dmabuf_test.c
scp -O npu-v4l2-dmabuf-test root@192.168.0.194:/usr/bin/npu-v4l2-dmabuf-test
ssh root@192.168.0.194 "/usr/bin/npu-v4l2-dmabuf-test && /usr/bin/npu-v4l2-dmabuf-test --cma"
```

---

## 5. Community Questions, Common A733 NPU Issues & Fixes

| Issue | Root Cause | Mainline Fix / Workaround |
| :--- | :--- | :--- |
| **T527 `*_t527.nb` fails `vpm_run` on A733** | T527 (`0x9003`: 6 NN / 2 TP) vs A733 (`0x9202`: 8 NN / **0 TP**, `HW_V83`, `SLICE=1`, `FP_POST_SCALE=1`) are binary-incompatible. | Mesa Teflon compiles standard `.tflite` files on-device at runtime—no Acuity Docker or per-SoC `.nb` exports. |
| **VIPLite `2.0.3.4-AW` vs `2.0.3.2` mismatch / missing libs on R6 & Trixie** | Proprietary `/dev/vipcore` and `libNBGlinker.so` blobs break across kernel/rootfs versions. | Uses standard Linux DRM UAPI (`/dev/dri/renderD128`, `etnaviv 1.4.0`) + Buildroot-compiled `libteflon.so`. |
| **Scrambled YOLO / Frigate bounding boxes** | `vip_query_output` reports NHWC while VIP9000 NN writes planar NCHW, and `0x9202` has `TP_ENGINE = 0`. | Teflon `0002` patch performs exact `NHWC ↔ NCHW` transpose/detranspose with GPU fence sync. |
| **NMS / TopK / Softmax accuracy loss on NPU** | Quantizing post-processing inside monolithic `.nb` breaks coordinate/score precision. | TFLite external delegate automatically keeps `NMS`, `TopK`, `Softmax`, and unsupported ops on Cortex-A76/A55 CPU. |
| **`aw_nna_vip` fails to compile on mainline 6.6 / 7.1** | Out-of-tree vendor driver depends on BSP 5.15 headers (`sunxi-autogen.h`). | Replaced with mainline `drivers/gpu/drm/etnaviv` + DT/CCU/reset/PDMODE patches. |
| **`0x03600000` register reads hang board** | Missing `PD_NPU`, `CLK_AHB_NPU` parent, or any of the 4 resets in `0x0b04`; or device runtime-suspended. | Parent `bus-npu` to `ahb-npu`, deassert resets via `reset_control_array`, only read MMIO while `runtime_status=active`. |
| **PPU shader (`npu-ppu-test`) non-deterministic after cold boot** | `dp2x8` read uninitialized `c3_512` uniform and 16 X-threads overlapped writes. | Replaced with 28-DWORD shader loading explicit `u2` uniform (`VIVS_SH_HALTI5_UNIFORMS(8..11)`) and non-overlapping rows. |

---

## 6. Vision-Based Flight Applications

1. **Precision Landing Target Detection**:
   - Quantized INT8/UINT8 MobileNet-YOLO running via Teflon NPU delegate alongside zero-copy CSI → ISP 602 → Cedrus H.264/H.265.
2. **Custom Vision / DSP Kernels**:
   - Low-latency Vivante PPU compute shaders via `etnaviv` (`0.052–0.950 ms`) with zero-copy V4L2/CMA `dma-buf` PRIME import (`0.206 ms`), feeding 14.5 µs MSGBOX setpoints to the on-die E902 RISC-V flight loop.
