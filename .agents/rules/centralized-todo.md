# Centralized Single-Source TODO Rule

This rule strictly governs all task management, feature roadmaps, and progress tracking across the **Cubie** project repository (`cubie-a5e`):

---

## 1. Single Authoritative Source of Truth
* **Exclusive Central File:** All task lists, hardware bring-up roadmaps, driver milestones, and testing TODOs MUST reside in the single root file:
  `TODO.md` (at `/home/tcmichals/projects/cubie/cubie-a5e/TODO.md`).
* **Strict Ban on Nested / Secondary TODO Files:**
  * ❌ NEVER create or recreate `TODO.md` in any subdirectory (e.g. `riscv-firmware/TODO.md`, `docs/platforms/*_TODO.md`).
  * ❌ NEVER create fragmented board-specific files (e.g. `TODO_A5E.md`, `TODO_A7A.md`).
  * ❌ All tasks across all co-processors (E907, E902, HiFi4 DSP), video/camera encoding (VPU/Cedrus), NPU (Vivante/Etnaviv), kernel drivers, USB, and Ethernet must be kept together in root `TODO.md`.

---

## 2. Multi-Board Structural Organization
Within root `TODO.md`, maintain clear, segregated parts for each hardware target:
* **Executive Dashboard & Navigation Matrix**: High-level cross-board status table at the top.
* **Part I: Radxa Cubie A5E (Allwinner A527 / T527)**:
  * Verified Silicon Milestones
  * Camera & Video Encoding (VPU / Cedrus)
  * NPU Acceleration (Vivante VIP9000 / Etnaviv / Teflon)
  * XuanTie E907 Advanced RemoteProc & IPC (Restart after crash, Suspend/Resume, SRAM VirtIO, Multi-channel stress, HiFi4 DSP)
  * Upstream Linux Kernel Submission
* **Part II: Radxa Cubie A7A (Allwinner A733)**:
  * Hardware Bring-Up & Current Status
  * USB & Power Subsystem (FE1.1S Hub & AIC8800 Wi-Fi 6)
  * Ethernet Subsystem (GMAC210 TX DMA Watchdog resolution)
  * XuanTie E902 RemoteProc Dual-Mode Architecture
  * Mandatory Patch Gate & Clean Buildroot Validation

---

## 3. Maintenance Discipline
* When any task is completed or new priorities emerge, update root `TODO.md` immediately.
* Do not leave scratch notes or ad-hoc checklists in documentation folders or firmware trees.
