#ifndef IOPROCESSOR_HAL_SPI_HPP
#define IOPROCESSOR_HAL_SPI_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

namespace fc::hal {

enum class SpiMode {
    SingleFullDuplex,
    DualIO,
};

class Spi0 {
public:
    static void init(uint32_t speed_hz = 25000000);
    static void set_speed(uint32_t speed_hz);
    
    /* Synchronous Transceive Operations */
    static bool transceive_fpga_dual(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length);
    static bool transceive_imu_single(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length);

    /* Generic Transfer */
    static bool transfer(SpiMode mode, int cs_pin, const uint8_t *tx, uint8_t *rx, size_t len);
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_SPI_HPP
