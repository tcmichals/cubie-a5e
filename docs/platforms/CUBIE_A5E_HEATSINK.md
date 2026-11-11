# Cubie A5E heatsink and heat spreading for flight

Research note, 2026-11-11. No hardware was touched for this note. It covers what the board
looks like around the SoC, what heatsinks exist, which off-the-shelf parts fit, and a ranked
recommendation with a bench test plan. "Not found" means the fact was searched for and not
found in a citable source.

## 1. Problem statement (measured 2026-11-11)

Source: `logs/a5e-thermal-20261111/RESULTS.md`, TODO.md §5 (A5E thermal check).

| Condition | cpu0/cpu4 zones | ddr zone | AXP717 PMIC |
|---|---|---|---|
| Idle, 4 cores online, VE/GPU/NPU clocks off, fan off, 15×15×5 mm heatsink | 86.3-87.5 °C | 85.6-87.1 °C | 53.6-54.6 °C |
| Same, small fan on, settled after ~4 min | 46.7-48.7 °C | 46.3-47.3 °C | 61.8-62.1 °C |

- Room temperature: not recorded. Load power: not measured (estimate +1-2 W for 4 A55 cores).
- `vdd-cpul/cpub/cpus` are already at 0.90 V, so idle heat cannot be cut much by cpufreq; the
  fan result (same power, about 40 °C lower) shows that the path from the package to the air
  limits the temperature, not the silicon.
- The A527 datasheet gives θJC = 1.26 °C/W (FCCSP 660) ([A527 datasheet V0.96, Table 7-4][a527ds]).
  The junction-to-case step is small; the case-to-air path (heatsink size, interface, airflow) is
  what has to improve.
- TODO.md §5 line "T527/A5E cpufreq + cpuidle" quotes "71 °C with fan". That was a transient
  sample during the cool-down (87 → 78 → 71 → 66 → 62 °C); the settled fan-on value is ~47 °C.
- Limits: A527 working junction temperature max 115 °C, destructive 125 °C, ambient
  -25 to 75 °C ([A527 datasheet V0.96, Tables 7-1, 7-2][a527ds]). The kernel zones have passive
  trips at 70/90 °C and critical 110 °C, but no cpufreq cooling device yet, so nothing throttles.
- Ground phase: before take-off there is no prop wash. The board boots and idles on the ground
  (arming, GPS lock, pre-flight checks), so the passive solution must hold the idle case by
  itself, or a fan is still needed on the ground.

## 2. Board facts

### 2.1 Outline and mounting holes (Radxa product brief, V1.2 drawing)

Source: [Cubie A5E product brief, §4.2 "V1.2"][brief] (RAD-DOC-0138).

| Item | Value |
|---|---|
| Board outline | 56.0 × 65.0 mm (the RJ45/USB/HDMI ports overhang the 65 mm edge by 5.7 mm) |
| Mounting holes | 4 × Ø2.7 mm (M2.5 clearance; M2 also passes), 49.0 × 58.0 mm centres, 3.5 mm from each edge |
| Hole pattern | Same 49 × 58 mm M2.5 pattern as the Raspberry Pi 4/5 ([Pi 4 mechanical drawing][pi4mech]), so Pi standoff and plate kits fit the holes (not the SoC position) |
| PCB thickness | not stated in the brief; the STEP model's board body is 1.6 mm thick |

The repo carrier `hardware/carrier_a5e_tang25k/README.md` §4 says 65.0 × 56.0 mm and M2.5 holes
at 58.0 × 49.0 mm, which matches.

### 2.2 The SoC is on the bottom side

The A5E SoC sits on the **bottom** side (the side without the RJ45, USB, HDMI and 40-pin header),
rotated **45°** to the board edges:

- Bottom view in the [product brief][brief] and page 1 of the
  [component placement map V1.2][placement] show the diamond-shaped package, the LPDDR4, the
  eMMC, the TF slot and the LCD/camera FPC connectors on that side.
