/*
 * main.cpp - testStringBinaryTrace0: Combined ASCII String & Binary Telemetry Logging
 *
 * Target: Allwinner T527 XuanTie E907 (RV32IMAFDC @ 200 MHz)
 *
 * Demonstrates:
 * 1. Hardware Floating Point Unit (FPU) computation: single (F) and double (D) precision.
 * 2. Combining human-readable ASCII string formatting with packed binary telemetry packets.
 * 3. Streaming binary structures to Dedicated MCU SRAM C (0x07131000) for zero-copy direct IPC/telemetry.
 * 4. Outputting rich telemetry frames to remoteproc trace0 buffer and S_UART0.
 */

#include <stdint.h>
#include <stdbool.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"

// Packed Binary Telemetry Frame (36 bytes)
struct __attribute__((packed)) TelemetryPacket {
    uint32_t header_magic;  // 0x54454C4D ("TELM")
    uint32_t sequence;      // Packet sequence number
    uint32_t uptime_ms;     // Milliseconds timestamp
    float    accel_x;       // Hardware float (F) X acceleration
    float    accel_y;       // Hardware float (F) Y acceleration
    float    accel_z;       // Hardware float (F) Z acceleration
    double   sine_wave;     // Hardware double (D) mathematical calculation
    uint16_t checksum;      // XOR checksum
    uint16_t tail_magic;    // 0x55AA
};

// Shared binary packet placed by Linker Script in section .sram_c
__attribute__((used, section(".sram_c"), aligned(4)))
static volatile TelemetryPacket sram_telemetry_packet;

static void write_packet_to_sram(const TelemetryPacket &pkt) {
    const uint32_t *src = reinterpret_cast<const uint32_t *>(&pkt);
    volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(&sram_telemetry_packet);
    size_t words = sizeof(TelemetryPacket) / sizeof(uint32_t);
    for (size_t i = 0; i < words; ++i) {
        dst[i] = src[i];
    }
}

// Fast polynomial approximation for sine using double-precision FPU
static double compute_sin(double x) {
    const double pi = 3.141592653589793;
    while (x > pi) x -= 2.0 * pi;
    while (x < -pi) x += 2.0 * pi;
    // Taylor series: x - x^3/6 + x^5/120 - x^7/5040
    double x2 = x * x;
    double x3 = x * x2;
    double x5 = x3 * x2;
    double x7 = x5 * x2;
    return x - (x3 / 6.0) + (x5 / 120.0) - (x7 / 5040.0);
}

int main(void) {
    // 1. Initialize HAL
    hal::Trace::init(/*enable_serial_mirror=*/true);
    hal::Timer::init();

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E907 String & Binary Trace0 Test       \n");
    hal::Trace::puts("  Features: RV32IMAFDC Hardware FPU (Float & Double Precision)  \n");
    hal::Trace::printf("  SRAM Binary Packet: %p (%u bytes in .sram_c)                  \n",
                       (void *)&sram_telemetry_packet, (uint32_t)sizeof(TelemetryPacket));
    hal::Trace::puts("================================================================\n");

    TelemetryPacket pkt;
    uint32_t seq = 0;
    double phase = 0.0;

    // 2. Periodic Telemetry Stream Loop (every 500ms)
    while (1) {
        seq++;
        phase += 0.1;

        // Perform hardware floating point math using FPU
        float ax = 0.015f * static_cast<float>(seq);
        float ay = -0.008f * static_cast<float>(seq);
        float az = 9.80665f + 0.05f * static_cast<float>(compute_sin(phase));
        double s_val = compute_sin(phase);

        // Populate packed binary structure
        pkt.header_magic = 0x54454C4D; // "TELM"
        pkt.sequence     = seq;
        pkt.uptime_ms    = seq * 500;
        pkt.accel_x      = ax;
        pkt.accel_y      = ay;
        pkt.accel_z      = az;
        pkt.sine_wave    = s_val;
        pkt.checksum     = static_cast<uint16_t>(seq ^ 0xA5A5);
        pkt.tail_magic   = 0x55AA;

        // 1. Mirror binary structure into Shared SRAM C (zero-copy IPC)
        write_packet_to_sram(pkt);

        // 2. Output human-readable ASCII string frame to trace0
        hal::Trace::printf("STRING: [TELM #%u] Accel: (%.3f, %.3f, %.3f) | FPU Sin: %.4f | SRAM: %p\n",
                           seq, ax, ay, az, static_cast<float>(s_val), (void *)&sram_telemetry_packet);

        // 3. Output raw binary packet directly into trace0 with framing tag
        hal::Trace::puts("BINARY:");
        hal::Trace::write(&pkt, sizeof(pkt));
        hal::Trace::putc('\n');

        // 4. Output human-readable hex dump of the binary struct into trace0
        hal::Trace::puts("HEXDUMP:\n");
        hal::Trace::dump_hex(&pkt, sizeof(pkt), 0);

        // Delay 500ms
        hal::Timer::delay_ms(500);
    }

    return 0;
}
