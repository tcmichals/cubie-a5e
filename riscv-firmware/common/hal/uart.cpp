#include "uart.hpp"
#include "timer.hpp"
#include "../../include/memory_map.h"

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

void Uart2::init(uint32_t baud_rate, uint32_t apb_clock_hz) {
    // 1. Disable all interrupts
    UART2_IER = 0x00;

    // 2. Enable and reset FIFOs (64-byte depth)
    UART2_FCR = 0x07; // FIFO Enable, Reset RX FIFO, Reset TX FIFO

    // 3. Set Baud Rate
    set_baud(baud_rate, apb_clock_hz);

    // 4. Set Modem Control (RTS/DTR High)
    UART2_MCR = 0x03;
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
    // Poll Line Status Register until Transmitter Holding Register Empty (THRE bit 5)
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
    // Bit 0 of LSR is Data Ready (DR)
    return (UART2_LSR & (1 << 0)) != 0;
}

uint8_t Uart2::read_byte() {
    while (!has_data());
    return (uint8_t)(UART2_RBR & 0xFF);
}

size_t Uart2::read_frame_timeout(uint8_t *buf, size_t max_len, uint32_t char_timeout_us) {
    size_t count = 0;
    if (max_len == 0 || buf == nullptr) return 0;

    // Wait for the very first character (or return 0 if no initial data)
    if (!has_data()) return 0;

    buf[count++] = (uint8_t)(UART2_RBR & 0xFF);
    uint64_t last_char_time = Timer::get_time_us();

    while (count < max_len) {
        if (has_data()) {
            buf[count++] = (uint8_t)(UART2_RBR & 0xFF);
            last_char_time = Timer::get_time_us();
        } else {
            // Check character idle timeout
            if (Timer::get_time_us() - last_char_time >= char_timeout_us) {
                break; // Line has gone quiet -> full packet received
            }
        }
    }

    return count;
}

} // namespace fc::hal
