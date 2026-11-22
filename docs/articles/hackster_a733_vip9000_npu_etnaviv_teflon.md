# Freeing the 3.0 TOPS NPU on the Allwinner A733: How an On-Die Neural Accelerator Works, Mainline Linux `etnaviv` + Mesa Teflon Bring-Up, and Zero-Copy Video Processing

**By Tim Michals**  
*Featured for Hackster.io — Open-Source Edge AI, Mainline Linux & Embedded Vision*

---

## 🚀 1. Basic Info & Introduction: What Does an On-Die NPU Actually Do?

If you have ever deployed a computer vision model (like YOLOv8, MobileNetV2, or PoseNet) on an embedded Linux single-board computer, you have felt the wall that general-purpose CPU cores hit when processing real-time 1080p video.

Even on a modern octa-core processor like the **Allwinner A733** (2× Arm Cortex-A76 @ 2.0 GHz + 6× Cortex-A55 @ 1.8 GHz, powering the **Radxa Cubie A7A**) or the **Allwinner T527** (8× Cortex-A55 @ 1.8 GHz, powering the **Radxa Cubie A5E**), running millions of multiply-accumulate operations per frame on the CPU means:
- Burning **3–5 Watts** of CPU thermal budget on matrix math instead of application logic.
- Saturating the CPU's L1/L2 caches and external DRAM bus by repeatedly fetching weights and writing intermediate feature maps.
- Dropping frames whenever the CPU also has to handle camera capture, H.264/H.265 video encoding, or network streaming.

To solve this, silicon designers place a dedicated **Neural Processing Unit (NPU)** right on the SoC die. On the **Allwinner A733** (verified on live **Radxa Cubie A7A** silicon), that NPU is a **3.0 TOPS-class VeriSilicon Vivante VIP9000Nano-DI (`npu@3600000`, `model 0x9000`, `rev 0x9202`, `HW_V83`)**; on the **Allwinner T527**, it is a **2.0 TOPS VeriSilicon Vivante VIP9000Nano-SI+ (`npu@7120000`, `rev 0x9003`)**.

### How Does an NPU Work Compared to a CPU or GPU?

Unlike a CPU (which fetches scalar or small SIMD instructions one by one) or a GPU (which runs general-purpose 32-bit floating-point shaders), a neural network accelerator is a **spatial dataflow engine** purpose-built around four hardware principles:

1. **8-Bit Quantized Tensor Math (`INT8` / `UINT8`)**:
   Neural networks trained in 32-bit floating point (`FP32`) are quantized to 8-bit integers (`INT8` or `UINT8`) using per-tensor or per-channel scale and zero-point parameters. An 8-bit integer multiply-accumulate (**MAC**) circuit takes roughly **15–20× less silicon area and switching energy** than an `FP32` fused multiply-add, allowing the A733 NPU to pack **8 parallel NN cores × 64 MADs/core** (with ZDP3 spatial reuse, delivering up to **3.0 TOPS** marketed peak at high OPP clocks) into a compact silicon footprint.
2. **Spatial Weight & Activation Broadcast Across 8 NN Cores**:
   In a 2D convolution ($3 \times 3$ or $1 \times 1$), the exact same kernel weights slide across thousands of $(x, y)$ pixel locations, while the same input pixel patch is multiplied against dozens of output channels. Instead of reading weights from DRAM over and over, the NPU loads weights and input tiles once into **512 KB of ultra-wide on-chip VIP SRAM** (**128 bytes / 1024 bits per cycle** physical width on the A733) and broadcasts them simultaneously across its **8 Neural Network (NN) cores**.
3. **On-the-Fly Hardware Weight Decompression (Huffman + ZRL)**:
   Neural network weights contain large runs of zeros and clustered small integer values. When a model is loaded, its weights are compressed using **Huffman coding and Zero-Run-Length (ZRL)** encoding. Inside the NPU, a dedicated hardware bitstream decoder decompresses weights as DMA streams them from LPDDR5—cutting external memory bandwidth by **2–4×** with zero CPU overhead.
4. **Fused Hardware Pooling, Activation & Requantization**:
   Before a partial sum ever leaves an NN core, dedicated inline stages apply 32-bit bias addition, non-linear activation (`ReLU`, `ReLU6`, `Clamp`), $2 \times 2$ max or average pooling, and 8-bit requantization in a single pass. On SoCs with a dedicated **Tensor Processor (TP)** (such as the T527), layout transposes and LUT activations run on TP cores; on the A733 `0x9202` (`TP_ENGINE = 0`), non-NN ops fall to the companion **Programmable Shader / PPU** core (`NumShaderCores = 1`) or CPU.

### Allwinner A733 (`0x9202`) vs. Allwinner T527 / A527 (`0x9003`): What Live Silicon Revealed

A common pitfall when reading vendor headers is guessing which `gc_feature_database.h` row matches an SoC without checking silicon. Before booting the A7A, it was easy to assume the A733 carried Allwinner's 12-core `0x9113` (`VIP9000UL-DI+`) block—**live hardware readback on the Radxa Cubie A7A (`/sys/kernel/debug/dri/0/gpu`) proved the A733 silicon is actually `model 0x9000`, `revision 0x9202` (`product_id 0x05090009`, `customer_id 0x1000003b`, `eco_id 0x08000000`, chip date `2023-05-18`) — VeriSilicon `VIP9000Nano-DI` (`HW_V83`, `VIP_VERSION 0x398`)**.

Live silicon register readbacks and VeriSilicon's hardware feature database (`allwinner/gc_feature_database.h`) highlight the exact architectural differences between the A733 and T527:

