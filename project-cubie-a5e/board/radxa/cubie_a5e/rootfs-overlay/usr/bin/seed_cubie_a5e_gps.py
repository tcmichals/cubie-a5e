#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-GPS-01] docs/DESIGN_SPECIFICATION.md#spec-gps-01
"""
seed_cubie_a5e_gps.py - Radxa Cubie A5E u-blox NEO-M8N GPS & Compass Seeder/Verifier
------------------------------------------------------------------------------------
Hardware bring-up and seeding tool for u-blox NEO-M8N GPS and I2C Compass
connected to the Radxa Cubie A5E 40-pin header.

Hardware Pinout (40-Pin Header):
  Pin 1 (or 2/4) : 3.3V Power / 5V -> GPS VCC
  Pin 6, 14       : GND             -> GPS GND
  Pin 3           : I2C1-SDA (TWI1) -> GPS/Compass SDA
  Pin 5           : I2C1-SCL (TWI1) -> GPS/Compass SCL
  Pin 11          : UART2-TX (PB0)  -> GPS RX
  Pin 13          : UART2-RX (PB1)  -> GPS TX

Features:
  1. Serial NMEA/UBX Reader & Parser (9600 baud factory default, or 38400/115200).
  2. I2C Bus Scanner & u-blox DDC Reader (Address 0x42: registers 0xFD/0xFE and 0xFF).
  3. Compass Prober (Addresses 0x1E, 0x0D, 0x0C: HMC5883L, QMC5883L, IST8310).
  4. UBX M8N Configuration Seeder (Configure 5Hz/10Hz rate, enable NAV-PVT).
  5. Studio TLP 64B Telemetry Broadcaster (Streams Tlp64 frames over UDP to Studio).
  6. Software Mock Mode (--mock) for standalone simulation and CI verification.

Usage:
  # Verification in Mock Mode:
  python3 tools/seed_cubie_a5e_gps.py --mock

  # Live Hardware Serial Ingestion (on Cubie A5E):
  python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyS2 --baud 9600

  # Live Hardware I2C Probe:
  python3 tools/seed_cubie_a5e_gps.py --i2c 1

  # Full Diagnostic & Seed to 5Hz Navigation Rate:
  python3 tools/seed_cubie_a5e_gps.py --serial /dev/ttyS2 --baud 9600 --i2c 1 --configure-5hz
"""

import sys
import os
import time
import struct
import socket
import argparse
from typing import Optional, Tuple, Dict, Any

# -----------------------------------------------------------------------------
# UBX Binary Frame Definitions
# -----------------------------------------------------------------------------
UBX_SYNC_1 = 0xB5
UBX_SYNC_2 = 0x62

# UBX Classes & IDs
UBX_CLASS_NAV = 0x01
UBX_ID_NAV_PVT = 0x07

UBX_CLASS_CFG = 0x06
UBX_ID_CFG_PRT = 0x00
UBX_ID_CFG_RATE = 0x08
UBX_ID_CFG_MSG = 0x01

# I2C Addresses
UBLOX_I2C_ADDR = 0x42
UBLOX_REG_AVAIL_MSB = 0xFD
UBLOX_REG_AVAIL_LSB = 0xFE
UBLOX_REG_STREAM = 0xFF

COMPASS_ADDR_HMC = 0x1E
COMPASS_ADDR_QMC = 0x0D
COMPASS_ADDR_IST = 0x0E

def compute_ubx_checksum(msg_class: int, msg_id: int, payload: bytes) -> Tuple[int, int]:
    """Calculate 2-byte Fletcher checksum (CK_A, CK_B) over Class, ID, Length, and Payload."""
    ck_a = 0
    ck_b = 0
    header = struct.pack('<BBH', msg_class, msg_id, len(payload))
    for b in header + payload:
        ck_a = (ck_a + b) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
    return ck_a, ck_b

def build_ubx_packet(msg_class: int, msg_id: int, payload: bytes) -> bytes:
    """Construct full UBX frame with 0xB5 0x62 header and 2-byte checksum."""
    ck_a, ck_b = compute_ubx_checksum(msg_class, msg_id, payload)
    return struct.pack('<BBBBH', UBX_SYNC_1, UBX_SYNC_2, msg_class, msg_id, len(payload)) + payload + bytes([ck_a, ck_b])

