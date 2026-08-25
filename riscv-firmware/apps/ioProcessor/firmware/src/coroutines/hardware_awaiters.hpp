#ifndef IOPROCESSOR_HARDWARE_AWAITERS_HPP
#define IOPROCESSOR_HARDWARE_AWAITERS_HPP

#include <coroutine>
#include <stdint.h>
#include "../hal/timer.hpp"
#include "../hal/uart.hpp"
#include "../ipc/ringbuffer.hpp"

namespace fc::coroutines {

// Non-blocking timer awaiter
struct SleepUsAwaiter {
    explicit SleepUsAwaiter(uint32_t us) 
        : target_cycles_(hal::Timer::get_cycles() + (uint64_t)us * (CPU_FREQ_HZ / 1000000ULL)) {}

    bool await_ready() const noexcept {
        return hal::Timer::get_cycles() >= target_cycles_;
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    void await_resume() const noexcept {}

private:
    uint64_t target_cycles_;
};

inline SleepUsAwaiter sleep_us(uint32_t us) {
    return SleepUsAwaiter(us);
}

inline SleepUsAwaiter sleep_ms(uint32_t ms) {
    return SleepUsAwaiter(ms * 1000);
}

// Non-blocking UART frame awaiter
struct UartRxAwaiter {
    UartRxAwaiter(uint8_t *buf, size_t max_len, uint32_t char_timeout_us)
        : buf_(buf), max_len_(max_len), timeout_us_(char_timeout_us), bytes_read_(0) {}

    bool await_ready() const noexcept {
        return hal::Uart2::has_data();
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    size_t await_resume() noexcept {
        return hal::Uart2::read_frame_timeout(buf_, max_len_, timeout_us_);
    }

private:
    uint8_t *buf_;
    size_t   max_len_;
    uint32_t timeout_us_;
    size_t   bytes_read_;
};

inline UartRxAwaiter read_uart2_frame(uint8_t *buf, size_t max_len, uint32_t timeout_us = 500) {
    return UartRxAwaiter(buf, max_len, timeout_us);
}

// Non-blocking IPC packet awaiter
struct IpcReceiveAwaiter {
    IpcReceiveAwaiter(ipc::SpscRingBuffer *rx_ring, ipc::IpcPacket *out_pkt)
        : rx_ring_(rx_ring), out_pkt_(out_pkt), success_(false) {}

    bool await_ready() const noexcept {
        return rx_ring_ && !rx_ring_->is_empty();
    }

    void await_suspend(std::coroutine_handle<>) const noexcept {}

    bool await_resume() noexcept {
        if (rx_ring_ && out_pkt_) {
            return rx_ring_->pop(*out_pkt_);
        }
        return false;
    }

private:
    ipc::SpscRingBuffer *rx_ring_;
    ipc::IpcPacket      *out_pkt_;
    bool                 success_;
};

inline IpcReceiveAwaiter receive_ipc_packet(ipc::SpscRingBuffer *rx_ring, ipc::IpcPacket *out_pkt) {
    return IpcReceiveAwaiter(rx_ring, out_pkt);
}

} // namespace fc::coroutines

#endif // IOPROCESSOR_HARDWARE_AWAITERS_HPP
