# NPU & TinyML Acceleration Guide

> **Canonical Guide**: All NPU documentation—including T527 (Cubie A5E) and A733 (Cubie A7A) hardware specs, Buildroot/Mesa Teflon configuration, Python/C++ TFLite delegate examples, standalone PPU compute shaders, and zero-copy V4L2/CMA `dma-buf` PRIME examples—has been unified into **[docs/common/NPU_ACCELERATION_GUIDE.md](../common/NPU_ACCELERATION_GUIDE.md)**.

## Quick Buildroot Summary
* `CONFIG_DRM_ETNAVIV=m` (or `y`) in `linux.config`
* `BR2_PACKAGE_MESA3D=y` and `BR2_PACKAGE_MESA3D_GALLIUM_DRIVER_ETNAVIV=y`
* `BR2_PACKAGE_MESA3D_TEFLON=y` (`/usr/lib/libteflon.so`)
* `BR2_PACKAGE_TENSORFLOW_LITE=y` (`/usr/lib/libtensorflow-lite.so`)

See **[NPU_ACCELERATION_GUIDE.md](../common/NPU_ACCELERATION_GUIDE.md)** for full validation commands and copy-pasteable examples.