def build_cfg_rate_packet(rate_ms: int = 200) -> bytes:
    """Configure UBX-CFG-RATE (rate_ms: e.g. 200ms = 5Hz, 100ms = 10Hz, 1000ms = 1Hz)."""
    # Payload: measRate (U2), navRate (U2=1), timeRef (U2=1 for GPS time)
    payload = struct.pack('<HHH', rate_ms, 1, 1)
    return build_ubx_packet(UBX_CLASS_CFG, UBX_ID_CFG_RATE, payload)

def build_cfg_nav_pvt_packet(rate: int = 1) -> bytes:
    """Enable UBX-NAV-PVT output rate on current port."""
    # Payload: msgClass (U1), msgID (U1), rate (U1)
    payload = struct.pack('<BBB', UBX_CLASS_NAV, UBX_ID_NAV_PVT, rate)
    return build_ubx_packet(UBX_CLASS_CFG, UBX_ID_CFG_MSG, payload)

# -----------------------------------------------------------------------------
# 64-Byte PCIe-Style TLP Packet Construction
# -----------------------------------------------------------------------------
TLP_TAG_GPS = 0x02

def make_gps_tlp64(lat_1e7: int, lon_1e7: int, alt_mm: int, speed_mm_s: int, heading_1e5: int, sats: int, fix_type: int) -> bytes:
    """Encode GPS fix into standard AbstractX 64-byte TLP format for Studio."""
    # Header: Type=0x01 (MemWr), Channel=0x02 (TELEMETRY), Tag=TLP_TAG_GPS, Addr=0x50000000
    header = struct.pack('>BBHI', 0x01, 0x02, TLP_TAG_GPS, 0x50000000)
    # Payload (28 bytes packed data):
    payload = struct.pack('<iiiiiBBH', lat_1e7, lon_1e7, alt_mm, speed_mm_s, heading_1e5, sats, fix_type, 0)
    # Total 64 bytes
    return header + payload + b'\x00' * (56 - len(payload))

# -----------------------------------------------------------------------------
# NMEA Sentence Parser
# -----------------------------------------------------------------------------
def parse_nmea_sentence(line: str) -> Optional[Dict[str, Any]]:
    """Parse basic $GNGGA / $GNRMC NMEA strings and extract fix details."""
    line = line.strip()
    if not line.startswith('$'):
        return None
    
    parts = line.split('*')[0].split(',')
    sentence = parts[0]

    try:
        if sentence in ('$GNGGA', '$GPGGA') and len(parts) >= 15:
            # Lat: DDMM.MMMMM
            raw_lat = parts[2]
            lat_dir = parts[3]
            raw_lon = parts[4]
            lon_dir = parts[5]
            fix_quality = int(parts[6]) if parts[6] else 0
            num_sats = int(parts[7]) if parts[7] else 0
            alt_m = float(parts[9]) if parts[9] else 0.0

            lat_deg = 0.0
            if raw_lat and len(raw_lat) >= 4:
                lat_deg = float(raw_lat[:2]) + float(raw_lat[2:]) / 60.0
                if lat_dir == 'S': lat_deg = -lat_deg

            lon_deg = 0.0
            if raw_lon and len(raw_lon) >= 5:
                lon_deg = float(raw_lon[:3]) + float(raw_lon[3:]) / 60.0
                if lon_dir == 'W': lon_deg = -lon_deg

            return {
                'type': 'GGA',
                'lat_deg': lat_deg,
                'lon_deg': lon_deg,
                'lat_1e7': int(lat_deg * 1e7),
                'lon_1e7': int(lon_deg * 1e7),
                'alt_mm': int(alt_m * 1000),
                'sats': num_sats,
                'fix': fix_quality > 0,
                'fix_type': 3 if fix_quality >= 1 else 0,
                'raw': line
            }

        elif sentence in ('$GNRMC', '$GPRMC') and len(parts) >= 12:
            status = parts[2]
            speed_knots = float(parts[7]) if parts[7] else 0.0
            heading_deg = float(parts[8]) if parts[8] else 0.0
            speed_mm_s = int(speed_knots * 514.444)
            heading_1e5 = int(heading_deg * 1e5)

            return {
                'type': 'RMC',
                'valid': status == 'A',
                'speed_mm_s': speed_mm_s,
                'heading_1e5': heading_1e5,
                'raw': line
            }
    except Exception:
        pass
    return None

