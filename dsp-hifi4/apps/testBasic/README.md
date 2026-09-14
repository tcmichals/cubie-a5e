# testBasic (Cadence HiFi4 DSP)
Minimal remoteproc lifecycle and trace0 verification application.

## Objectives
1. Verify remoteproc ELF parsing and loading at execution window `0x40100000`.
2. Release DSP clock/reset gating and confirm start/stop state machine.
3. Validate string output in `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.

## Target Execution
```bash
cp testBasic.elf /lib/firmware/dsp_firmware.elf
echo dsp_firmware.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```
