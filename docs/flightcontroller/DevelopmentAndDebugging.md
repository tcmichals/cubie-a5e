# C/C++ Development and Debugging Guide

This document describes how to compile C/C++ flight applications using the Buildroot toolchain and set up a seamless, automated compile-deploy-debug loop within VS Code / Antigravity.

---

## 1. Toolchain & Sysroot Locations

When Buildroot completes a build, it packages a cross-compilation toolchain and a Target Sysroot (containing headers and libraries for the target board) inside the output folder:

* **Cross-Compiler Toolchain:**
  * C Compiler: `bld/host/bin/aarch64-buildroot-linux-gnu-gcc`
  * C++ Compiler: `bld/host/bin/aarch64-buildroot-linux-gnu-g++`
  * Debugger (GDB): `bld/host/bin/aarch64-buildroot-linux-gnu-gdb`
* **Target Sysroot:**
  * Path: `bld/host/aarch64-buildroot-linux-gnu/sysroot/`
  * Contains `/usr/include`, `/usr/lib`, and compiled libraries (OpenCV, TFLite, etc.) matching the target image.

---

## 2. VS Code Task Automation (`tasks.json`)

To automate building and transferring your executable to the board, create `.vscode/tasks.json` in your workspace.

This configuration defines three tasks:
1. **Build Application:** Compiles the C++ source file using the Buildroot toolchain.
2. **Deploy Application:** Copies the binary to the board using `scp`.
3. **Spawn GDBServer:** Runs `gdbserver` on the target board over SSH to listen for connections.

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Flight App",
            "type": "shell",
            "command": "${workspaceFolder}/bld/host/bin/aarch64-buildroot-linux-gnu-g++",
            "args": [
                "-g",
                "${workspaceFolder}/flight_app.cpp",
                "-o",
                "${workspaceFolder}/build/flight_app",
                "--sysroot=${workspaceFolder}/bld/host/aarch64-buildroot-linux-gnu/sysroot",
                "-lpthread"
            ],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": ["$gcc"]
        },
        {
            "label": "Deploy to Board",
            "type": "shell",
            "command": "scp",
            "args": [
                "${workspaceFolder}/build/flight_app",
                "rock@192.168.1.100:/home/rock/flight_app"
            ],
            "dependsOn": "Build Flight App",
            "problemMatcher": []
        },
        {
            "label": "Spawn gdbserver",
            "type": "shell",
            "command": "ssh",
            "args": [
                "rock@192.168.1.100",
                "gdbserver :1234 /home/rock/flight_app"
            ],
            "isBackground": true,
            "dependsOn": "Deploy to Board",
            "problemMatcher": {
                "pattern": [
                    {
                        "regexp": ".",
                        "file": 1,
                        "location": 2,
                        "message": 3
                    }
                ],
                "background": {
                    "activeOnStart": true,
                    "beginsPattern": "Process /home/rock/flight_app created",
                    "endsPattern": "Listening on port 1234"
                }
            }
        }
    ]
}
```

---

## 3. VS Code Launch Configuration (`launch.json`)

To debug the application with source-level breakpoints, create a `.vscode/launch.json` configuration. 

This connects the host-side GDB to the board's running `gdbserver` and maps the target libraries to the Buildroot sysroot:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug Flight App (Target GDB)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/flight_app",
            "miDebuggerServerAddress": "192.168.1.100:1234",
            "miDebuggerPath": "${workspaceFolder}/bld/host/bin/aarch64-buildroot-linux-gnu-gdb",
            "cwd": "${workspaceFolder}",
            "externalConsole": false,
            "MIMode": "gdb",
            "preLaunchTask": "Spawn gdbserver",
            "setupCommands": [
                {
                    "description": "Set Sysroot to local Buildroot host sysroot",
                    "text": "set sysroot ${workspaceFolder}/bld/host/aarch64-buildroot-linux-gnu/sysroot",
                    "ignoreFailures": false
                },
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                }
            ]
        }
    ]
}
```

---

## 4. How to Use the Debug Loop

1. **Set a Breakpoint:** Open your C++ source file (e.g. `flight_app.cpp`) in your IDE and set breakpoints on the lines you wish to inspect.
2. **Start Debugging:** Press **F5** (or click the Play icon in the Run & Debug view).
3. **Execution Sequence:**
   * VS Code runs the prelaunch task `Spawn gdbserver`.
   * This builds the latest binary, copies it to the board via `scp`, and launches `gdbserver` listening on port `1234`.
   * VS Code then launches the local `aarch64-buildroot-linux-gnu-gdb` debugger, attaches it to the remote board, and stops execution at your breakpoint.
4. **Control Flow:** You can now step through code, inspect variables, and monitor registers directly from the IDE.

---

## 5. Real-Time Linux & Core Isolation Optimizations

To run the custom **Linux port of iNAV** with low-latency and microsecond-level loop-time determinism under the `PREEMPT_RT` patched kernel, the system must be configured to isolate a CPU core, disable dynamic frequency scaling, lock all memory pages in RAM, and pre-fault stacks.

### A. OS-Level Core Isolation (Boot Args)

By default, the Linux OS scheduler schedules tasks across all cores. To prevent standard OS interrupts and user space processes from running on Core 7 (leaving it exclusively for iNAV), pass the `isolcpus` argument to the kernel.

