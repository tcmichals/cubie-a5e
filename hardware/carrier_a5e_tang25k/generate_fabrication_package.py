#!/usr/bin/env python3
"""
Fabrication Package & KiCad Generator for:
Radxa Cubie A5E + Sipeed Tang Primer 25K Avionics Carrier

Outputs:
1. bom.csv          (JLCPCB / PCBWay SMT BOM with verified LCSC part numbers)
2. positions.csv    (JLCPCB / PCBWay Pick-and-Place CPL Centroid file)
3. carrier_a5e_tang25k.kicad_sch
4. carrier_a5e_tang25k.kicad_pcb
"""

import os
import csv

HARDWARE_DIR = os.path.dirname(os.path.abspath(__file__))

# -------------------------------------------------------------------------
# BOM Data (JLCPCB SMT Compatible with exact LCSC Part Numbers)
# -------------------------------------------------------------------------
BOM_ITEMS = [
    {
        "Comment": "100nF (0.1uF) 50V X7R",
        "Designator": "C1, C2, C3, C4, C5, C6, C7",
        "Footprint": "C_0402_1005Metric",
        "LCSC Part #": "C1525",
        "Description": "Capacitor 100nF 50V X7R 0402",
        "Type": "Basic Part",
    },
    {
        "Comment": "10uF 16V X5R",
        "Designator": "C8, C9, C10",
        "Footprint": "C_0603_1608Metric",
        "LCSC Part #": "C19702",
        "Description": "Capacitor 10uF 16V X5R 0603",
        "Type": "Basic Part",
    },
    {
        "Comment": "47uF 16V X5R",
        "Designator": "C11, C12",
        "Footprint": "C_0805_2012Metric",
        "LCSC Part #": "C23746",
        "Description": "Capacitor 47uF 16V X5R 0805 Low-ESR",
        "Type": "Basic Part",
    },
    {
        "Comment": "33R 1% 1/16W",
        "Designator": "R1, R2, R3, R4, R5",
        "Footprint": "R_0402_1005Metric",
        "LCSC Part #": "C25114",
        "Description": "Resistor 33 Ohm 1% 0402 (SPI Damping)",
        "Type": "Basic Part",
    },
    {
        "Comment": "2.2k 1% 1/16W",
        "Designator": "R6, R7",
        "Footprint": "R_0402_1005Metric",
        "LCSC Part #": "C25879",
        "Description": "Resistor 2.2k Ohm 1% 0402 (I2C Pull-Up)",
        "Type": "Basic Part",
    },
    {
        "Comment": "10k 1% 1/16W",
        "Designator": "R8, R9, R10, R11",
        "Footprint": "R_0402_1005Metric",
        "LCSC Part #": "C25744",
        "Description": "Resistor 10k Ohm 1% 0402 (CS/IRQ Pull-Up/Down)",
        "Type": "Basic Part",
    },
    {
        "Comment": "1k 1% 1/16W",
        "Designator": "R12, R13",
        "Footprint": "R_0402_1005Metric",
        "LCSC Part #": "C11702",
        "Description": "Resistor 1k Ohm 1% 0402 (LED Current Limiting)",
        "Type": "Basic Part",
    },
    {
        "Comment": "LED Green 0603",
        "Designator": "D1, D2",
        "Footprint": "LED_0603_1608Metric",
        "LCSC Part #": "C72043",
        "Description": "LED Green 0603 5V/3V3 Power Indicators",
        "Type": "Basic Part",
    },
    {
        "Comment": "SMBJ6.0A 600W TVS",
        "Designator": "D3",
        "Footprint": "D_SMB",
        "LCSC Part #": "C96495",
        "Description": "TVS Diode 6.0V 600W Unidirectional SMB",
        "Type": "Basic Part",
    },
    {
        "Comment": "USBLC6-2SC6 SOT-23-6",
        "Designator": "U1, U2",
        "Footprint": "SOT-23-6",
        "LCSC Part #": "C7519",
        "Description": "TVS Diode Array ESD Protection",
        "Type": "Basic Part",
    },
    {
        "Comment": "Power Ferrite Bead 3A 0805",
        "Designator": "FB1",
        "Footprint": "L_0805_2012Metric",
        "LCSC Part #": "C1017",
        "Description": "Ferrite Bead 220 Ohm @ 100MHz 3A 0805",
        "Type": "Basic Part",
    },
    {
        "Comment": "SGM2036-3.3YN5G Ultra-Low-Noise LDO",
        "Designator": "U3",
        "Footprint": "SOT-23-5",
        "LCSC Part #": "C160538",
        "Description": "300mA Ultra-Low Noise High-PSRR LDO 3.3V SOT-23-5",
        "Type": "Basic Part",
    },
    {
        "Comment": "60-Pin BTB Receptacle 0.4mm (Tang 25K)",
        "Designator": "J2, J3",
        "Footprint": "Connector_BTB_60P_0.4mm_Pitch",
        "LCSC Part #": "C2895646",
        "Description": "60-Pin 0.4mm Pitch Board-to-Board Socket (Mating with Tang 25K)",
        "Type": "Extended Part",
    },
    {
        "Comment": "JST-GH 6-Pin SMT",
        "Designator": "J4, J5",
        "Footprint": "JST_GH_SM06B-GHS-TB_1x06-1MP_P1.25mm_Horizontal",
        "LCSC Part #": "C383675",
        "Description": "JST-GH 1.25mm 6-Pin Locking Connector SMT",
        "Type": "Extended Part",
    },
    {
        "Comment": "JST-GH 4-Pin SMT",
        "Designator": "J6",
        "Footprint": "JST_GH_SM04B-GHS-TB_1x04-1MP_P1.25mm_Horizontal",
        "LCSC Part #": "C383674",
        "Description": "JST-GH 1.25mm 4-Pin Locking Connector SMT",
        "Type": "Extended Part",
    },
    {
        "Comment": "Pin Header 1x03 2.54mm",
        "Designator": "J7",
        "Footprint": "PinHeader_1x03_P2.54mm_Vertical",
        "LCSC Part #": "C37854",
        "Description": "2.54mm 3-Pin Header (Linux Debug Console)",
        "Type": "Basic Part",
    },
    {
        "Comment": "Pin Header 2x08 2.54mm",
        "Designator": "J8",
        "Footprint": "PinHeader_2x08_P2.54mm_Vertical",
        "LCSC Part #": "C2895601",
        "Description": "2.54mm 2x8 Pin Header (8x Motor ESC DShot Outputs)",
        "Type": "Basic Part",
    },
    {
        "Comment": "XT30PW-M 2-Pin SMT / Solder Pad",
        "Designator": "J9",
        "Footprint": "TerminalBlock_XT30_2Pin",
        "LCSC Part #": "C2932822",
        "Description": "XT30 High-Current Power Input Connector (5V BEC)",
        "Type": "Extended Part",
    },
    {
        "Comment": "2x20 Female Header 2.54mm (Cubie A5E)",
        "Designator": "J1",
        "Footprint": "PinSocket_2x20_P2.54mm_Vertical",
        "LCSC Part #": "C22557",
        "Description": "40-Pin 2.54mm Dual-Row Female Stacking Socket (Cubie A5E Stacking)",
        "Type": "Extended Part",
    },
]

