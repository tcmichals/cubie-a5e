# A5E reset under load: AXP717 readback and supply log (2026-11-12, partial)

Board `cubie-a5e-flight`, kernel `7.1.0-cpufreq`, wlan0 192.168.1.4 (eth0 link down this morning), uptime 20 min at start (reset or power cycle at about 07:02, cause unknown). Datasheet: AXP717 V1.0 (`~/projects/cubie/downloads/axp717/`, text in `axp717.txt`).

## AXP717 readback (i2c-2 0x34) and decode

| Reg | Value | Meaning (datasheet §6.15.2) |
|---|---|---|
| 00 PMU status1 | 0x20 | VBUS good, no battery, not in thermal regulation, not in current limit |
| 01 PMU status2 | 0x90 | system on, not in VINDPM |
| 08 PMU fault | 0x05 | bit 2 VBAT < UVLO (no battery, expected), bit 0 battery under-temperature (TS, no battery) |
| 16 VINDPM | 0x07 | 4.44 V |
| 17 input current limit | 0x1c | 100 + 28 × 50 = **1500 mA** |
| 19 module enable 2 | 0x06 | bit 0 = 0: **PMIC watchdog disabled** |
| 1A watchdog control | 0x06 | IRQ only, 64 s (watchdog off anyway) |
| 20 PWRON status | 0x04 | power-on source: VBUS insert and good |
| 21 PWROFF status | 0x00 | no power-off source latched (POR-reset bits) |
| 23 DCDC OVP/UVP | 0x17 | OVP off-switch on; DCDC1/2/3 85 % undervoltage power-off on, DCDC4 off |
| 27 soft power-off | 0x04 | PWROK-low restart disabled |

ADC (1 mV/LSB): VBUS = `in_voltage2_raw`, VSYS = `in_voltage3_raw`.

## How to read the next reset

REG 21H is reset only by a power-on reset (POR). A marker in a harmless POR-type register tells the three cases apart; write `i2cset -f -y 2 0x34 0x1a 0x05` before the load (the watchdog is disabled, so the timer field does nothing), then read 1A, 20, 21 after the reset, before any power cycle:

- 1A still 0x05: the PMIC was not reset: a SoC-side reset.
- 1A back to 0x06, REG 21 bit set: the PMIC switched off on that fault (bit 5 DCDC undervoltage, bit 3 VSYS undervoltage, bit 7 over-temperature, ...) and came back on VBUS.
- 1A back to 0x06, REG 21 = 0: power-on reset: the input supply collapsed.

## Run 1 (stress-ng --cpu 4 --vm 2 --vm-bytes 25 %, E906 running riscv-firmware.elf, 792 MHz, fan on)

Stopped after about 2 min (owner leaving). No reset. `adc.log` (uptime, VBUS mV, VSYS mV, zone0 m°C, about 40 samples/s):
VBUS 4.66 V idle → min **4.532 V** under load (0.09 V above VINDPM 4.44 V); VSYS min 3.584 V; zone0 max 50.4 °C. The supply already sags 0.13 V under this load; the E906 bench load that reset the board draws more.

Ramoops not set up: `PSTORE_CONSOLE` and `PSTORE_PMSG` are off in this kernel, so ramoops would only keep oops/panic dumps and these resets produce none.

## Next

1. Arm the marker, start the ADC stream and serial log, run the E906 load that reset 2/2 at 792 MHz, read 1A/20/21 after the reset.
2. Measure 5 V at the board input with a meter/scope under load; try a stronger supply or shorter cable. If VBUS reaches 4.44 V the PMIC limits input and VSYS drops.
3. Board state at the end: stress stopped, REG 1A restored to 0x06, board lock removed.
