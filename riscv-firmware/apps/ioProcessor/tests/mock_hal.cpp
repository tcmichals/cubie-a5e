#include "msgbox.hpp"
#include <stdint.h>

namespace fc::hal {

void MsgBox::init() {}

void MsgBox::notify_host(uint32_t message_id) {
    (void)message_id;
}

bool MsgBox::has_host_notification(uint32_t *message_id) {
    if (message_id) *message_id = 0;
    return false;
}

} // namespace fc::hal