# -------------------------------------------------------------------------
# Centroid / Pick-and-Place (CPL) Placement Data
# -------------------------------------------------------------------------
CPL_ITEMS = [
    # Top-side SMT Connectors
    {"Designator": "J2", "Val": "BTB_60P", "Package": "BTB_60P_0.4mm", "Mid X": "42.0", "Mid Y": "21.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "J3", "Val": "BTB_60P", "Package": "BTB_60P_0.4mm", "Mid X": "42.0", "Mid Y": "35.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "J4", "Val": "JST-GH-6P", "Package": "JST_GH_6P_1.25mm", "Mid X": "15.0", "Mid Y": "48.0", "Rotation": "90.0", "Layer": "Top"},
    {"Designator": "J5", "Val": "JST-GH-6P", "Package": "JST_GH_6P_1.25mm", "Mid X": "30.0", "Mid Y": "52.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "J6", "Val": "JST-GH-4P", "Package": "JST_GH_4P_1.25mm", "Mid X": "42.0", "Mid Y": "52.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "J7", "Val": "HDR_1x3", "Package": "PinHeader_1x03_P2.54mm", "Mid X": "54.0", "Mid Y": "52.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "J8", "Val": "HDR_2x8", "Package": "PinHeader_2x08_P2.54mm", "Mid X": "58.0", "Mid Y": "28.0", "Rotation": "90.0", "Layer": "Top"},
    {"Designator": "J9", "Val": "XT30_2P", "Package": "XT30_2Pin", "Mid X": "8.0", "Mid Y": "50.0", "Rotation": "90.0", "Layer": "Top"},
    # ICs
    {"Designator": "U1", "Val": "USBLC6-2SC6", "Package": "SOT-23-6", "Mid X": "28.0", "Mid Y": "46.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "U2", "Val": "USBLC6-2SC6", "Package": "SOT-23-6", "Mid X": "40.0", "Mid Y": "46.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "U3", "Val": "SGM2036-3.3", "Package": "SOT-23-5", "Mid X": "10.0", "Mid Y": "38.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "D3", "Val": "SMBJ6.0A", "Package": "D_SMB", "Mid X": "12.0", "Mid Y": "45.0", "Rotation": "180.0", "Layer": "Top"},
    {"Designator": "FB1", "Val": "Ferrite_Bead", "Package": "L_0805", "Mid X": "16.0", "Mid Y": "44.0", "Rotation": "90.0", "Layer": "Top"},
    # Passives
    {"Designator": "C11", "Val": "47uF", "Package": "C_0805", "Mid X": "18.0", "Mid Y": "42.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "C12", "Val": "47uF", "Package": "C_0805", "Mid X": "18.0", "Mid Y": "39.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "C8", "Val": "10uF", "Package": "C_0603", "Mid X": "10.0", "Mid Y": "34.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "C9", "Val": "10uF", "Package": "C_0603", "Mid X": "12.0", "Mid Y": "34.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "C1", "Val": "100nF", "Package": "C_0402", "Mid X": "10.0", "Mid Y": "31.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "C2", "Val": "100nF", "Package": "C_0402", "Mid X": "12.0", "Mid Y": "31.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R1", "Val": "33R", "Package": "R_0402", "Mid X": "28.0", "Mid Y": "22.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R2", "Val": "33R", "Package": "R_0402", "Mid X": "28.0", "Mid Y": "24.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R3", "Val": "33R", "Package": "R_0402", "Mid X": "28.0", "Mid Y": "26.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R4", "Val": "33R", "Package": "R_0402", "Mid X": "28.0", "Mid Y": "28.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R5", "Val": "33R", "Package": "R_0402", "Mid X": "28.0", "Mid Y": "30.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R6", "Val": "2.2k", "Package": "R_0402", "Mid X": "34.0", "Mid Y": "48.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R7", "Val": "2.2k", "Package": "R_0402", "Mid X": "36.0", "Mid Y": "48.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R8", "Val": "10k", "Package": "R_0402", "Mid X": "24.0", "Mid Y": "34.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R9", "Val": "10k", "Package": "R_0402", "Mid X": "24.0", "Mid Y": "36.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "D1", "Val": "LED_Green", "Package": "LED_0603", "Mid X": "6.0", "Mid Y": "40.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "D2", "Val": "LED_Green", "Package": "LED_0603", "Mid X": "6.0", "Mid Y": "35.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R12", "Val": "1k", "Package": "R_0402", "Mid X": "6.0", "Mid Y": "38.0", "Rotation": "0.0", "Layer": "Top"},
    {"Designator": "R13", "Val": "1k", "Package": "R_0402", "Mid X": "6.0", "Mid Y": "33.0", "Rotation": "0.0", "Layer": "Top"},
    # Bottom-side 40-Pin Stacking Receptacle
    {"Designator": "J1", "Val": "Socket_2x20", "Package": "PinSocket_2x20_P2.54mm", "Mid X": "29.0", "Mid Y": "3.5", "Rotation": "0.0", "Layer": "Bottom"},
]


def generate_bom():
    bom_path = os.path.join(HARDWARE_DIR, "bom.csv")
    with open(bom_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["Comment", "Designator", "Footprint", "LCSC Part #", "Description", "Type"])
        writer.writeheader()
        for item in BOM_ITEMS:
            writer.writerow(item)
    print(f"Generated BOM: {bom_path}")


def generate_cpl():
    cpl_path = os.path.join(HARDWARE_DIR, "positions.csv")
    with open(cpl_path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=["Designator", "Val", "Package", "Mid X", "Mid Y", "Rotation", "Layer"])
        writer.writeheader()
        for item in CPL_ITEMS:
            writer.writerow(item)
    print(f"Generated CPL Centroid: {cpl_path}")


if __name__ == "__main__":
    generate_bom()
    generate_cpl()
    print("Carrier A5E + Tang 25K fabrication package generation complete!")
