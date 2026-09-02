# Radxa Cubie A7A V1.10 Ethernet Schematic Reference

> **Source**: [Radxa Cubie A7A V1.10 schematic, sheet 18 of 19](https://dl.radxa.com/cubie/a7a/docs/hw/radxa_cubie_a7a_v1.10_schematic.pdf), rendered and reviewed on 2026-09-01.
>
> **Local searchable conversion**: [`../extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt`](../extracted_vendor/radxa_cubie_a7a_v1.10_schematic.txt)

## PHY and RJ45

| Item | Verified design |
|---|---|
| PHY | U10, **Maxio MAE0621A-Q3C** |
| Connector | J2, **LPJG0926HENL** RJ45 with integrated magnetics and green/yellow LEDs |
| MDIO address straps | `PHY_RXD3/PHYAD0`, `PHY_RXCLK/PHYAD1`, `PHY_RXDV/PHYAD2`; configured as address 1 |
| Copper pairs | PHY `MDI[0:3]+/-` route through L18–L21 common-mode chokes to J2 transformer pairs |
| Link LEDs | PHY LED0/1/2 outputs route to the integrated RJ45 green/yellow LEDs through fitted 510 Ω resistors |

## Clock, reset, and power

| Signal | Verified board wiring | Bring-up implication |
|---|---|---|
| PHY reference | Y5 is a **25 MHz, 12 pF, 10 ppm crystal**, with C180/C181 18 pF capacitors, directly on U10 `XIN`/`XOUT` | The PHY has an autonomous reference clock. It does not need a SoC-generated 25 MHz clock to establish copper link. |
| `EPHY-CLK-25M` | Connected to PHY `XTALOUT` only through R116, marked **NC/0R** | The current CCU raw write to the SoC GMAC PHY-clock register cannot restore a missing PHY clock on this board. |
| Reset | `GMAC1_RSTn_L` → fitted R185 (0 Ω) → U10 `PHY_RESETn`; C196 (100 nF) to ground | PH16 must release reset after boot. |
| 3.3 V core/I/O | `VCC3V3_PHY` supplies PHY 3.3 V; `VCCIO_PHY` is joined through fitted R178 (0 Ω) | Check these rails at U10 if LEDs remain dark. |
| 1.0 V core | U10 `REG_OUT` feeds L12 and produces `VDD10_PHY` | Check this internal regulator rail if link logic is inactive despite MDIO response. |

## RGMII and straps

- RGMII uses `GMAC1_TXD[0:3]`, `GMAC1_TXCLK`, `GMAC1_TXEN`, `GMAC1_RXD[0:3]`, `GMAC1_RXDV_CRS`, and `GMAC1_RXCLK`, each through a fitted 0 Ω series resistor.
- Management signals `GMAC1_MDC` and `GMAC1_MDIO` use fitted 0 Ω resistors R221/R222.
- The strap table selects **external 3.3 V RGMII I/O**.
- Fitted pull-ups on `PHY_RXD0/RXDLY` and `PHY_RXD1/TXDLY` enable the PHY’s additional 2 ns receive/transmit clock delays. This supports `phy-mode = "rgmii-id"`; the MAC must not add duplicate RGMII delay.

## Current failure boundary

No RJ45 link LEDs, no kernel `Link is Up` transition, and zero packet counters demonstrate a PHY/copper-link failure before IP and MAC DMA. Given the dedicated PHY crystal, investigate PHY control/status (link, autonegotiation, power-down, isolate), reset level, `VCC3V3_PHY`/`VCCIO_PHY`/`VDD10_PHY`, cable/J2 magnetics, and PHY-driver initialization. Do not alter DMA, DHCP, or the optional SoC `EPHY-CLK-25M` path without evidence.