- Radxa's own [Metal Case 7264 assembly guide][case-a5e] says to apply the thermal pad "to the
  back of the mainboard at the SoC location"; the case render thread says the board is mounted
  "upside down" ([forum: Radxa Metal Case for Cubie A5E][forum-case]).

Consequence for the quad: the 40-pin header is on the top side, so the carrier
(`hardware/carrier_a5e_tang25k`) stacks on the top side and the SoC side stays free for a heat
spreader. Confirm with the real stack before cutting metal.

### 2.3 SoC position and package

Measured from the V1.2 bottom-view drawing in the [product brief][brief] (dimensions printed
on the drawing; positions of other parts scaled from the same drawing, ±0.5 mm):

Coordinates: bottom view, origin at the corner where the **FPC/TF edge** (the short edge
opposite the RJ45 ports) meets the **edge away from the 40-pin header**; x along the FPC edge
(0-56 mm), y towards the RJ45 edge (0-65 mm). In these coordinates the mounting holes are at
(3.5, 3.5), (52.5, 3.5), (3.5, 61.5), (52.5, 61.5) and the 40-pin header runs along x ≈ 49-56.

| Item | Value |
|---|---|
| SoC centre | 20.1 mm from the 40-pin-header edge, 29.6 mm from the FPC/TF edge → (35.9, 29.6) (both printed on the drawing) |
| SoC orientation | rotated 45°; the package diamond spans about 24.5 mm corner to corner |
| Package, A527 | 17 × 17 mm FCCSP 660 (max height 1.046 mm) or ED-FCCSP 660 (exposed die, max height 1.066 mm), 0.5 mm pitch ([A527 datasheet V0.96 §1, §8][a527ds]) |
| Package, T527 | 17 × 17 mm FCBGA 664 per the T527 datasheet drafts as reported by search results ([T527 V0.11 draft][t527ds011], [T527 V0.90][t527ds090], [LCSC T527M00X0DCH][lcsc-t527]); a V0.90 maximum height of 1.844 mm was quoted by a search summary but the PDF could not be opened here: **not verified** |
| Which SoC is on the owner's board | The board has an NPU node and NPU clocks (`7122000.npu`, `logs/a5e-thermal-20261111/extra-checks.txt`), which suggests the T527 SKU (RS500-…R41). Read the marking on the package to confirm; the package height decides the shim thickness |
| Thermal data (A527, JESD51, 4-layer 114 × 76 mm test board, still air) | FCCSP: θJA 22.13, θJB 9.34, θJC 1.26 °C/W; ED-FCCSP: θJA 21.76, θJB 8.97 °C/W, θJC not given ([A527 datasheet V0.96 Tables 7-3/7-4][a527ds]) |
| Thermal data (T527) | not found |

Geometry rule that follows from the 45° rotation: a square shim or heatsink base laid **parallel
to the board edges** stays inside the 17 mm package only if its side is ≤ 17/√2 ≈ 12.0 mm. A
square up to 17 mm must be **rotated 45°** to match the package. A larger part laid square to the
edges overhangs the package onto the decoupling parts around it, so it must not touch them (see
§2.4 and §5).

### 2.4 What sits around the SoC (bottom side)

From the bottom view in the [product brief][brief] and the [placement map][placement]; refdes
from the [schematic V1.2][schematic]:

