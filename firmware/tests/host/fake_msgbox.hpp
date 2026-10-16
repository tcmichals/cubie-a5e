/*
 * fake_msgbox.hpp - Host-side stand-in for hal::MsgBox, for unit tests.
 *
 * send() fails while `full_sends_left` > 0 (a full FIFO), then succeeds and
 * calls `on_send` so a test can play the Linux side (for example, free a
 * host RX buffer when it sees a doorbell).
 */
#pragma once

#include <cstdint>
#include <functional>

namespace fake_msgbox {

extern uint32_t full_sends_left;   // sends to report as "FIFO full" before accepting
extern uint32_t sends;             // accepted sends
extern uint32_t rx_pending;        // entries queued for the E906 to drain
extern std::function<void()> on_send;

void reset();

} // namespace fake_msgbox
