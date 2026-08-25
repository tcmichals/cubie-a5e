#ifndef IOPROCESSOR_HAL_PIO_HPP
#define IOPROCESSOR_HAL_PIO_HPP

#include <stdint.h>
#include <stdbool.h>

namespace fc::hal {

class Pio {
public:
    static void init();
    
    /* Port C Chip Select Control (when in Manual GPIO Mode) */
    static inline void set_cs0(bool active); // Active = LOW
    static inline void set_cs1(bool active); // Active = LOW

    /* Interrupt / Status Checks */
    static inline bool get_fpga_frame_ready(); // Pin 22
    static inline bool get_imu_drdy();         // Pin 29
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_PIO_HPP