| Part | Position (bottom-view coordinates above), approx. size | Note |
|---|---|---|
| LPDDR4/4X (schematic p.8) | x 8.7-19.2, y 20.5-36.1 mm, about 10.5 × 15.6 mm | Left of the SoC, about 4-5 mm gap to the SoC's left corner. Also hot (ddr zone 86 °C fanless) |
| eMMC (schematic p.9; not fitted on E0 SKUs) | x 4.0-17.6, y 37.1-49.1 mm | Below the DRAM |
| 2 × YT8531C Ethernet PHY QFN (U13, U14, crystal Y5) | x 38.5-43.8 and 47.5-52.9, y 41.3-46.2 mm, about 5 × 5 mm each | Just below the SoC's lower corner, under a 25 mm-wide sink footprint |
| Crystals / small parts (Y1, Y2) | around x 27-33, y 17-21 mm | Above-left of the SoC corner |
| Decoupling capacitor arrays | along the upper-right edges of the SoC diamond | Within 1-3 mm of the package edge; the main short-circuit risk for a shim or overhanging sink |
| TF (microSD) slot | x 21.8-33.9, y 0-11.7 mm | Card must remain removable, or plan to boot from eMMC only |
| LCD and camera FPC connectors | along the y = 0 edge, x ≈ 7-21 (LCD) and 38-49 (camera) | Need clearance if used |
| 40-pin header and RJ45/USB through-hole tails | x ≈ 49-56 (header), y ≈ 52-65 (RJ45/USB) | Solder tails protrude on the bottom side |
| PMIC AXP717C (UP1) and AXP323 (UP5) | **top** side (placement map page 2) | Not under a bottom-side sink |

Component heights on the bottom side: **not published** by Radxa (no 2D height drawing found).
A rough look at the vertices of the [V1.2 STEP model][step] gives through-hole tails of about
2-2.5 mm, the TF slot about 1.4-1.6 mm, FPC connectors about 1 mm, and one group of parts near
the header-side/FPC corner up to about 2.8 mm. These figures come from automated vertex
sampling, not from a dimensioned drawing: **measure with calipers** (or in CAD from the STEP file)
before choosing standoff and shim heights.

Downloaded copies (outside the repo, per the workspace rule): `~/projects/cubie/radxa-a5e-hw/`
(product brief, placement map, schematic, STEP zip, A527 datasheet, Metal Case 7264 brief).

## 3. Existing heatsinks, cases and reports

| Item | Facts | Source |
|---|---|---|
| Radxa Metal Case 7264 (official, Cubie A5E) | All-metal case, about 72.6 × 63.4 × 30 mm, "3D cooling fins", fanless. The heatsink is the case body; the board sits SoC-side down on four raised posts of the heatsink, thermal pad on the SoC. Parts: heatsink, two side covers, bottom cover, thermal pad, 4 × KM2 3.5 mm screws, 4 × M2 × 16.6 mm standoffs. PoE variant fixes the board with M2 × 7 mm copper standoffs. Weight: **not found**. Pad thickness/conductivity: **not found**. Thermal test data: **not found** | [product brief][case-brief], [A5E assembly][case-a5e], [A5E + PoE assembly][case-a5e-poe] |
| Arace listing "Radxa Metal Case for Cubie A5E" | USD 7.50 (July 2025 forum post); the store page refused automated fetch (HTTP 429) | [forum: heatsink and/or case][forum-hs], [arace][arace-case] |
| Case standoffs | Users report badly threaded/wobbly standoffs (Dec 2025-Feb 2026) | [forum: Radxa Metal Case for Cubie A5E][forum-case] |
| Community idle temperatures | ~76 °C idle on the Radxa bullseye beta image (performance governor; Radxa staff named that as the cause); ~71-74 °C on rsdk-t3 test image; ~54-58 °C on Armbian 6.14 and on another user's Radxa image; "close to 90 °C" with PoE + NVMe (Dec 2025). Heatsinks, fans and room temperature mostly not stated. Radxa staff also noted U-Boot enables some rails by default. 2.2 W at the U-Boot prompt measured by one user | [forum: Very high temperature on idle Cubie A5E][forum-idle] |
| Other A5E heatsinks/fans (official or third-party) | **not found** (only 3D-printable cases mentioned in the forum) | [forum: heatsink and/or case][forum-hs] |

## 4. Off-the-shelf parts that fit

### 4.1 Interface materials

