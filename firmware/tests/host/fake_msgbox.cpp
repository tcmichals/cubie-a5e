#include "fake_msgbox.hpp"
#include "hal/msgbox.hpp"

namespace fake_msgbox {

uint32_t full_sends_left = 0;
uint32_t sends = 0;
uint32_t rx_pending = 0;
std::function<void()> on_send;

void reset() {
    full_sends_left = 0;
    sends = 0;
    rx_pending = 0;
    on_send = nullptr;
}

} // namespace fake_msgbox

namespace hal {

void MsgBox::init(bool) noexcept {}

bool MsgBox::send(Channel, uint32_t) noexcept {
    if (fake_msgbox::full_sends_left > 0) {
        fake_msgbox::full_sends_left--;
        return false;
    }
    fake_msgbox::sends++;
    if (fake_msgbox::on_send) {
        fake_msgbox::on_send();
    }
    return true;
}

bool MsgBox::is_rx_pending(Channel) noexcept {
    return fake_msgbox::rx_pending > 0;
}

std::optional<uint32_t> MsgBox::receive(Channel) noexcept {
    if (fake_msgbox::rx_pending == 0) {
        return std::nullopt;
    }
    fake_msgbox::rx_pending--;
    return 0u;
}

} // namespace hal
