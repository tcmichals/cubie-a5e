#ifndef IOPROCESSOR_HAL_CCU_HPP
#define IOPROCESSOR_HAL_CCU_HPP

#include <stdint.h>

namespace fc::hal {

class Ccu {
public:
    static void init();
    static void enable_spi0();
    static void enable_uart2();
    static void enable_msgbox();
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_CCU_HPP