| Part | Data | Use here |
|---|---|---|
| Copper shims, 20 × 20 mm (also 15 × 15), 0.3 / 0.5 / 0.8 / 1.0 / 1.2 / 1.5 mm, sold in assortments for laptop GPU repair | Bulk copper; sellers quote ~400 W/mK (unverified seller claim). Mass computed from copper density 8.96 g/cm³: 20 × 20 × 1.0 mm ≈ 3.6 g, 15 × 15 × 1.0 mm ≈ 2.0 g, 15 × 15 × 1.5 mm ≈ 3.0 g | Pedestal that lifts the contact above the decoupling parts and fills the gap to a plate or sink. Use 15 × 15 rotated 45°, or ≤ 12 × 12 square to the edges (§2.3). Example listings: [Kustom PCs 20 × 20 × 0.5][shim-kustom], [assortment 0.3-1.5 mm][shim-kit] |
| Gelid GP-Extreme pad | 12 W/mK, Shore 00-35, 0.5 / 1.0 / 1.5 / 2.0 / 3.0 mm, 80 × 40 mm sheets, electrically non-conductive (retailer data; one 1.5 mm listing says 5 W/mK, so check the datasheet) | Between shim and plate/sink; soft, takes up height tolerance and vibration. Source: [quietpc][gelid] |
| Non-conductive, non-curing paste (any brand marked electrically non-conductive) | — | Thin layer between package and shim when the shim is mechanically held; avoid metal-filled pastes next to the 0201/0402 decoupling parts |
| Adhesive thermal tape | — | Only to keep a part in place during assembly. Not as the only retention for anything heavier than a few grams under vibration |

### 4.2 Heatsinks

| Part | Size | Published thermal resistance | Weight | Fit on the A5E | Source |
|---|---|---|---|---|---|
| Current part | 15 × 15 × 5 mm | not known | not known | Smaller than the 17 mm package; idles at 87 °C fanless | 2026-11-11 test |
| Fischer Elektronik ICK S 25 × 25 × 6.5 (pin fin, Al) | 25 × 25 × 6.5 mm | 5.4 °C/W (element14); 5.8 °C/W natural convection, 4 °C/W forced (DigiKey); 3.5-5.8 K/W (Bürklin) | ~5 g (element14 NZ) | Base larger than the package: must sit on a shim so it clears the decoupling parts, Y1/Y2 and the PHYs. Centred on the SoC a 25 mm base spans x ≈ 23-48, so it just clears the header tails at x ≈ 49 | [element14][ick-e14], [DigiKey][ick-dk], [element14 NZ][ick-e14nz] |
| Fischer ICK S 25 × 25 × 12.5 | 25 × 25 × 12.5 mm | 5.3 to 2.1 K/W (natural to forced) | ~6 g | Same as above, taller; more fin area for prop wash | [DAU][ick-dau] |
| ATS maxiFLOW/maxiGRIP ATS-50170B-C1-R0 | 17 × 17 × 7.5 mm, clip grips the package edges | 18.2 °C/W at 200 LFM (1 m/s) down to 9.0 °C/W at 800 LFM (4 m/s), unducted | not found | The maxiGRIP clip needs free space around all four package edges; the decoupling arrays sit within 1-3 mm of the A5E package edge, so the clip probably does not fit. Not recommended without a test fit | [ATS][ats-50170b] |
| Radxa Metal Case 7264 heatsink body | case 72.6 × 63.4 × 30 mm | not published | not found | Designed for this board and SoC position; fixes on the board's hole pattern via posts. Probably too heavy and too enclosed for flight; useful as a bench reference | §3 |

### 4.3 Plate and frame as heatsink

A flat aluminium plate on the four mounting holes, coupled to the SoC through a copper shim, uses
the board's own mounting points (no adhesive, no clip around the package) and puts a large area in
the prop wash. The plate can also be the plate that fixes the A5E to the airframe. Masses computed
from aluminium density 2.70 g/cm³:

| Plate | Mass |
|---|---|
| 56 × 65 × 1.5 mm, full board outline | ≈ 14.7 g |
| 56 × 65 × 2.0 mm | ≈ 19.7 g |
| 1.5 mm, cut down to a ~35 × 35 mm centre pad plus four arms to the holes | less; depends on the cut-out |

