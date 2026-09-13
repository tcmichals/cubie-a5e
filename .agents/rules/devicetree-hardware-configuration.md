# Device Tree Driven Hardware Configuration & Test Parity Rule

This rule strictly governs all Linux kernel driver development, Devicetree binding schemas, and target hardware testing across the **Cubie** project repository (`cubie-a5e`):

---

## 1. Strict Ban on Hardcoded Memory Layouts in Kernel Drivers
* **FORBIDDEN:** Drivers targeting upstream acceptance (including `sunxi_rproc.c` and `sun55i-msgbox.c`) must NEVER hardcode physical memory addresses, SRAM slice offsets, carveout names, or DMA window boundaries in C source code.
* **MANDATORY:** All memory topologies (on-chip SRAM banks, DDR DMA carveouts, vring buffers, and trace buffers) MUST be declared via standard Devicetree nodes:
  - In `reserved-memory`: nodes with `compatible = "shared-dma-pool"` and `no-map`.
  - In consumer nodes (`&rproc`): referenced via `memory-region = <&...>`.
* **Standard Kernel API Usage:** Drivers must parse memory regions dynamically using:
  - `of_count_phandle_with_args(np, "memory-region", NULL)`
  - `of_parse_phandle(np, "memory-region", index)`
  - `of_reserved_mem_lookup(rm_np)`
  - `rproc_of_resm_mem_entry_init(dev, index, size, base, name)`

---

## 2. Devicetree YAML Schema to Driver Parity
* **1-to-1 Schema Parity:** Every property, name list, and optional feature declared in an upstream YAML binding schema (`Documentation/devicetree/bindings/...`) MUST have an active, working implementation in the corresponding C driver.
* **No Phantom Bindings:** If the YAML schema defines `memory-region-names` (e.g., `"vram"`, `"dram"`, `"trace"`) or `interrupt-names` (`"crash"`), the driver MUST explicitly query and handle those names with `of_property_read_string_index()`. Never submit a driver that relies on generic fallback stubs while the YAML schema promises named region parsing.

---

## 3. Test Validity: Hardware Configuration Requires Matching DT Overlay & Reboot
* **No Unaligned Testing:** A hardware benchmark or driver test that changes memory layouts (e.g., switching between DDR VirtIO, on-chip SRAM VirtIO, and Userspace UIO) is **STRICTLY INVALID** if performed by simply swapping firmware binaries (`.elf`) on a static Device Tree.
* **Mandatory Overlay Configuration:**
  - Hardware modes must be selected via Device Tree Overlays (`.dtbo`) configured in `/boot/config.txt` (`dtoverlay=...`).
  - The target board MUST undergo a clean reboot (`reboot`) to allow U-Boot to merge the overlay and the Linux kernel to bind the matching memory region.
* **Pre-Test Verification Gate:** Before running any benchmark or logging metrics:
  1. Inspect `/sys/firmware/devicetree/base/...` to confirm the active `memory-region` matches the test scope.
  2. Inspect `dmesg` to confirm the kernel bound the intended DMA pool without address mismatch warnings.
* **Documentation:** All manual test steps, required `/boot/config.txt` overlay settings, and verification commands must be explicitly documented in `riscv-firmware/tests.md`.
