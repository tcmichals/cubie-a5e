# testMsgbox (Cadence HiFi4 DSP)
Hardware mailbox and IPC FIFO verification application.

## Objectives
1. Validate hardware msgbox interrupt and FIFO reception on Mailbox Channel 4.
2. Validate response message transmission back to ARM A55 on Mailbox Channel 5.
3. Observe real-time IPC telemetry in `/sys/kernel/debug/remoteproc/remoteproc0/trace0`.

## Target Execution
```bash
cp testMsgbox.elf /lib/firmware/dsp_firmware.elf
echo dsp_firmware.elf > /sys/class/remoteproc/remoteproc0/firmware
echo start > /sys/class/remoteproc/remoteproc0/state

# Trigger mailbox message via mailbox-test
echo -n "PING" > /sys/kernel/debug/mailbox-test/message
cat /sys/kernel/debug/remoteproc/remoteproc0/trace0
```