Carbon-fibre frame plates conduct electricity and conduct heat much less well across the plate than
aluminium; use the frame only as a mount for the aluminium plate, not as the heat path, unless a
test shows otherwise. Published thermal data for drone frames as heatsinks: **not found**.

## 5. Recommendation

All three options keep the board's own four Ø2.7 mm holes as the only mechanical attachment and
keep adhesive out of the load path.

### Option 1 (recommended): aluminium spreader plate on the four holes, copper shim pedestal

Stack-up (from the board out):

1. SoC package (17 × 17 mm, rotated 45°).
2. Thin layer of non-conductive paste, or a 0.5 mm 12 W/mK pad.
3. Copper shim 15 × 15 mm **rotated 45°** to match the package (or ≤ 12 × 12 mm square to the
   edges), thickness chosen from the measured gap (see below).
4. 0.5 mm soft pad (e.g. Gelid GP-Extreme 0.5 mm), 20 × 20 mm.
5. 1.5-2.0 mm aluminium plate, board outline 56 × 65 mm (or cut down), 4 × Ø2.7 mm holes at
   49 × 58 mm.

Mounting: 4 × M2.5 screws through the board into M2.5 standoffs (female-female, aluminium or
brass; nylon only if the plate is also fixed elsewhere, because nylon creeps and the pad pressure
drops). Standoff height H is set above the tallest bottom-side part (measured; the rough STEP figure
is about 2.5-2.8 mm near the header/RJ45 tails), so H is about 3-3.5 mm. Shim thickness then
follows: shim = H − SoC height − pad thickness × (1 − compression). Example with H = 3.0 mm,
A527 FCCSP 1.05 mm and a 0.5 mm pad compressed 20 %: 3.0 − 1.05 − 0.4 ≈ 1.5 mm shim. Re-do this
with the measured package height (the T527 package may be taller) and measured standoff
length. Check by assembling with a thin film of paste and looking at the imprint on the shim.

Optionally add a DRAM pad: a 1.0-1.5 mm pad between the LPDDR4 and the plate (the ddr zone is as
hot as the CPU zones). The DRAM package height must be measured first.

Short-circuit precautions:

- Kapton (polyimide) tape on the whole inner face of the plate except the shim/pad window, and
  around the edges of the window.
- Kapton over the through-hole tails of the 40-pin header and RJ45/USB if the plate covers them.
- Round or deburr plate edges and holes; the plate is grounded through the standoffs only if the
  hole pads are grounded (check the hole rings on the schematic/board before relying on that), so
  treat it as floating metal.
- Shim corners must stay inside the package outline: no contact with the decoupling arrays.
- After the first assembly, power on with a current-limited supply and check the 5 V input current
  against the normal idle value before the first flight.

Weight: plate ≈ 15-20 g (full outline), shim ≈ 2-3 g, pad and screws ≈ 1-2 g. Replaces the
current sink.

Expected benefit (qualitative): much more surface than the 15 × 15 × 5 mm sink, in the prop wash
in flight, with a metal path from the package to it. On the ground without airflow it may still be
too hot at idle; the bench test decides whether a fan is still needed before take-off.

### Option 2: Option 1 plus a pin-fin heatsink on the plate

Same stack-up, plus a Fischer ICK S 25 × 25 × 6.5 (≈ 5 g) or 25 × 25 × 12.5 (≈ 6 g) on the
outer face of the plate over the SoC, with a 0.5 mm pad or paste, held by two M2 screws through
the plate into tapped holes or nuts in the sink base (drill between pins), not by adhesive alone.
Adds fin area for the ground phase and for low airflow. Weight ≈ 20-27 g total. Use it if the
Option 1 bench test still idles too hot without a fan.