# -----------------------------------------------------------------------------
# Hardware I2C Scanner & DDC Prober
# -----------------------------------------------------------------------------
def probe_i2c_bus(bus_num: int, mock: bool = False) -> Dict[str, Any]:
    """Scan I2C bus and probe u-blox DDC (0x42) and compass (0x1E/0x0D)."""
    results = {
        'devices_found': [],
        'ublox_detected': False,
        'ublox_pending_bytes': 0,
        'compass_detected': False,
        'compass_model': 'None'
    }

    if mock:
        print("[MOCK I2C] Scanning I2C bus 1...")
        results['devices_found'] = [0x42, 0x0D]
        results['ublox_detected'] = True
        results['ublox_pending_bytes'] = 84
        results['compass_detected'] = True
        results['compass_model'] = 'QMC5883L (0x0D)'
        print("  -> Found 0x42: u-blox NEO-M8N DDC Port (84 bytes waiting in buffer)")
        print("  -> Found 0x0D: QMC5883L 3-Axis Electronic Compass")
        return results

    try:
        from smbus2 import SMBus
        with SMBus(bus_num) as bus:
            print(f"[+] Scanning I2C bus {bus_num} (Pins 3 & 5)...")
            for addr in range(0x03, 0x78):
                try:
                    bus.read_byte(addr)
                    results['devices_found'].append(addr)
                    print(f"  [ACK] Device detected at 0x{addr:02X}")
                except Exception:
                    pass

            # 1. Check u-blox M8N DDC (0x42)
            if UBLOX_I2C_ADDR in results['devices_found']:
                results['ublox_detected'] = True
                try:
                    msb = bus.read_byte_data(UBLOX_I2C_ADDR, UBLOX_REG_AVAIL_MSB)
                    lsb = bus.read_byte_data(UBLOX_I2C_ADDR, UBLOX_REG_AVAIL_LSB)
                    avail = (msb << 8) | lsb
                    results['ublox_pending_bytes'] = avail
                    print(f"  -> u-blox NEO-M8N DDC responding at 0x42! ({avail} bytes in FIFO)")
                except Exception as e:
                    print(f"  -> u-blox 0x42 read error: {e}")

            # 2. Check Compass
            if COMPASS_ADDR_QMC in results['devices_found']:
                results['compass_detected'] = True
                results['compass_model'] = 'QMC5883L (0x0D)'
                print("  -> QMC5883L Compass detected at 0x0D!")
            elif COMPASS_ADDR_HMC in results['devices_found']:
                results['compass_detected'] = True
                results['compass_model'] = 'HMC5883L (0x1E)'
                print("  -> HMC5883L Compass detected at 0x1E!")

    except ImportError:
        print("[!] smbus2 module not installed; install via 'pip install smbus2' or use Linux /dev/i2c ioctl.")
    except Exception as e:
        print(f"[!] I2C Bus Error on bus {bus_num}: {e}")

    return results