In the boot loader partition (`/boot/extlinux/extlinux.conf` or equivalent U-Boot boot command), append the isolation parameter to the kernel command line:
```text
APPEND root=/dev/mmcblk0p2 rootwait isolcpus=7 nohz_full=7 rcu_nocbs=7
```
* **`isolcpus=7`:** Prevents the OS scheduler from putting tasks on CPU 7.
* **`nohz_full=7`:** Stops scheduling-clock ticks on CPU 7 when only one real-time thread is running.
* **`rcu_nocbs=7`:** Relocates Read-Copy Update (RCU) callback threads away from CPU 7.

> [!NOTE]
> This boot command is automated in this repository via the U-Boot boot command script [`project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd`](file:///home/tcmichals/projects/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/boot.cmd).

### B. Pinning the CPU Governor to Max Performance

To avoid latency spikes caused by the CPU scaling governor entering low-frequency power-saving states, configure Core 7 to stay at maximum frequency:

```bash
# Lock Core 7 to the performance governor
echo performance > /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
```

### C. Hardware Interrupt Steering (IRQ Affinity)

Even if CPU 7 is isolated from standard user-space tasks, hardware interrupts (such as SPI transactions, Wi-Fi network packets, NPU execution states, and timers) can default to triggering on Core 7, causing microsecond-level latency spikes in the iNAV loop.

To prevent this, steer all hardware interrupts away from Core 7 to Cores 0-6 by setting the SMP affinity mask to `7f` (binary `01111111`):

```bash
# Route all future interrupts to CPU Cores 0-6
echo 7f > /proc/irq/default_smp_affinity

# Route all active interrupts away from CPU Core 7
for irq in /proc/irq/*; do
    [ -d "$irq" ] && echo 7f > "$irq/smp_affinity" 2>/dev/null || true
done
```

> [!TIP]
> Both the Performance Governor lock and the Interrupt Steering are automated at startup in this repository via the system init script [`project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/init.d/S15realtime`](file:///home/tcmichals/projects/cubie-a5e/project-cubie-a5e/board/radxa/cubie_a5e/rootfs-overlay/etc/init.d/S15realtime).

---

### D. C++20 Real-Time ISR Optimization Template (The Doorbell Pattern)

Below is the standard C++20 template used to initialize a real-time thread for the flight loop or IPC bridge (`rbb-server`). Instead of spinning in a `usleep` loop, it blocks on a hardware interrupt doorbell via `/dev/uio0`. It locks memory (preventing disk swapping), sets the `SCHED_FIFO` real-time scheduler policy, and pins the thread affinity to the isolated CPU 7.

```cpp
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <system_error>

#define RT_PRIORITY 90
#define ISOLATED_CPU 7

// Elevate a std::jthread to a POSIX real-time thread (SCHED_FIFO)
// and pin it to an isolated CPU core for hard realtime performance.
void set_realtime_priority(std::jthread& thread) {
    pthread_t native_thread = thread.native_handle();
    
    // 1. Set SCHED_FIFO Priority
    sched_param sch_params;
    sch_params.sched_priority = RT_PRIORITY;
    if (pthread_setschedparam(native_thread, SCHED_FIFO, &sch_params) != 0) {
        std::cerr << "Warning: Failed to set SCHED_FIFO. Run as root.\n";
    }

    // 2. Set CPU Affinity (Pin to isolated core)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(ISOLATED_CPU, &cpuset);
    if (pthread_setaffinity_np(native_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Warning: Failed to pin thread to CPU " << ISOLATED_CPU << "\n";
    }
}

void isr_worker(std::stop_token stoken) {
    // Open the UIO device for the Mailbox doorbell interrupt
    int uio_fd = open("/dev/uio0", O_RDONLY);
    if (uio_fd < 0) return;

    uint32_t irq_count = 0;
    while (!stoken.stop_requested()) {
        // Block completely until the RISC-V or FPGA fires the hardware doorbell!
        // 0% CPU usage while waiting.
        ssize_t bytes = read(uio_fd, &irq_count, sizeof(irq_count));
        
        if (bytes == sizeof(irq_count)) {
            // Doorbell rang! Safely execute the hard real-time loop payload here.
            // e.g., Read from lock-free shared memory /dev/mem or SPI.
            
            // Re-enable the UIO interrupt for the next doorbell
            uint32_t enable = 1;
            write(uio_fd, &enable, sizeof(enable));
        }
    }
    close(uio_fd);
}

int main() {
    // 1. Lock all current and future mapped memory pages to prevent swapping
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: mlockall failed. Page faults may cause jitter.\n";
    }
    
    // 2. Create the background worker using C++20 jthread
    std::jthread worker(isr_worker);
    
    // 3. Elevate it to a real-time POSIX thread on Core 7
    set_realtime_priority(worker);
    
    // Main thread is free to handle non-realtime tasks like Wi-Fi/Telemetry
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
}
```

### D. Verification of Real-Time Loops
You can verify the scheduling priority and core assignment on target using:
```bash
# Display core affinity (psr) and scheduling class (cls) of your process
ps -eo pid,tid,class,rtprio,psr,comm | grep inav
```
* **Expected Class:** `FF` (SCHED_FIFO)
* **Expected RTPRIO:** `80`
* **Expected PSR:** `7` (pinned core index)