Variant without the plate (lighter, less robust): the same ICK S 25 × 25 sink on a 15 × 15 mm
copper shim (rotated 45°), pressed by a 1.0-1.5 mm aluminium strap screwed into two diagonal
mounting holes. The diagonal from (3.5, 61.5) to (52.5, 3.5) passes about 6 mm from the SoC
centre, so a strap with a small dog-leg reaches it. Weight ≈ 10-12 g. Risk: the 25 mm base
overhangs the package onto the decoupling parts and PHYs; it relies on the shim height alone for
clearance, and a single strap gives a less even pressure.

### Option 3: Radxa Metal Case 7264 heatsink body (bench reference, ground testing)

Use only the finned heatsink part of the official case, with its pad, and screw the board to its
posts (M2; the PoE variant uses M2 × 7 mm standoffs). Stack-up: SoC → Radxa pad → heatsink post
area. Benefit: designed by Radxa for this board, so contact and pad thickness are right without
measurement. Drawbacks: weight not published (weigh it), 30 mm-tall case, fins designed for a
closed case standing on a desk, and reported standoff thread problems. Good as a bench reference
for what a well-coupled sink achieves; probably not the flight solution.

### Fan as a fallback

The 2026-11-11 test showed a small fan alone takes the idle temperature from 87 °C to ~47 °C. If
the passive options do not keep the ground idle below the passive trip with margin, keep a small
5 V fan and put it under Linux control (`pwm-fan`/`gpio-fan` cooling device, TODO.md §5).

## 6. Bench test plan

Same board, same image, same 4-core configuration (cpu4-7 offline, VE/GPU/NPU off) as the
2026-11-11 baseline, so the results compare.

1. **Record room temperature** with a thermometer next to the board at the start and end of every
   run; report every result also as (zone − room). Same board orientation (SoC side up or down)
   for every run; note it.
2. Sampling: all `thermal_zone*` plus the AXP717 hwmon every 5-15 s, with
   `logs/a5e-thermal-20261111/sample_temps.py`, one serial reader at a time.
3. For each configuration (baseline 15 × 15 sink, Option 1, Option 2, Option 3 if available):
   1. Fan off, idle from cold boot, 15 min or until stable (< 0.5 °C change in 2 min).
   2. If idle stays below 80 °C: 4-core load (`stress-ng --cpu 4` or 4 × `yes > /dev/null`),
      **abort at 85 °C on any zone**, else run 10 min; record the peak and the time to it.
   3. Fan on (same fan, same position): repeat idle and the 4-core load with the same abort rule.
   4. Cool-down: stop the load and record 5 min.
   5. Optional prop-wash proxy: a fan 20-30 cm away blowing across the plate (not onto it from
      close range), to compare against "fan on the sink".
4. Also record: 5 V input current at idle and under load (USB meter), so the load power estimate
   (+1-2 W) is replaced by a measurement.
5. Mechanical: after the runs, shake test (hand or motors running on the frame), then remove the
   plate and check the paste/pad imprint for full contact and shim movement; check the Kapton for
   cuts at the header tails.
6. Pass criteria to discuss with the owner: fanless idle and 4-core load below the 70 °C passive
   trip on the bench at the recorded room temperature, with margin for summer ambient and for
   adding VE/NPU load later.

Results go into `logs/a5e-thermal-<date>/RESULTS.md` and the TODO.md A5E thermal entry.

## 7. Open facts (not found)

- Bottom-side component heights (no dimensioned height drawing; measure or use the STEP file).
- T527 package height and θJC (A527 data only; T527 datasheet PDF not opened here).
- Radxa Metal Case 7264 weight, pad thickness and conductivity, thermal test data.
- Any other A5E-specific heatsink or fan product.
- Published data for drone frames used as heatsinks.

## Sources