# -----------------------------------------------------------------------------
# Main Bring-Up & Seeding Routine
# -----------------------------------------------------------------------------
def run_seed_and_verify(serial_dev: Optional[str], baud: int, i2c_bus: Optional[int], 
                        configure_5hz: bool, stream_tlp: bool, mock: bool, max_samples: int = 10):
    print("=================================================================")
    print(" Radxa Cubie A5E u-blox NEO-M8N GPS & Compass Bring-Up Seeder")
    print("=================================================================")
    print(f"Mode          : {'MOCK SIMULATION' if mock else 'REAL SILICON'}")
    print(f"Serial Port   : {serial_dev or 'Disabled'} @ {baud} baud")
    print(f"I2C Bus       : Bus {i2c_bus} (Pins 3 & 5)" if i2c_bus is not None else "I2C: Disabled")
    print(f"Configure 5Hz : {configure_5hz}")
    print(f"Stream TLP    : {stream_tlp} (UDP 127.0.0.1:9872)")
    print("=================================================================\n")

    # 1. Probe I2C Bus
    if i2c_bus is not None or mock:
        bus_id = i2c_bus if i2c_bus is not None else 1
        i2c_res = probe_i2c_bus(bus_id, mock=mock)
        print(f"[I2C Status] Found: {[hex(x) for x in i2c_res['devices_found']]}")
        print(f"             u-blox: {i2c_res['ublox_detected']} ({i2c_res['ublox_pending_bytes']} B waiting)")
        print(f"             Compass: {i2c_res['compass_model']}\n")

    # 2. Setup UDP Socket if streaming TLP frames
    udp_sock = None
    if stream_tlp:
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

    # 3. Serial Port GPS Seeding & Ingestion
    if mock:
        print("[MOCK GPS] Simulating incoming u-blox stream at 9600 baud...")
        sample_lat = 37.7749
        sample_lon = -122.4194
        sample_alt = 15.2

        for i in range(max_samples):
            time.sleep(0.2)
            nmea_gga = f"$GNGGA,123519,{sample_lat:07.4f},N,{abs(sample_lon):08.4f},W,1,08,0.9,{sample_alt},M,0.0,M,,*47"
            parsed = parse_nmea_sentence(nmea_gga)
            if parsed:
                print(f"[{i+1}/{max_samples}] Lat: {parsed['lat_deg']:.6f}°, Lon: {parsed['lon_deg']:.6f}°, Alt: {parsed['alt_mm']/1000.0:.1f} m, Sats: {parsed['sats']}")
                if udp_sock:
                    tlp = make_gps_tlp64(parsed['lat_1e7'], parsed['lon_1e7'], parsed['alt_mm'], 1200, 4500000, parsed['sats'], parsed['fix_type'])
                    udp_sock.sendto(tlp, ('127.0.0.1', 9872))
            sample_lat += 0.00001
            sample_lon += 0.00001
        print("\n[SUCCESS] Mock GPS & I2C bring-up cycle completed cleanly!")
        return

    if serial_dev:
        try:
            import serial
            ser = serial.Serial(serial_dev, baudrate=baud, timeout=1.0)
            print(f"[+] Connected to {serial_dev} at {baud} baud.")

            if configure_5hz:
                print("[+] Seeding u-blox M8N configuration: Setting 5 Hz rate & enabling NAV-PVT...")
                ser.write(build_cfg_rate_packet(rate_ms=200)) # 5 Hz
                time.sleep(0.05)
                ser.write(build_cfg_nav_pvt_packet(rate=1))
                time.sleep(0.05)
                print("[+] UBX configuration packets kicked to M8N!")

            print("[+] Listening for GPS NMEA/UBX stream (Ctrl-C to stop)...\n")
            samples = 0
            while samples < max_samples:
                line = ser.readline().decode('ascii', errors='ignore')
                if not line:
                    continue
                parsed = parse_nmea_sentence(line)
                if parsed and parsed.get('type') == 'GGA':
                    samples += 1
                    status_str = f"FIX ({parsed['sats']} Sats)" if parsed['fix'] else "NO FIX (Searching...)"
                    print(f"[{samples}/{max_samples}] {status_str} | Lat: {parsed['lat_deg']:.6f}°, Lon: {parsed['lon_deg']:.6f}°, Alt: {parsed['alt_mm']/1000.0:.1f} m")
                    if udp_sock and parsed['fix']:
                        tlp = make_gps_tlp64(parsed['lat_1e7'], parsed['lon_1e7'], parsed['alt_mm'], 0, 0, parsed['sats'], parsed['fix_type'])
                        udp_sock.sendto(tlp, ('127.0.0.1', 9872))
                elif not line.startswith('$'):
                    # Binary or non-standard frame
                    pass

        except ImportError:
            print("[!] pyserial module not found; install with 'pip install pyserial'.")
        except Exception as e:
            print(f"[!] Serial Port Error on {serial_dev}: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Radxa Cubie A5E u-blox NEO-M8N GPS Seeder & Bring-Up Tool")
    parser.add_argument("--serial", default=None, help="Serial device path (e.g. /dev/ttyS2 or /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=9600, help="Baud rate (default: 9600 factory default)")
    parser.add_argument("--i2c", type=int, default=None, help="I2C bus number (e.g. 1 for Pins 3 & 5)")
    parser.add_argument("--configure-5hz", action="store_true", help="Seed M8N to 5Hz navigation rate")
    parser.add_argument("--stream-tlp", action="store_true", help="Broadcast decoded fixes as 64B TLPs over UDP:9872 to Studio")
    parser.add_argument("--mock", action="store_true", help="Run simulated GPS and I2C verification")
    parser.add_argument("--samples", type=int, default=10, help="Number of sample fixes to display")
    args = parser.parse_args()

    # If no hardware arguments provided, run mock verification
    if not args.serial and args.i2c is None and not args.mock:
        print("[!] No hardware arguments specified; defaulting to --mock mode.\n")
        args.mock = True

    run_seed_and_verify(
        serial_dev=args.serial,
        baud=args.baud,
        i2c_bus=args.i2c,
        configure_5hz=args.configure_5hz,
        stream_tlp=args.stream_tlp,
        mock=args.mock,
        max_samples=args.samples
    )
