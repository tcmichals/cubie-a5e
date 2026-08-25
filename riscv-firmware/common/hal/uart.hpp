#ifndef IOPROCESSOR_HAL_UART_HPP
#define IOPROCESSOR_HAL_UART_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace fc::hal {

class Uart2 {
public:
    static void init(uint32_t baud_rate = 115200, uint32_t apb_clock_hz = 24000000);
    static void set_baud(uint32_t baud_rate, uint32_t apb_clock_hz = 24000000);

    /* Direct Transmission */
    static void write_byte(uint8_t ch);
    static void write(const uint8_t *data, size_t len);
    static void write_str(const char *str);

    /* Direct Reception */
    static bool has_data();
    static uint8_t read_byte();
    
    /* Timed Frame Ingestion with Character Idle Timeout */
    static size_t read_frame_timeout(uint8_t *buf, size_t max_len, uint32_t char_timeout_us);
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_UART_HPP
