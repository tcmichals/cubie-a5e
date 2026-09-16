#ifndef IOPROCESSOR_HAL_PIO_HPP
#define IOPROCESSOR_HAL_PIO_HPP

#include <stdint.h>
#include <stdbool.h>
#include "memory_map.h"

namespace fc::hal {

#define PC_DATA_REG (*(volatile uint32_t *)(PIO_BASE + 0x0070))

class Pio {
public:
    static void init();
    
    /* Port C Chip Select Control (when in Manual GPIO Mode) */
    static inline void set_cs0(bool active) {
        if (active)
            PC_DATA_REG &= ~(1 << 3); // Pull LOW (Active)
        else
            PC_DATA_REG |=  (1 << 3); // Pull HIGH (Inactive)
    }

    static inline void set_cs1(bool active) {
        if (active)
            PC_DATA_REG &= ~(1 << 7); // Pull LOW (Active)
        else
            PC_DATA_REG |=  (1 << 7); // Pull HIGH (Inactive)
    }

    /* Interrupt / Status Checks */
    static inline bool get_fpga_frame_ready() {
        return (PC_DATA_REG & (1 << 6)) != 0; // Pin 22 (PC6)
    }

    static inline bool get_imu_drdy() {
        return (PC_DATA_REG & (1 << 8)) != 0; // Pin 29 (PC8)
    }
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_PIO_HPP
