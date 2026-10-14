# DeviceTree Binding Schema Lore & Peer Patterns Guide

This document analyzes upstream DeviceTree binding rules, review critiques from **Krzysztof Kozlowski**, **Rob Herring**, and **Conor Dooley**, and evolutionary bug fixes on peer coprocessor and mailbox schemas from `lore.kernel.org`.

---

## 1. Upstream Maintainer Rules for DT Bindings

### Rule 1: Never Redefine Types for Core Properties
- **Commit Reference**: `74eb6cd91aef` ("dt-bindings: remoteproc: qcom: Drop types for firmware-name")
- **Reviewer**: **Krzysztof Kozlowski <krzysztof.kozlowski@oss.qualcomm.com>**
- **The Trap**:
  Specifying `$ref: /schemas/types.yaml#/definitions/string` or `string-array` on standard core properties like `firmware-name` is redundant and flagged by maintainers. Core schemas (`dt-schema`) already define the type.
- **The Upstream Standard**:
  Constrain only the number of expected items:
  ```yaml
  firmware-name:
    maxItems: 1
    description: Default firmware file name in /lib/firmware
  ```

---

### Rule 2: Rigid Positional Items (No `enum:` Permutations)
- **Maintainer Critique (v1 / v2)**:
  > *"Nope, this cannot be flexible. Please open existing code to see how this is done... So you completely ignored my feedback and sent exactly the same."* — Krzysztof Kozlowski
- **The Trap**:
  Using `items: enum: [cfg, r_sram, r_sram1, remap]` allows invalid or scrambled hardware trees (e.g. `reg-names = "remap", "cfg"`).
- **The Upstream Standard**:
  Hardware wiring is physically invariant. Always use an ordered, positional list with `- const:`:
  ```yaml
  reg-names:
    items:
      - const: cfg
      - const: r_sram
      - const: r_sram1
      - const: remap
  ```

---

### Rule 3: Positional Pairing of `memory-region` and `memory-region-names`
- **Commit Reference**: `479ba9d293f5` ("dt-bindings: remoteproc: k3-r5f: Add memory-region-names")
- **Maintainer Signed-off-by**: **Mathieu Poirier <mathieu.poirier@linaro.org>**
- **The Rule**:
  When a coprocessor driver supports multiple memory regions (e.g. DMA pool, code DRAM, trace buffer), each index in `memory-region` must be explicitly named in `memory-region-names` using positional `- const:` definitions:
  ```yaml
  memory-region:
    minItems: 1
    items:
      - description: Virtio vring and buffer pool (DMA pool)
      - description: Firmware code and data in DRAM
      - description: Trace diagnostic buffer

  memory-region-names:
    minItems: 1
    items:
      - const: vram
      - const: dram
      - const: trace
  ```

---

### Rule 4: Clean Patch Subject Lines
- **Maintainer Directive**: Krzysztof Kozlowski (KK4)
- **The Rule**:
  Drop redundant words like `"schema"` or `"binding"` from patch subjects when the subsystem prefix already establishes context:
  - ❌ `dt-bindings: remoteproc: add Allwinner sun55i-a523-rproc schema`
  - ❌ `dt-bindings: mailbox: add Allwinner sun55i-a523-msgbox binding`
  - ✅ `dt-bindings: remoteproc: add Allwinner sun55i-a523-rproc`
  - ✅ `dt-bindings: mailbox: add Allwinner sun55i-a523-msgbox`

---

### Rule 5: No Driver Prose in Hardware Descriptions
- **Maintainer Directive**: Krzysztof Kozlowski (KK9)
- **The Trap**:
  Describing how Linux software processes the property (e.g. *"Interrupt used by the driver's workqueue to signal vring arrival"*).
- **The Upstream Standard**:
  DeviceTree describes the hardware component and physical signals, independent of the operating system:
  - ❌ *"Used by Linux workqueue for mailbox notification"*
  - ✅ *"Hardware Message Box interrupt line for the Cortex-A55 host bank"*

---

### Rule 6: Clean Header Includes in Examples
- **Maintainer Directive**: Rob Herring (`robh@kernel.org`)
- **The Trap**:
  Using raw hex magic numbers or macros that are not defined in upstream `<dt-bindings/...>` headers causes `make dt_binding_check` to fail.
- **The Upstream Standard**:
  Always include the exact binding header files:
  ```yaml
  examples:
    - |
      #include <dt-bindings/clock/sun55i-a523-mcu-ccu.h>
      #include <dt-bindings/power/allwinner,sun55i-a523-ppu.h>
      #include <dt-bindings/reset/sun55i-a523-mcu-ccu.h>
  ```

---

## 2. Peer DT Schema Comparative Benchmark

| Checkpoint | `allwinner,sun55i-a523-rproc.yaml` | `st,stm32-rproc.yaml` | `fsl,imx-rproc.yaml` | `ti,k3-r5f-rproc.yaml` |
| :--- | :--- | :--- | :--- | :--- |
| **Filename = Compatible** | `allwinner,sun55i-a523-rproc.yaml` matches primary compatible | `st,stm32-rproc.yaml` matches compatible | `fsl,imx-rproc.yaml` matches compatible | `ti,k3-r5f-rproc.yaml` matches compatible |
| **`reg-names` Order** | Rigid positional `- const:` | Positional list | Positional list | Positional list |
| **`mboxes` Channels** | Single channel (`maxItems: 1`) | Multi-channel (`minItems: 1`) | Dual channel | Dual channel |
| **`power-domains`** | Positional (`riscv`, `sram`) | Optional list | Optional single | Power domain list |
| **`additionalProperties`** | `false` | `false` | `false` | `unevaluatedProperties: false` (inherits) |
| **`make dt_binding_check`**| Exit code 0 (clean) | Exit code 0 | Exit code 0 | Exit code 0 |