| Hardware Parameter | Allwinner A733 (Radxa Cubie A7A — Live Silicon) | Allwinner T527 / A527 (Radxa Cubie A5E) | What It Means in Practice |
| :--- | :--- | :--- | :--- |
| **Silicon IP Name** | **VeriSilicon VIP9000Nano-DI (`HW_V83`)** | **VeriSilicon VIP9000Nano-SI+** | Newer `HW_V83` (`VIP_VERSION 0x398`) IP on A733 |
| **Marketed Peak Throughput** | **3.0 TOPS** (`INT8`, 492–1120 MHz OPP) | **2.0 TOPS** (`INT8`) | Higher NN core count & clock scaling on A733 |
| **MMIO Base Address** | `0x03600000` (`4 KB` / `0x1000`) | `0x07120000` (`128 KB`) | Mapped on NPU system bus vs. MCU domain |
| **GICv3 Interrupt** | `GIC_SPI 65` (GICv3 hwirq `97`) | `GIC_SPI 157` | Signals Front-End (`FE`) command completion |
| **Vivante Chip Model** | `0x9000` | `0x9000` | Shared `VIP9000` family ID |
| **Chip Revision (`0x00024`)** | **`0x9202`** (date `2023-05-18`) | **`0x9003`** | Verified directly from A7A `/sys/kernel/debug/dri/0/gpu` |
| **Product / Customer / ECO ID** | `0x05090009` / **`0x1000003b`** / `0x08000000` | `0x05090009` / `0x10000016` / `0x06000000` | Exact lookup key in `allwinner/gc_feature_database.h` |
| **NN Core Count** | **8 NN Cores** (`INT8` & `INT16`) | **6 NN Cores** | 33% more NN cores on A733 |
| **INT8 MADs per NN Core** | 64 MADs/core (`32` lanes/out-cycle) | 64 MADs/core | Paired with `NN_ZDP3 = 1` & up to 1.12 GHz OPP |
| **NN Architecture Generation** | **`HW_V83 = 1`**, `NN_ZDP3 = 1`, `ZRL_8BIT = 0` | V8.0/8.1, `NN_ZDP6 = 1` | A733 adds `NN_TENSOR_ADD_FIELD_MOVE_TO_EXT_CMD = 1` and `NN_SMALL_BATCH_PHASE2 = 1` |
| **VIP SRAM Size & Bus Width** | **512 KB @ 128-Byte Width** (`1024-bit`) | **512 KB @ 64-Byte Width** (`512-bit`) | A733 has **2× wider** internal VIP SRAM bandwidth |
| **TP / PPU Shader Cores** | **`TP_ENGINE = 0`** (`TPEngine_CoreCount = 0`), **1 Shader Core** | **2 TP Cores**, **1 Shader Core** | **Critical finding**: A733 `0x9202` has **no dedicated hardware TP block** — transposes must run on PPU shader or CPU |
| **MMUv2 Page Descriptor (`0x003B4`)** | **`MMU_PAGE_DESCRIPTOR = 1`** | `MMU_PAGE_DESCRIPTOR = 0` | A733 requires `0x003B4` flat-fetch bypass (`0x2` $\rightarrow$ `0x1`) during MMU init |
| **Reset Lines** | **3 resets** (`CORE`, `AXI`, `AHB` in `0x0b04`) | **1 reset** (`RST_BUS_NPU`) | A733 requires multi-reset array control in `etnaviv` |

---

## 🔓 2. Breaking Out of the Proprietary Vendor Trap: Why Mainline `etnaviv` + Mesa Teflon?

Traditionally, using a Vivante VIP9000 NPU on an Allwinner SoC meant accepting a locked-down vendor BSP stack:
* **Kernel Space**: A ~44,000-line out-of-tree kernel module (`vipcore.ko` or `galcore.ko` exposing `/dev/vipcore` or `/dev/galcore`) that breaks on every minor Linux kernel upgrade (`EXTRA_CFLAGS` removal, `in_irq()` deprecation, `MAX_ORDER` rename, `platform_driver` `.remove` signature changes).
* **Userspace**: Precompiled closed-source shared libraries (`libVIPlite.so`, `libVIPuser.so`, or `TIM-VX`) tied to a specific glibc/Android ABI.
* **Offline Model Lock-In**: You cannot load a standard `.tflite` model directly. Instead, you must run a proprietary x86 offline compiler (`acuity-toolkit` / `pegasus`) to convert your model into a locked `.nb` (`network_binary.nb`) blob tailored to a single chip revision.

To run on **mainline Linux 7.1** with zero binary blobs, we replaced the entire vendor stack with upstream open-source components:

| Feature | Legacy Vendor Stack (`vipcore` / `VIPlite`) | Mainline Open-Source Stack (`etnaviv` + Mesa Teflon) |
| :--- | :--- | :--- |
| **Kernel Driver** | Out-of-tree 44K-line `vipcore.ko` / `galcore.ko` | In-tree mainline **`drivers/gpu/drm/etnaviv/`** (`CONFIG_DRM_ETNAVIV=y`) |
| **Device Node & Memory API** | Custom `/dev/vipcore` ioctl ABI | Standard Linux DRM render node **`/dev/dri/renderD128`**, GEM & **`dma-buf` PRIME** |
| **Userspace AI Interface** | Proprietary `libVIPlite.so` / `TIM-VX` blobs | Open-source **Mesa 26.1.8 Teflon** (`libteflon.so`) TFLite external delegate |
| **Model Format** | Proprietary offline-compiled `.nb` binary | **Standard quantized `.tflite` models** compiled on the fly at load time |
| **Multi-Process Safety** | Shared global address space | Per-process **MMUv2 virtual page tables** + **`drm_gpu_scheduler`** hang recovery |

---

## 🔬 3. Inside the Chip: How the A733 NPU Works (Clocks, MMUv2, DMA & 8 NN Cores)

To understand what happens inside the silicon at `0x03600000` when an AI inference job runs—and why specific clock, reset, and MMUv2 registers had to be unlocked for the Allwinner A733—let's trace the internal hardware blocks from the power rail down to the 8 NN cores.

![Allwinner A733 3.0 TOPS NPU Silicon Architecture & DMA Dataflow](media/a733_vip9000_npu_silicon_dma.svg)

