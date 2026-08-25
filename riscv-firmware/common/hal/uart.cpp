#include "uart.hpp"
#include "timer.hpp"
#include "memory_map.h"
#include <etl/circular_buffer.h>

namespace fc::hal {

/* DesignWare 8250 UART2 Register Mapping */
#define UART2_RBR       (*(volatile uint32_t *)(UART2_BASE + 0x00))
#define UART2_THR       (*(volatile uint32_t *)(UART2_BASE + 0x00))
#define UART2_DLL       (*(volatile uint32_t *)(UART2_BASE + 0x00))
#define UART2_DLH       (*(volatile uint32_t *)(UART2_BASE + 0x04))
#define UART2_IER       (*(volatile uint32_t *)(UART2_BASE + 0x04))
#define UART2_IIR       (*(volatile uint32_t *)(UART2_BASE + 0x08))
#define UART2_FCR       (*(volatile uint32_t *)(UART2_BASE + 0x08))
#define UART2_LCR       (*(volatile uint32_t *)(UART2_BASE + 0x0C))
#define UART2_MCR       (*(volatile uint32_t *)(UART2_BASE + 0x10))
#define UART2_LSR       (*(volatile uint32_t *)(UART2_BASE + 0x14))
#define UART2_USR       (*(volatile uint32_t *)(UART2_BASE + 0x7C))

/* Interrupt Queues and Asynchronous Coroutine Context */
static etl::circular_buffer<uint8_t, 512> g_uart_rx_ring;
static std::coroutine_handle<> g_uart_rx_coroutine = nullptr;
static volatile bool g_uart_packet_ready = false;

void Uart2::init(uint32_t baud_rate, uint32_t apb_clock_hz) {
    // 1. Disable all interrupts during init
    UART2_IER = 0x00;

    // 2. Enable and reset FIFOs (64-byte depth, RX trigger at 1/2 full = 32 bytes)
    // FCR: Bit 0=FIFO Enable, Bit 1=Reset RX, Bit 2=Reset TX, Bits 7:6=0b10 (32-byte trigger)
    UART2_FCR = 0x87;

    // 3. Set Baud Rate
    set_baud(baud_rate, apb_clock_hz);

    // 4. Set Modem Control (RTS/DTR High)
    UART2_MCR = 0x03;

    // 5. Clear queues
    g_uart_rx_ring.clear();

    // 6. Enable Receiver Data Available (ERBFI bit 0) & Receiver Timeout (RTO) Interrupt
    UART2_IER = 0x01;
}

void Uart2::set_baud(uint32_t baud_rate, uint32_t apb_clock_hz) {
    uint32_t divisor = (apb_clock_hz + (8 * baud_rate)) / (16 * baud_rate);
    if (divisor == 0) divisor = 1;

    // Enable Divisor Latch Access (DLAB bit 7 in LCR)
    UART2_LCR = 0x83; // 8N1 + DLAB=1
    UART2_DLL = divisor & 0xFF;
    UART2_DLH = (divisor >> 8) & 0xFF;

    // Clear DLAB (8 bits, no parity, 1 stop bit)
    UART2_LCR = 0x03;
}

void Uart2::write_byte(uint8_t ch) {
    while (!(UART2_LSR & (1 << 5)));
    UART2_THR = ch;
}

void Uart2::write(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        write_byte(data[i]);
    }
}

void Uart2::write_str(const char *str) {
    while (*str) {
        if (*str == '\n') write_byte('\r');
        write_byte((uint8_t)*str++);
    }
}

bool Uart2::has_data() {
    return (UART2_LSR & (1 << 0)) != 0 || !g_uart_rx_ring.empty();
}

uint8_t Uart2::read_byte() {
    if (!g_uart_rx_ring.empty()) {
        uint8_t byte = 0;
        g_uart_rx_ring.pop(byte);
        return byte;
    }
    while (!(UART2_LSR & (1 << 0)));
    return (uint8_t)(UART2_RBR & 0xFF);
}

void Uart2::handle_irq() {
    uint32_t iir = UART2_IIR & 0x0F;

    // 0x04: Received Data Available (RDA) - FIFO reached threshold
    // 0x0C: Character Timeout Indication (RTO) - Line idle for 4 character times
    if (iir == 0x04 || iir == 0x0C) {
        // Drain all available bytes from hardware FIFO into ETL ring buffer
        while (UART2_LSR & (1 << 0)) {
            uint8_t ch = (uint8_t)(UART2_RBR & 0xFF);
            g_uart_rx_ring.push(ch);
        }

        // On Character Timeout (RTO), mark packet completed and wake coroutine
        if (iir == 0x0C) {
            g_uart_packet_ready = true;
            if (g_uart_rx_coroutine) {
                auto handle = g_uart_rx_coroutine;
                g_uart_rx_coroutine = nullptr;
                handle.resume();
            }
        }
    }
}

/* AsyncRxPacketAwaiter Implementation */
Uart2::AsyncRxPacketAwaiter::AsyncRxPacketAwaiter(uint8_t *buf, size_t len)
    : dest_buf(buf), max_len(len), bytes_received(0) {}

bool Uart2::AsyncRxPacketAwaiter::await_ready() const noexcept {
    return g_uart_packet_ready && !g_uart_rx_ring.empty();
}

void Uart2::AsyncRxPacketAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    g_uart_rx_coroutine = handle;
    g_uart_packet_ready = false;
}

size_t Uart2::AsyncRxPacketAwaiter::await_resume() noexcept {
    bytes_received = 0;
    while (!g_uart_rx_ring.empty() && bytes_received < max_len) {
        uint8_t byte = 0;
        if (g_uart_rx_ring.pop(byte)) {
            dest_buf[bytes_received++] = byte;
        }
    }
    g_uart_packet_ready = false;
    return bytes_received;
}

} // namespace fc::hal