[brief]: https://dl.radxa.com/cubie/a5e/radxa_cubie_a5e_product_brief.pdf
[placement]: https://dl.radxa.com/cubie/a5e/docs/hw/v1.2/radxa_cubie_a5e_components_placement_map_v1.2_20250113.pdf
[schematic]: https://dl.radxa.com/cubie/a5e/docs/hw/v1.2/radxa_cubie_a5e_schematic_v1.2_20250113.pdf
[step]: https://dl.radxa.com/cubie/a5e/docs/hw/v1.2/radxa_cubie_a5e_pcba_3d_v1.2_stp_20250224.zip
[a527ds]: https://dl.radxa.com/cubie/a5e/docs/hw/datasheet/A527_Datasheet_V0.96.pdf
[t527ds011]: https://server7.whycan.com/files/members/3907/T527_Datasheet_V0.11_Draft_Version.pdf
[t527ds090]: https://www.armdesigner.com/download/T527_Datasheet_V0.90.pdf
[lcsc-t527]: https://lcsc.com/product-detail/Microcontrollers-MCU-MPU-SOC_Allwinner-Tech-T527M00X0DCH_C28310420.html
[pi4mech]: https://datasheets.raspberrypi.com/rpi4/raspberry-pi-4-mechanical-drawing.pdf
[case-brief]: https://dl.radxa.com/accessories/metal-case-7264/docs/radxa_metal_case_7264_product_brief_revision_1.0.pdf
[case-a5e]: https://docs.radxa.com/en/accessories/heatsink-case/metal-case-7264/metal-case-7264-a5e
[case-a5e-poe]: https://docs.radxa.com/en/accessories/heatsink-case/metal-case-7264/metal-case-7264-a5e-poe
[arace-case]: https://arace.tech/products/radxa-metal-case-for-cubie-a5e
[forum-hs]: https://forum.radxa.com/t/cubie-a5e-heatsink-and-or-case/27917
[forum-case]: https://forum.radxa.com/t/radxa-metal-case-for-cubie-a5e/26211
[forum-idle]: https://forum.radxa.com/t/very-high-temperature-on-idle-cubie-a5e/28026
[shim-kustom]: https://www.kustompcs.co.uk/cooling/heatsinks/graphics-coolers/pure-copper-thermal-pad-20mm-x-20mm-x-0.5mm/
[shim-kit]: https://www.desertcart.hr/products/295479746-easycargo-40pcs-6-sizes-20mm-copper-pad-shim-thermal-assortment.md
[gelid]: https://quietpc.com/gelid-gp-pads
[ick-e14]: https://my.element14.com/fischer-elektronik/ick-s-25-x-25-x-6-5/heat-sink-pin/dp/1211712
[ick-e14nz]: https://nz.element14.com/fischer-elektronik/ick-s-25-x-25-x-6-5/heat-sink-pin/dp/1211712
[ick-dk]: https://www.digikey.co.uk/en/products/detail/fischer-elektronik/ICK-S-25-x-25-x-6-5/26281936
[ick-dau]: https://dau-components.co.uk/product-catalogue/thermal-management/heatsinks/for-processors/pin-heatsinks-ick-s-25-x-25-x-12-5/
[ats-50170b]: https://www.qats.com/Product/Heat-Sinks/BGA-Heat-Sink---High-Performance/maxiFLOW-maxiGRIP-STD/ATS-50170B-C1-R0/389.aspx

- Radxa Cubie A5E product brief (mechanical drawing V1.1/V1.2, SKUs): <https://dl.radxa.com/cubie/a5e/radxa_cubie_a5e_product_brief.pdf>
- Radxa Cubie A5E docs, downloads page: <https://docs.radxa.com/en/cubie/a5e/download>
- Placement map, schematic, STEP V1.2: see the dl.radxa.com links above
- A527 datasheet V0.96: <https://dl.radxa.com/cubie/a5e/docs/hw/datasheet/A527_Datasheet_V0.96.pdf>
- Radxa Metal Case 7264: brief, A5E and A5E+PoE assembly pages (links above)
- Radxa forum threads: heatsink/case, metal case, idle temperature (links above)
- Distributor pages for the Fischer ICK S and ATS heatsinks, Gelid pads and copper shims (links above)
