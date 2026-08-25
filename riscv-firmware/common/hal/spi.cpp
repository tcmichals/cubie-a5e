#include "spi.hpp"
#include "pio.hpp"
#include "memory_map.h"

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

/* Asynchronous Transfer Context */
static std::coroutine_handle<> g_spi_coroutine = nullptr;
static uint8_t *g_spi_rx_ptr = nullptr;
static size_t   g_spi_rx_len = 0;
static int      g_spi_active_cs = -1;
static volatile bool g_spi_transfer_done = false;

void Spi0::init(uint32_t speed_hz) {
    // 1. Soft reset controller
    SPI0_GCR = (1 << 31);
    while (SPI0_GCR & (1 << 31));

    // 2. Enable Master Mode & Transmit Pause Enable
    SPI0_GCR = (1 << 1) | (1 << 0);

    // 3. Reset FIFOs
    SPI0_FCR = (1 << 31) | (1 << 15);

    // 4. Disable all interrupts initially
    SPI0_IER = 0x00;

    // 5. Configure Clock
    set_speed(speed_hz);
}

void Spi0::set_speed(uint32_t speed_hz) {
    uint32_t div = 24000000 / (2 * speed_hz);
    if (div > 0) div -= 1;
    if (div > 0xF) div = 0xF;
    SPI0_CCR = div;
}

bool Spi0::start_async_transfer(SpiMode mode, int cs_id, const uint8_t *tx, uint8_t *rx, size_t len) {
    if (len == 0) return true;

    g_spi_rx_ptr = rx;
    g_spi_rx_len = len;
    g_spi_active_cs = cs_id;
    g_spi_transfer_done = false;

    // 1. Flush & Reset FIFOs
    SPI0_FCR |= (1 << 31) | (1 << 15);

    // 2. Set Burst Counters
    SPI0_MBC = len;
    SPI0_MTC = len;
    SPI0_BCC = (mode == SpiMode::DualIO) ? ((1 << 28) | len) : len;

    // 3. Configure Transfer Control
    uint32_t tcr = (1 << 7); // SS_LEVEL=1 (manual CS)
    if (mode == SpiMode::DualIO) {
        tcr |= (1 << 28); // Dual-IO Mode Enable
    }
    SPI0_TCR = tcr;

    // 4. Assert Chip Select
    if (cs_id == 0) Pio::set_cs0(true);
    else if (cs_id == 1) Pio::set_cs1(true);

    // 5. Preload initial TX FIFO chunk (up to 64 bytes)
    size_t chunk = (len > 64) ? 64 : len;
    for (size_t i = 0; i < chunk; ++i) {
        SPI0_TXD_8 = tx ? tx[i] : 0xFF;
    }

    // 6. Enable Transfer Complete Interrupt (TC_INT_EN bit 12)
    SPI0_IER |= (1 << 12);

    // 7. Start Hardware Exchange (XCH bit 31)
    SPI0_TCR |= (1U << 31);

    return true;
}

void Spi0::handle_irq() {
    uint32_t isr = SPI0_ISR;

    // Check Transfer Complete (TC bit 12)
    if (isr & (1 << 12)) {
        // Drain remaining bytes from RX FIFO
        if (g_spi_rx_ptr) {
            size_t rx_idx = 0;
            while (rx_idx < g_spi_rx_len && (SPI0_FSR & 0xFF) > 0) {
                g_spi_rx_ptr[rx_idx++] = SPI0_RXD_8;
            }
        }

        // Deassert Chip Select
        if (g_spi_active_cs == 0) Pio::set_cs0(false);
        else if (g_spi_active_cs == 1) Pio::set_cs1(false);

        // Disable TC Interrupt and clear status
        SPI0_IER &= ~(1 << 12);
        SPI0_ISR = (1 << 12);

        g_spi_transfer_done = true;

        // Resume suspended coroutine handle
        if (g_spi_coroutine) {
            auto handle = g_spi_coroutine;
            g_spi_coroutine = nullptr;
            handle.resume();
        }
    }
}

/* AsyncTransferAwaiter Implementation */
Spi0::AsyncTransferAwaiter::AsyncTransferAwaiter(SpiMode m, int cs, const uint8_t *t, uint8_t *r, size_t l)
    : mode(m), cs_id(cs), tx(t), rx(r), len(l), completed(false) {}

bool Spi0::AsyncTransferAwaiter::await_ready() const noexcept {
    return (len == 0);
}

void Spi0::AsyncTransferAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    g_spi_coroutine = handle;
    Spi0::start_async_transfer(mode, cs_id, tx, rx, len);
}

bool Spi0::AsyncTransferAwaiter::await_resume() noexcept {
    return g_spi_transfer_done;
}

/* Synchronous Fallbacks */
bool Spi0::transceive_fpga_dual_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    start_async_transfer(SpiMode::DualIO, 0, tx_buf, rx_buf, length);
    while (!(SPI0_ISR & (1 << 12)));
    handle_irq();
    return true;
}

bool Spi0::transceive_imu_single_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    start_async_transfer(SpiMode::SingleFullDuplex, 1, tx_buf, rx_buf, length);
    while (!(SPI0_ISR & (1 << 12)));
    handle_irq();
    return true;
}

} // namespace fc::hal
