#include "spi.hpp"
#include "pio.hpp"
#include "../../include/memory_map.h"

namespace fc::hal {

/* SPI0 Hardware Register Mapping */
#define SPI0_GCR        (*(volatile uint32_t *)(SPI0_BASE + 0x04))
#define SPI0_TCR        (*(volatile uint32_t *)(SPI0_BASE + 0x08))
#define SPI0_IER        (*(volatile uint32_t *)(SPI0_BASE + 0x10))
#define SPI0_ISR        (*(volatile uint32_t *)(SPI0_BASE + 0x14))
#define SPI0_FCR        (*(volatile uint32_t *)(SPI0_BASE + 0x18))
#define SPI0_FSR        (*(volatile uint32_t *)(SPI0_BASE + 0x1C))
#define SPI0_CCR        (*(volatile uint32_t *)(SPI0_BASE + 0x24))
#define SPI0_MBC        (*(volatile uint32_t *)(SPI0_BASE + 0x30))
#define SPI0_MTC        (*(volatile uint32_t *)(SPI0_BASE + 0x34))
#define SPI0_BCC        (*(volatile uint32_t *)(SPI0_BASE + 0x38))
#define SPI0_TXD_8      (*(volatile uint8_t  *)(SPI0_BASE + 0x200))
#define SPI0_RXD_8      (*(volatile uint8_t  *)(SPI0_BASE + 0x300))

void Spi0::init(uint32_t speed_hz) {
    // 1. Soft reset controller
    SPI0_GCR = (1 << 31); // Soft Reset
    while (SPI0_GCR & (1 << 31));

    // 2. Enable Master Mode & Transmit Pause Enable
    SPI0_GCR = (1 << 1) | (1 << 0); // Master Mode (bit 1), Module Enable (bit 0)

    // 3. Reset FIFOs
    SPI0_FCR = (1 << 31) | (1 << 15); // Reset TX/RX FIFO

    // 4. Configure Clock
    set_speed(speed_hz);
}

void Spi0::set_speed(uint32_t speed_hz) {
    // Clock source is 24MHz OSC
    // CDR2 divider: freq = 24MHz / (2 * (N + 1))
    uint32_t div = 24000000 / (2 * speed_hz);
    if (div > 0) div -= 1;
    if (div > 0xF) div = 0xF;

    SPI0_CCR = div; // CDR2 divider mode
}

bool Spi0::transfer(SpiMode mode, int cs_id, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (len == 0) return true;

    // Flush and reset FIFOs
    SPI0_FCR |= (1 << 31) | (1 << 15);

    // Setup Master Burst Counter and Master Transmit Counter
    SPI0_MBC = len;
    SPI0_MTC = len;
    SPI0_BCC = (mode == SpiMode::DualIO) ? ((1 << 28) | len) : len; // Dual-IO bit in BCC

    // Configure Transfer Control Register
    uint32_t tcr = (1 << 7); // Default CPOL=0, CPHA=0, SS_LEVEL=1 (manual CS)
    if (mode == SpiMode::DualIO) {
        tcr |= (1 << 28); // Dual Mode Enable
    }
    SPI0_TCR = tcr;

    // Assert CS (Active LOW)
    if (cs_id == 0) Pio::set_cs0(true);
    else if (cs_id == 1) Pio::set_cs1(true);

    // Push TX bytes to FIFO
    size_t tx_idx = 0;
    size_t rx_idx = 0;

    // Start Exchange (XCH bit 31)
    SPI0_TCR |= (1U << 31);

    while (tx_idx < len || rx_idx < len) {
        // Push to TX FIFO while not full (64 bytes depth)
        while (tx_idx < len && ((SPI0_FSR >> 16) & 0xFF) < 64) {
            uint8_t byte = tx ? tx[tx_idx++] : 0xFF;
            SPI0_TXD_8 = byte;
        }

        // Pull from RX FIFO while not empty
        while (rx_idx < len && (SPI0_FSR & 0xFF) > 0) {
            uint8_t byte = SPI0_RXD_8;
            if (rx) rx[rx_idx] = byte;
            rx_idx++;
        }
    }

    // Wait for transfer complete (XCH bit clears)
    while (SPI0_TCR & (1U << 31));

    // Deassert CS (High)
    if (cs_id == 0) Pio::set_cs0(false);
    else if (cs_id == 1) Pio::set_cs1(false);

    return true;
}

bool Spi0::transceive_fpga_dual(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    return transfer(SpiMode::DualIO, 0 /* CS0 */, tx_buf, rx_buf, length);
}

bool Spi0::transceive_imu_single(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    return transfer(SpiMode::SingleFullDuplex, 1 /* CS1 */, tx_buf, rx_buf, length);
}

} // namespace fc::hal
