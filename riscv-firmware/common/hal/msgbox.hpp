#ifndef IOPROCESSOR_HAL_MSGBOX_HPP
#define IOPROCESSOR_HAL_MSGBOX_HPP

#include <stdint.h>
#include <stdbool.h>

namespace fc::hal {

class MsgBox {
public:
    static void init();

    /* Ring Doorbell to Linux Host (Channel 0) */
    static void notify_host(uint32_t message_id = 0x01);

    /* Check if Host rang our Doorbell (Channel 1) */
    static bool has_host_notification(uint32_t *message_id = nullptr);
};

} // namespace fc::hal

#endif // IOPROCESSOR_HAL_MSGBOX_HPP
