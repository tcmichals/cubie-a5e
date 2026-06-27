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

### B. Pinning the CPU Governor to Max Performance

To avoid latency spikes caused by the CPU scaling governor entering low-frequency power-saving states, configure Core 7 to stay at maximum frequency:

```bash
# Lock Core 7 to the performance governor
echo performance > /sys/devices/system/cpu/cpu7/cpufreq/scaling_governor
```

---

### C. C++ Application RT Optimization Template

Below is the standard C++ template used to initialize a real-time thread for the main iNAV loop. It locks memory (preventing disk swapping), allocates stack pages beforehand (preventing on-demand page faults), sets the `SCHED_FIFO` real-time scheduler policy, and pins the thread affinity to CPU 7.

```cpp
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>

#define RT_PRIORITY 80
#define STACK_SIZE (1024 * 1024) // Allocate 1MB stack space

// Pre-fault the stack to ensure all stack pages are mapped to physical RAM
// before entering the high-speed flight loop.
void pre_fault_stack(void) {
    unsigned char dummy[STACK_SIZE];
    memset(dummy, 0, STACK_SIZE);
}

void configure_realtime_runtime(void) {
    // 1. Lock all current and future mapped memory pages to prevent swapping
    if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
        perror("mlockall failed");
        exit(EXIT_FAILURE);
    }
    
    // 2. Pre-fault the stack pages
    pre_fault_stack();
    
    // 3. Configure scheduling policy to SCHED_FIFO (first-in, first-out real-time)
    struct sched_param param;
    param.sched_priority = RT_PRIORITY;
    if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
        perror("sched_setscheduler SCHED_FIFO failed");
        exit(EXIT_FAILURE);
    }
    
    // 4. Bind the execution context exclusively to isolated CPU Core 7
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(7, &cpuset);
    
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np failed");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[]) {
    // Initialize real-time scheduler, memory, and core affinity
    configure_realtime_runtime();
    
    printf("iNAV Real-Time Loop Initialized on CPU Core 7.\n");
    
    // Real-Time Loop
    while (1) {
        // 1. Read IMU sensors over SPI (/dev/spidev0.0)
        // 2. Compute PID update (iNAV core control loop)
        // 3. Write Motor/ESC command structures to FPGA (/dev/spidev1.0)
        
        // CRITICAL RULE: Avoid any dynamic memory allocation (malloc, new, free, std::vector resizing)
        // inside this loop to prevent non-deterministic heap lock overhead.
        
        usleep(1000); // 1 kHz flight control loop
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

