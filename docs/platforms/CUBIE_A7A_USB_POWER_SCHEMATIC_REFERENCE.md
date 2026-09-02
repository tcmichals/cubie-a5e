# Radxa Cubie A7A V1.10 USB and Power Schematic Reference

> **Source**: [Radxa Cubie A7A V1.10 schematic](https://dl.radxa.com/cubie/a7a/docs/hw/radxa_cubie_a7a_v1.10_schematic.pdf), sheets 4, 14, and 15 of 19, reviewed on 2026-09-01.
>
> **Local searchable conversion**: [`../extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt`](../extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt)

## Sheet map

| Sheet | Content | Bring-up use |
|---|---|---|
| 4 | AXP318 PMIC and board power rails | Verify regulator origin, voltage, and always-on dependencies |
| 13 | Wi-Fi/BT | Wi-Fi module power and USB relationship |
| 14 | USB 3.0 / 2.0 / hub | VBUS switches, FE1.1S hub, USB data routing |
| 15 | USB-C and TF card | USB0 Type-C power/data wiring |
| 18 | Ethernet | PHY, RJ45, magnetics, and PHY supply |

## USB physical topology

| Function | Board circuit | Linux-visible control |
|---|---|---|
| USB0 / USB3 OTG bottom connector | CON1 bottom Type-A USB2/USB3 connector; `USB1-DM/DP` and SuperSpeed pairs | VBUS switch U2 (`SGM2576YN5G/TR`) output `VCC5V0_USB30_OTG`; enable net `USB0-DRVVBUS` is **PL2** |
| USB1 host top connector | CON1 top Type-A USB2 connector; `USB1_DM/DP` | Fed from `VCC5V0_USB20` |
| USB2 hub path | J4 USB2D0_2, then FE1.1S U6 downstream ports | VBUS switch U5 (`SGM2576YN5G/TR`) output `VCC5V0_USB20`; enable net `USB_HOST_EN` is **PM5** |
| USB hub | U6 (`FE1.1S`) with Y4 12 MHz crystal | Separate `VCC_3V3_USB20HUB` rail from DCDC1 through R58 |

## Power rails and enable controls

| Rail/control | Schematic source and destination | DTS status |
|---|---|---|
| `VCC5V0_SYS` | Board 5 V source feeding VBUS switches U2 and U5 | Fixed upstream board rail; not GPIO controlled by Linux |
| `USB0-DRVVBUS` / PL2 | Enables U2, switching `VCC5V0_SYS` to `VCC5V0_USB30_OTG` | `reg_usb0_vbus` GPIO `<&r_pio 0 2 GPIO_ACTIVE_HIGH>` |
| `USB_HOST_EN` / PM5 | Enables U5, switching `VCC5V0_SYS` to `VCC5V0_USB20` | `reg_usb1_vbus` GPIO `<&r_pio 1 5 GPIO_ACTIVE_HIGH>` |
| `VCC_3V3_USB20HUB` | DCDC1 through fitted R58, then hub U6 | Hardware PMIC rail; do not model it as a GPIO VBUS regulator |
| `USB_WIFI_PWR` / PM0 | Wi-Fi/BT sheet power-enable net | `reg_wifi_power_en`; separate from USB VBUS |
| `WIFI_REG_ON` / PM1 | Wi-Fi module enable/reset | `reg_wifi_chip_en`; separate from USB VBUS |

## Disabled-USB policy

All USB PHY, EHCI, OHCI, and DWC3 nodes currently remain `status = "disabled"`. Consequently, `reg_usb0_vbus` and `reg_usb1_vbus` must **not** be `regulator-always-on`: those properties drive PL2 and PM5 and energize external USB VBUS even though the controllers are disabled.

When bringing up one USB path later:

1. Enable only its controller, PHY index, and matching VBUS regulator.
2. Keep the unrelated VBUS regulator and all other USB nodes disabled.
3. Confirm the applicable enable net (PL2 or PM5), the switched 5 V rail, and the controller's clock/reset domain before accessing USB MMIO.
4. For the hub path, also confirm `VCC_3V3_USB20HUB` from DCDC1 and Y4's 12 MHz clock.

This prevents a disabled controller from powering connectors or a powered connector from being paired with an unclocked controller.
