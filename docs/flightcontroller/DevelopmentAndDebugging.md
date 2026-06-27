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