### 3.1 Power Domain, 4-Clock CCU Hierarchy & Triple Reset Lines

Before a single register at `0x03600000` responds to CPU reads, three hardware prerequisites must be satisfied in strict order:

1. **Power Supply & Silicon Island (`AXP8191 dcdc2` + `PCK-600 PD_NPU`)**:
   * The external `vdd-gpu-npu` power rail is driven by the **AXP8191 PMIC `dcdc2` buck converter** (`800 mV` at 492–852 MHz boot nominal, scaling up to `960–1080 mV` at 1008–1120 MHz OPP bins).
   * Inside the A733 SoC, the **PCK-600 Power Controller** (`7060000.power-controller`) controls power isolation for the NPU island via domain **`PD_NPU` (id `4`, `pstate=0x8, on`)**. When `etnaviv` calls `pm_runtime_resume_and_get()`, Linux `genpd` powers on `PD_NPU` and releases isolation clamps.
2. **The 4-Clock Tree in Mainline `ccu-sun60i-a733.c`**:
   * **`CLK_NPU` (`0x0b00`, `"core"`)**: The high-speed compute clock driving the 8 NN cores and 512 KB VIP SRAM (`840 MHz` default from `pll-npu`, muxable across `pll-npu`, `pll-periph0-800M/600M/480M`, `pll-ve0`, `pll-ve1`, `pll-de-3x`) with a 5-bit divider (`M`) and gate `BIT(31)`.
   * **`CLK_AHB_NPU` (`0x05c0` bit 6) $\rightarrow$ `CLK_BUS_NPU` (`0x0b04` bit 0, `"reg"`, `200 MHz`)**: On the A733, CPU register reads/writes to `0x03600000` traverse **two series AHB clock gates**: the top-level NPU AHB master gate (`0x05c0` bit 6) and the NPU module bus gate (`0x0b04` bit 0). In `drivers/clk/sunxi-ng/ccu-sun60i-a733.c`, we parented `bus_npu_clk` (`CLK_BUS_NPU`) directly to `ahb_npu_clk` (`CLK_AHB_NPU`). That way, when `etnaviv` enables its `"reg"` clock, the Linux Common Clock Framework automatically enables **both** `0x05c0[6]` and `0x0b04[0]`.
   * **`CLK_MBUS_NPU` (`0x05e0` bit 18, `"bus"`, `600 MHz`)**: Gates the 128-bit AXI/MBUS DMA master port, parented to `mbus_clk` (`600 MHz`, synced with Junhui Liu's upstream A733 CCU `v7` patch series). If `CLK_MBUS_NPU` is left gated, MMIO register reads work fine, but the NPU's DMA engine stalls forever the moment it tries to fetch its first command buffer from LPDDR5!
3. **Triple Reset Lines (`0x0b04` bits 16, 17, 18)**:
   * Unlike the T527 (which has a single `RST_BUS_NPU` bit), A733 register `0x0b04` splits the NPU reset into active-low lines: **`RST_BUS_NPU_CORE` (`BIT(16)`)**, **`RST_BUS_NPU_AXI` (`BIT(17)`)**, **`RST_BUS_NPU_AHB` (`BIT(18)`)** (plus `RST_BUS_NPU_SRAM` at `BIT(19)`, read back as `0x000F0000` deasserted on silicon).
   * Mainline `etnaviv` previously called `devm_reset_control_get_optional()`, which only supports a single reset line. By switching `etnaviv_gpu_platform_probe()` to `devm_reset_control_array_get_optional_exclusive(&pdev->dev)` (and updating `vivante,gc.yaml` to `maxItems: 3`), `etnaviv` cleanly asserts and deasserts all three reset lines as a unit.

### 3.2 The Front-End (FE) Wait-Link DMA Command Parser

How does the NPU execute a multi-layer neural network without CPU intervention between layers?

At the front of the NPU sits the **Front-End (FE) Command Parser**. It is a dedicated DMA state machine that reads 64-bit instructions (`LOAD_STATE`, `END`, `NOP`, `WAIT`, `LINK`, `STALL`) directly from system memory:

* **Wait-Link Ring Buffer**: The kernel maintains a small circular ring buffer in DMA memory ending in a 16-byte **`WAIT` (`200 cycles`) $\rightarrow$ `LINK` (back to `WAIT`)** loop. While the NPU is idle or finishing a job, the FE parser continuously loops between those two instructions.
* **Dynamic Job Chaining**: When userspace submits an inference command stream, the kernel writes a `LINK` instruction at the end of the user buffer pointing back to the kernel's new `WAIT -> LINK` tail, and then overwrites the previous `LINK` target in the active ring to jump into the user's command stream. The FE seamlessly follows the new link on its very next cycle without ever stopping.
* **Why `ETNA_SEC_KERNEL` (`0x003A4`) Is Mandatory**: The VIP9000Nano-DI (`0x9202`) implements Vivante's Security AHB feature (`MINOR_FEATURES7_BIT_SECURITY` + `MINOR_FEATURES10_SECURITY_AHB`). On this hardware, the legacy `VIVS_FE_COMMAND_CONTROL` (`0x00658`) kick register is a no-op. By adding the Allwinner `0x9000` NPU entries (`0x8303`, `0x9003`, `0x9113`, `0x9202`) to the kernel's `etnaviv_hwdb.c`, `etnaviv` enables `gpu->sec_mode = ETNA_SEC_KERNEL`: it unlocks non-secure register writes at `0x003A8`, writes the command buffer address to `VIVS_FE_COMMAND_ADDR` (`0x00654`), and starts the FE via **`VIVS_MMUv2_SEC_COMMAND_CONTROL` (`0x003A4`)**.

### 3.3 MMUv2 Virtual Memory & The A733 `0x003B4` Page Descriptor Handshake

Every memory address inside an NPU job—the command buffer, compressed weights, bias tables, input feature maps, and output tensors—is a **32-bit NPU Virtual Address (`NPU_VA`)**.

1. **Two-Level Page Walk (`PTA` $\rightarrow$ `MTLB` $\rightarrow$ `STLB`)**:
   * The NPU's internal **MMUv2** translates every 32-bit `NPU_VA` into a **40-bit LPDDR5 Physical Address (`PA`)** using a Page Table Array (`PTA` at `0x0038C`/`0x00390`), a 1024-entry Master TLB (`MTLB`, bits `[31:22]`), and 1024-entry Slave TLB page tables (`STLB`, bits `[21:12]`, 4 KB pages).
   * Because the MMU translates scatter-gather physical pages into a contiguous virtual range, large neural network weight buffers do **not** need physically contiguous CMA memory—standard paged Linux GEM buffers work out of the box.
2. **The Revision `0x9202` `MMU_PAGE_DESCRIPTOR` (`0x003B4`) Handshake**:
   * On the T527 (`0x9003`), `etnaviv_iommuv2_restore_sec()` initializes MMUv2 by running a 16-byte physical-mode command buffer on the FE (`load_state(VIVS_MMUv2_PTA_CONFIG)` + `END`) and then writing `0x00388 = 0x1` (`VIVS_MMUv2_SEC_CONTROL_ENABLE`).
   * On the A733 (`0x9202`, and `0x9113`), VeriSilicon enabled hardware feature **`MMU_PAGE_DESCRIPTOR = 1`** controlled by register **`0x003B4`**. Without programming `0x003B4`, the FE stalls when trying to fetch the physical bootstrap command buffer before MMUv2 is turned on.
   * Matching `_FuncExecute_MMU_CMD` in VeriSilicon's hardware HAL, we updated `etnaviv_iommuv2_restore_sec()` in `drivers/gpu/drm/etnaviv/etnaviv_iommu_v2.c` to write **`0x003B4 = 0x2`** (flat physical command-fetch bypass) before kicking the FE PTA bootstrap buffer, wait for FE idle (`0x00004 == 0x7fffffff`), write **`0x003B4 = 0x1`** (Page Descriptor mode active), and then enable `0x00388 = 0x1`. On live A7A silicon, this enabled clean MMUv2 virtual address translation (`IOVA 0x1000` ring, `0x3000` command stream, `0xffff7000..0xffffa000` descriptors and tensors) with zero MMU faults.

### 3.4 Inside the Compute Engines: 8 NN Cores, 1 PPU Shader Core & 512 KB VIP SRAM

Once the FE parses a layer kick (`VIVS_PS_NN_INST_ADDR` at `0x010A0` + `VIVS_PS_UNK10A4` at `0x010A4`), the actual tensor math runs across the internal blocks wired to the **512 KB on-chip VIP SRAM**:

* **8 Parallel NN Cores (`HW_V83`, `VIP_VERSION = 0x398`)**:
  The NN cluster fetches a 128-byte descriptor from `VIVS_PS_NN_INST_ADDR`, streams compressed weights from LPDDR5 into the **512 KB VIP SRAM** (`128-byte` physical width, mapped via `VIVS_GL_OCB_REMAP_START/END`), and executes `64 MADs/core` across all **8 NN cores** in parallel (`32 lanes/output cycle`). On `0x9202`, VeriSilicon enabled **`HW_V83`**, **`NN_SMALL_BATCH_PHASE2 = 1`**, **`NN_TENSOR_ADD_FIELD_MOVE_TO_EXT_CMD = 1`**, and **`ZRL_8BIT = 0`**.
* **1 Programmable Shader / PPU Core (No Dedicated TP Block)**:
  Unlike the T527 (`2 TP cores`), the A733's `0x9202` silicon sets **`TP_ENGINE = 0`** and **`TPEngine_CoreCount = 0`**. All non-NN tensor manipulation (such as `NHWC ↔ NCHW` layout transpose, padding, or custom elementwise activations) must execute on the single **USC / PPU Shader core** (`256 threads`, `64 temp regs`, `512 instructions`) or the Arm CPU—a key architectural difference that surfaced immediately during our live Mesa Teflon bring-up below.

---

## 🧩 4. How It Interfaces: The 4-Layer Open-Source Driver & AI Stack

How does a Python script or C/C++ application loading a standard `.tflite` file end up driving 8 NN cores through `/dev/dri/renderD128`? The software stack is cleanly split into four layers.

![Open-Source NPU Software Stack: TFLite C API -> Mesa Teflon -> Etnaviv Gallium -> Linux DRM](media/etnaviv_teflon_driver_ai_stack.svg)

### 4.1 Layer 1: Application & TensorFlow Lite Runtime (`libtensorflow-lite.so`)

Your application uses the standard **TensorFlow Lite C API** (`<tensorflow/lite/c/c_api.h>`) or Python `tflite_runtime`. You do not link against any proprietary NPU SDK. Instead, you load Mesa's `/usr/lib/libteflon.so` as a standard TFLite external delegate plugin (`tflite_plugin_create_delegate`):

```c
#include <dlfcn.h>
#include <tensorflow/lite/c/c_api.h>

typedef TfLiteDelegate* (*create_delegate_fn)(char**, char**, size_t,
                                              void (*)(const char*));
typedef void (*destroy_delegate_fn)(TfLiteDelegate*);

void run_npu_inference(const char *model_path) {
    /* 1. Dynamically load Mesa's open-source Teflon TFLite delegate */
    void *teflon_so = dlopen("/usr/lib/libteflon.so", RTLD_LAZY);
    create_delegate_fn create_delegate =
        (create_delegate_fn)dlsym(teflon_so, "tflite_plugin_create_delegate");
    destroy_delegate_fn destroy_delegate =
        (destroy_delegate_fn)dlsym(teflon_so, "tflite_plugin_destroy_delegate");

    TfLiteDelegate *npu_delegate = create_delegate(NULL, NULL, 0, NULL);

    /* 2. Attach the NPU delegate to TFLite InterpreterOptions */
    TfLiteModel *model = TfLiteModelCreateFromFile(model_path);
    TfLiteInterpreterOptions *opts = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsAddDelegate(opts, npu_delegate);

    /* 3. Create interpreter: Teflon partitions supported INT8/UINT8 subgraphs */
    TfLiteInterpreter *interp = TfLiteInterpreterCreate(model, opts);
    TfLiteInterpreterAllocateTensors(interp);

    /* 4. Fill input tensor, run on the 3.0 TOPS NPU, read output tensor */
    TfLiteInterpreterInvoke(interp);

    TfLiteInterpreterDelete(interp);
    TfLiteInterpreterOptionsDelete(opts);
    TfLiteModelDelete(model);
    destroy_delegate(npu_delegate);
    dlclose(teflon_so);
}
```

Or in Python (`ctypes` or `tflite_runtime`):
```python
# Or run our built-in zero-dependency verification & benchmark utility:
#   npu-smoke-test /path/to/model_quant.tflite 50
```

### 4.2 Layer 2: Mesa Teflon Delegate Frontend (`src/gallium/frontends/teflon/`)

When `TfLiteInterpreterCreate()` and `TfLiteInterpreterAllocateTensors()` execute:
1. **DRM Render Node Discovery**: Teflon opens `/dev/dri/renderD128` via `pipe_loader_drm_probe_fd()` and instantiates a Gallium `pipe_screen` and `pipe_context` backed by `etnaviv`.
2. **Automatic Graph Partitioning (`Prepare`)**: Teflon walks every node in the `.tflite` flatbuffer graph. Supported quantized (`INT8` / `UINT8`) operators—**`CONV_2D`**, **`DEPTHWISE_CONV_2D`**, **`FULLY_CONNECTED`** (lowered to $1 \times 1$ convolution), **`ADD`**, **`AVERAGE_POOL_2D`**, **`MAX_POOL_2D`**, **`PAD`**, **`LOGISTIC`**, and fused **`RELU` / `RELU6`**—are claimed by the Teflon delegate and merged into contiguous NPU subgraphs. Any unsupported operator (such as a final `SOFTMAX` dequantization node) automatically remains on the Arm CPU.
3. **Lowering to Gallium ML**: Teflon converts the partitioned TFLite subgraph into an array of hardware-agnostic Gallium `struct pipe_ml_operation` descriptors and hands them to `context->ml_subgraph_create()`.

### 4.3 Layer 3: Mesa Etnaviv Gallium ML Backend (`src/gallium/drivers/etnaviv/`)

Inside Mesa's `etnaviv` driver, the neural network subgraph is compiled directly into VIP9000 hardware descriptors, compressed weight bitstreams, and FE command buffers:

1. **Silicon Feature Database Lookup (`allwinner/gc_feature_database.h`)**:
   * At screen creation, Mesa queries the kernel via `DRM_IOCTL_ETNAVIV_GET_PARAM` for `MODEL` (`0x9000`), `REVISION` (`0x9202`), `PRODUCT_ID` (`0x05090009`), `CUSTOMER_ID` (`0x1000003b`), and `ECO_ID` (`0x08000000`).
   * `etna_query_feature_db()` in `src/etnaviv/hwdb/etna_hwdb.c` looks up the exact silicon row in `src/etnaviv/hwdb/allwinner/gc_feature_database.h` and configures the compiler for the A733's **`nn_core_count = 8`**, **`nn_mad_per_core = 64`**, **`on_chip_sram_size = 524288` (`512 KB`)**, **`physical_vip_sram_width = 128` (`128 bytes/cycle`)**, **`nn_zdp3 = true`**, and **`nn_lanes_per_out_cycle = 32`**.
   * *The `TP_ENGINE = 0` Discovery on Live `0x9202` Silicon*: Earlier Mesa patches forced `info->npu.tp_core_count = db->TPEngine_CoreCount ? db->TPEngine_CoreCount : 1;` assuming a single TP unit existed whenever `TPEngine_CoreCount` was `0`. Live silicon testing on the A7A (`0x9202`, where **`gcFEATURE_BIT_TP_ENGINE = 0`** and **`TPEngine_CoreCount = 0`**) proved that the hardware truly has **no TP engine**—`TP` transpose/detranspose ops become silent no-ops, leaving intermediate and final buffers zeroed unless lowered to the PPU shader core or CPU!
2. **One-Time Compilation During `AllocateTensors()` (`etna_ml_subgraph_create`)**:
   * **Hardware Transpose & Channel Reshuffle Insertion (`etnaviv_ml_tp.c`)**: Whenever an input or output tensor layout requires realignment between TFLite's `NHWC` and the NN engine's `NCHW`, Mesa wraps the graph with transpose/detranspose stages (which use hardware TP cores on T527 `0x9003`, and need a PPU shader / CPU fallback on A733 `0x9202`).
   * **V8 Superblock Tiling & Weight Compression (`etnaviv_ml_nn_v8.c`)**: Filter weights and biases are tiled into V8 superblocks matched to **8 NN cores** and the **512 KB VIP SRAM**, compressed into a bitstream, and uploaded into a write-combined GEM buffer object (`coefficients`).
   * **Command Stream Generation (`etnaviv_ml.c`)**: Mesa records the entire sequence of FE register writes (`VIVS_GL_OCB_REMAP_START/END`, `VIVS_GL_NN_CONFIG`, `VIVS_PS_NN_INST_ADDR`, `VIVS_PS_UNK10A4`, and `VIVS_GL_FLUSH_CACHE` barriers).
3. **Zero-Recompile Inference During `Invoke()` (`etna_ml_subgraph_invoke`)**:
   * Every call to `TfLiteInterpreterInvoke()` simply updates the input tensor BO, submits the pre-compiled command stream via `DRM_IOCTL_ETNAVIV_GEM_SUBMIT`, and waits on the hardware fence via `DRM_IOCTL_ETNAVIV_WAIT_FENCE`.

### 4.4 Layer 4: Mainline Linux 7.1 `etnaviv` DRM Kernel Driver (`drivers/gpu/drm/etnaviv/`)

In kernel space, `etnaviv` provides:
* **Unprivileged Compute Access (`/dev/dri/renderD128`)**: AI applications run as normal non-root users via the DRM render node.
* **Per-Process MMUv2 Isolation & `dma-buf` PRIME Import**: Every process gets isolated MMUv2 page tables, and camera/video buffers from V4L2 or G2D can be imported directly into NPU virtual memory via `DRM_IOCTL_PRIME_FD_TO_HANDLE`.
* **DRM GPU Scheduler (`drm_gpu_scheduler`) & Runtime PM**: Fair job scheduling, hardware hang detection with automatic triple-reset recovery, and runtime power gating of `PD_NPU` when idle.

---

## 🎥 5. Zero-Copy Video Processing: Connecting MIPI CSI-2, ISP 602, Hardware H.264/H.265 VE, and the 3.0 TOPS NPU

In a real vision application—an autonomous drone, agricultural sprayer, security camera, or industrial inspection rig—the NPU does not live in isolation. It consumes live video frames from a camera while a video encoder simultaneously records or streams those frames over RTSP/WebRTC.

If the CPU had to copy every 1080p (`1920 × 1088`) `NV12` frame (**3.13 MB/frame**, or **~94 MB/s** at 30 fps) between the camera driver, the H.265 video encoder, and the NPU preprocessor, `memcpy()` and cache flushes alone would saturate an entire CPU core.

On the **Allwinner A733 (Radxa Cubie A7A)**, we eliminate CPU frame copies by passing **Linux `dma-buf` file descriptors** allocated in shared LPDDR5 CMA memory directly across all hardware engines:

![Allwinner A733 Zero-Copy Heterogeneous Vision, NPU Inference & Video Encoding Pipeline](media/a733_zero_copy_vision_npu_ve_pipeline.svg)

### 5.1 Stage 1: Photon to Shared `NV12` `dma-buf` (`0% CPU Pixel Math`)

1. **MIPI CSI-2 Sensor (`OV5647`) & D-PHY (`0x05800000`)**: The camera sensor streams 10-bit Raw Bayer (`SRGGB10_1X10`) at `1920 × 1080 @ 30 fps` over a 2-lane MIPI CSI-2 link into the A733 combo D-PHY and CSI0 receiver (`0x05811000`).
2. **In-Line Hardware ISP 602 (`0x05809400`, `sun60i-isp`)**: Running in on-the-fly (`online`) mode directly from the CSI parser FIFO at `~148.5 Mpixels/s`, the **Hardware ISP 602** performs black-level correction, lens shading, 2D noise reduction, Bayer demosaicing, color correction (`CCM`), gamma, and RGB-to-`NV12` color space conversion in dedicated silicon (`0% CPU`). Userspace `aw-isp-2a` runs only once per frame (30 Hz) to read compact 3A histogram statistics and update sensor exposure/gain over I²C.
3. **CSI DMA0 (`0x05813000`, `/dev/video0`)**: Writes stride-aligned `1920 × 1088` planar **`NV12`** frames (`Y` luma + interleaved `UV` chroma) directly into contiguous LPDDR5 CMA ring buffers (`vb2-dma-contig`) and exports each buffer as a **`dma-buf` PRIME file descriptor** via `VIDIOC_EXPBUF`.

### 5.2 Stage 2: Parallel Zero-Copy Fan-Out to Hardware Video Encoder, 3.0 TOPS NPU & RISC-V

When V4L2 dequeues a completed camera frame (`VIDIOC_DQBUF`), userspace hands the **`dma-buf` FD** to three hardware blocks in parallel **without copying the 3.13 MB frame on the CPU**:

1. **Branch A — Hardware H.264 / H.265 Video Encoding (`ve@1c10000`, `< 1.6% CPU`)**:
   * The V4L2 `dma-buf` FD is imported directly into the **Allwinner A733 Video Engine (`ve@1c10000`, `VE300` / `V3102_0016`)** with `VE_BUFFER_FLAG_DMA_BUF`.
   * The VE's internal hardware **`CSI` / `ISP` preprocessor (`+0xA00`)** and **`AVC` (`+0xB00`) / `HEVC` (`+0xC00`) encoder sub-engines** read the `Y` and `UV` planes directly from the camera CMA buffer over the 128-bit MBUS DMA port (`CLK_MBUS_VE` at 600 MHz).
   * **Measured Hardware Performance on Radxa Cubie A7A**:
     * **1080p30 H.264 (AVC) Hardware Encoding**: **3.09 ms/frame** (**323.5 fps** max throughput), **0.89% total system CPU usage**.
     * **1080p30 H.265 (HEVC) Hardware Encoding**: **6.34 ms/frame** (**157.8 fps** max throughput), **1.55% total system CPU usage**.
2. **Branch B — 3.0 TOPS-Class VIP9000Nano-DI NPU Inference (`npu@3600000`)**:
   * Simultaneously, the NPU processes the detection tensor (`e.g. 640 × 640 × 3` or `320 × 320 × 3` `UINT8`/`INT8`).
   * Using Allwinner's **G2D 2D hardware blitter** (`g2d@1480000` — hardware YUV-to-RGB color conversion + bilinear downscaler accepting the source V4L2 `dma-buf` FD and writing directly into an `etnaviv` GEM `dma-buf` BO via `drmPrimeFDToHandle()`), or a fast NEON stride-gather into the write-combined Teflon input BO, the input tensor is mapped into the NPU's **MMUv2 (`0x00388`)** page table.
   * Calling `TfLiteInterpreterInvoke()` runs the pre-compiled **Teflon command stream** across the **8 NN cores** and **512 KB VIP SRAM**, outputting bounding boxes, class IDs, and confidence scores while the Arm cores remain largely idle.
3. **Branch C — Hard Real-Time Actuation on the On-Die 200 MHz RISC-V Core (`rproc@7032000`)**:
   * When the NPU detects a target $(\Delta x, \Delta y, \text{class}, \text{confidence})$, Linux writes a 16-byte setpoint into **System SRAM A2 (`0x00044000`)** and rings the **Hardware MSGBOX0 (`0x07033000`)** doorbell.
   * Within **14.47 µs**, the on-die **200 MHz XuanTie E902 RISC-V coprocessor** updates its bare-metal PID / motor control loop with zero Linux scheduling jitter (see our companion Hackster.io guide on [Unlocking the On-Die 200 MHz XuanTie RISC-V Core](hackster_a733_e902_remoteproc.md)).

---

## 🛠️ 6. Step-by-Step Implementation & Board Verification

Here is the exact Device Tree, Kernel, Buildroot, and verification setup used on the **Radxa Cubie A7A (Allwinner A733)** in this repository.

### 6.1 Device Tree Node (`sun60i-a733-cubie-a7a.dts`)

In `arch/arm64/boot/dts/allwinner/sun60i-a733-cubie-a7a.dts`, we define the NPU node at `0x03600000` with its 3 clocks (`bus`, `core`, `reg`), 3 reset lines (`core`, `axi`, `ahb`), PCK-600 power domain (`PD_NPU`), and the AXP8191 `dcdc2` (`vdd-gpu-npu`) regulator:

```dts
npu: npu@3600000 {
    compatible = "vivante,gc";
    reg = <0x03600000 0x1000>;
    interrupts = <GIC_SPI 65 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&ccu CLK_MBUS_NPU>,
             <&ccu CLK_NPU>,
             <&ccu CLK_BUS_NPU>;
    clock-names = "bus", "core", "reg";
    resets = <&ccu RST_BUS_NPU_CORE>,
             <&ccu RST_BUS_NPU_AXI>,
             <&ccu RST_BUS_NPU_AHB>;
    reset-names = "core", "axi", "ahb";
    power-domains = <&pck600 PD_NPU>;
    #cooling-cells = <2>;
};
```

### 6.2 Summary of the 5 Mainline Kernel & Mesa Fixes for Allwinner A733 (`0x9202`)

| Component | File | Silicon Requirement on Allwinner A733 (`0x9202`) | Engineering Fix Implemented |
| :--- | :--- | :--- | :--- |
| **1. A733 CCU Clock Parenting** | `drivers/clk/sunxi-ng/ccu-sun60i-a733.c` | Register access to `0x03600000` requires both `CLK_AHB_NPU` (`0x05c0` bit 6) and `CLK_BUS_NPU` (`0x0b04` bit 0), plus `CLK_MBUS_NPU` (`0x05e0` bit 18) for AXI DMA. | Parented `bus_npu_clk` (`CLK_BUS_NPU`) directly to `ahb_npu_clk` (`CLK_AHB_NPU`), added `CLK_SET_RATE_PARENT` to `npu_clk`, and synced `ccu-sun60i-a733.c` with upstream `v7`. |
| **2. Triple NPU Reset Lines** | `drivers/gpu/drm/etnaviv/etnaviv_gpu.c` & `vivante,gc.yaml` | A733 `npu@3600000` splits resets in `0x0b04` (`RST_BUS_NPU_CORE` bit 16, `AXI` bit 17, `AHB` bit 18), while `etnaviv` only fetched a single reset. | Switched `etnaviv_gpu_platform_probe()` to `devm_reset_control_array_get_optional_exclusive()` and updated `vivante,gc.yaml` to `maxItems: 3`. |
| **3. Kernel `etnaviv` HWDB (`ETNA_SEC_KERNEL`)** | `drivers/gpu/drm/etnaviv/etnaviv_hwdb.c` | Without `0x9000` entries in kernel `etnaviv_hwdb.c`, `sec_mode` remained `ETNA_SEC_NONE` and `etnaviv_gpu_start_fe()` never wrote `0x003A4` (`SEC_COMMAND_CONTROL`). | Added all 4 Allwinner `0x9000` NPU revisions (`0x8303`, `0x9003`, `0x9113`, and verified A733 `0x9202`) with `MINOR_FEATURES7_BIT_SECURITY` and `MINOR_FEATURES10_SECURITY_AHB`. |
| **4. MMUv2 `0x003B4` Page Descriptor Handshake** | `drivers/gpu/drm/etnaviv/etnaviv_iommu_v2.c` | Revision `0x9202` sets `MMU_PAGE_DESCRIPTOR = 1`; without configuring `0x003B4`, the FE bootstrap PTA load stalls before MMUv2 is enabled. | Set `0x003B4 = 0x2` (flat physical command-fetch bypass) before kicking the FE PTA bootstrap buffer, and `0x003B4 = 0x1` after FE idle. |
| **5. Mesa 26.1.8 Allwinner HWDB** | `src/etnaviv/hwdb/etna_hwdb.c` & `allwinner/gc_feature_database.h` | Upstream Mesa 26.1.8 lacked Allwinner's `0x9000` feature database rows (`0x9003`, `0x9113`, `0x9202`). | Added `allwinner/gc_feature_database.h` to Mesa 26.1.8 so Teflon recognizes `0x9000 rev 0x9202` (`nn_core_count = 8`, `on_chip_sram_size = 512 KB`). |

### 6.3 Live Silicon Verification on Radxa Cubie A7A (What Passed & Two Real `HW_V83` Teflon Findings)

Booting our mainline Linux `7.1.0 PREEMPT_RT` Buildroot image on the **Radxa Cubie A7A** confirmed full kernel bring-up on the very first try:

```sh
# 1. Verified live kernel boot log on Radxa Cubie A7A:
dmesg | grep -iE "etnaviv|npu"
#   [    0.111755] sunxi-pck-600 7060000.power-controller: domain "NPU" (id 4) pstate=0x8, on
#   [    0.125867] etnaviv etnaviv: bound 3600000.npu (ops gpu_ops)
#   [    0.125975] etnaviv-gpu 3600000.npu: model: GC9000, revision: 9202
#   [    0.125986] etnaviv-gpu 3600000.npu: etnaviv has been instantiated on a NPU, for which the UAPI is still experimental
#   [    0.126585] [drm] Initialized etnaviv 1.4.0 for etnaviv on minor 0

# 2. Verified live silicon identity & clocks via debugfs:
mount -t debugfs none /sys/kernel/debug
cat /sys/kernel/debug/dri/0/gpu
#   model: 0x9000 | revision: 0x9202 | product_id: 0x5090009
#   customer_id: 0x1000003b | eco_id: 0x8000000
#   shader_core_count: 1 | nn_core_count: 8 | thread_count: 256
#   (pll-npu / npu = 840 MHz, mbus-npu = 600 MHz, bus-npu = 200 MHz, vdd-gpu-npu = 800 mV)
```

Running `/usr/bin/npu-smoke-test` with `ETNA_MESA_DEBUG=ml_msgs,dump_shaders,npu_no_batching` and decoding the Etnaviv devcoredump (`/sys/class/devcoredump/devcd1/data`) gave us an immediate, crystal-clear X-ray of what already works in silicon and the exact two Mesa 26.1.8 userspace gaps remaining for `0x9202` (`HW_V83`):

#### ✅ Verified Working End-to-End on A733 Silicon
* **Power, Clocks, Resets & DRM Node**: `PD_NPU` (domain 4), `AXP8191 dcdc2` (`800 mV`), `pll-npu` (`840 MHz`), `mbus-npu` (`600 MHz`), `bus-npu` (`200 MHz`), all 3 resets (`0x02002b04 = 0x000F0000`), and `/dev/dri/renderD128` probe cleanly.
* **MMUv2 + FE Command Fetch + Interrupts + NN DMA Writeback**: `init_npu()` and `quantized_conv2d` execute on `3600000.npu` with **zero MMU faults** (`REG 0x0184 = 0x0`) and **zero hangs**, completing each Conv2D dispatch in **`0.11 ms`**, firing **`GIC_SPI 65` (GICv3 hwirq 97)** cleanly (`/proc/interrupts` increments on every job), and DMA-writing back into LPDDR5.

#### 🔧 Two Real-Silicon Mesa 26.1.8 Teflon Gaps Uncovered on `0x9202` (`HW_V83`)
1. **No Hardware TP Engine (`TP_ENGINE = 0`, `TPEngine_CoreCount = 0`)**:
   Per-op buffer dumps (`mesa-input-000` $\rightarrow$ `mesa-output-000` $\rightarrow$ `mesa-output-001` $\rightarrow$ `mesa-output-002`) proved that `0 TP` (NHWC $\rightarrow$ NCHW transpose) and `2 TP` (NCHW $\rightarrow$ NHWC detranspose) are silent no-ops on `0x9202`, leaving their output buffers all zeros while `1 NN` ran on the zeroed tensor and DMA-wrote `128` (the quantized output zero-point) into DDR. Upstream Mesa `etnaviv_ml` needs a **PPU shader or CPU transpose fallback** when `TP_ENGINE == 0`.
2. **Vivante `HW_V83` (`VIP_VERSION = 0x398`) NN Descriptor & Coefficient Encoding**:
   * On `quantized_add`, the devcoredump showed FE cleanly fetched and returned to its Wait-Link loop (`FE_DMA_ADDRESS = 0x10b0`), while the NN engine stayed busy (`IDLE = 0x7ffbfff6`, bit 18 NN active) — directly explained by `0x9202` setting **`gcFEATURE_BIT_NN_TENSOR_ADD_FIELD_MOVE_TO_EXT_CMD = 1`** (moving tensor-add fields into an extended NN command that Mesa's NN v8.0 backend does not yet emit).
   * On `quantized_conv2d`, the NN job completed cleanly in `0.11 ms` and wrote `128` across `ch0` and part of `ch1`, confirming that Mesa 26.1.8's NN v8.0 descriptor and `ZRL_8BIT` weight compressor (`etnaviv_ml_nn.c` / `etnaviv_ml_nn_v8.c`) need **`HW_V83` (`ZRL_8BIT = 0`, `NN_SMALL_BATCH_PHASE2 = 1`)** descriptor/compression support before numerical outputs match CPU byte-for-byte.

This makes the Radxa Cubie A7A an ideal open-source bring-up target: the **entire mainline Linux kernel `etnaviv` stack (DT, CCU clocks, triple resets, `ETNA_SEC_KERNEL`, `0x003B4` MMUv2 Page Descriptor, IRQ 65, and DMA) is already live on silicon**, and the exact two Mesa `etnaviv_ml` userspace work items (`TP_ENGINE = 0` fallback and `HW_V83` descriptor/compression) are now cleanly isolated for upstream Mesa development.

---

## 🔗 Related Guides & Source Repositories

- **Mainline Linux Kernel Tree**: [linux-cubie on GitHub](https://github.com/tcmichals/linux-cubie) (branch `a7a_remoteproc_camera_usb`)
- **Buildroot, Mesa Patches, Firmware & Tooling**: [cubie-a5e on GitHub](https://github.com/tcmichals/cubie-a5e)
- **NPU Acceleration Subsystem Guide**: [docs/common/NPU_ACCELERATION_GUIDE.md](../common/NPU_ACCELERATION_GUIDE.md)
- **Camera & V4L2 ISP 602 Guide**: [docs/common/CAMERA_V4L2_GUIDE.md](../common/CAMERA_V4L2_GUIDE.md)
- **A733 Hardware Video Encoder Guide**: [docs/common/A733_VE_ENCODER_GUIDE.md](../common/A733_VE_ENCODER_GUIDE.md)
- **Companion Hackster.io Article (On-Die 200 MHz RISC-V Core)**: [Turning an 8-Core Desktop Processor into a Hard Real-Time Robotics & Industrial Engine](hackster_a733_e902_remoteproc.md)
