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

inline hal::Timer::AsyncSleepAwaiter sleep_ms(uint32_t ms) {
    return hal::Timer::async_sleep_ms(ms);
}

// Non-blocking UART frame awaiter (Interrupt & RTO Driven)
inline hal::Uart2::AsyncRxPacketAwaiter read_uart2_frame(uint8_t *buf, size_t max_len) {
    return hal::Uart2::async_read_packet(buf, max_len);
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
