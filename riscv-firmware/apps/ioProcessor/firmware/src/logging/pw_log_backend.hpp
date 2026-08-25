#ifndef IOPROCESSOR_LOGGING_PW_LOG_BACKEND_HPP
#define IOPROCESSOR_LOGGING_PW_LOG_BACKEND_HPP

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ringbuffer.hpp"
#include "ipc_protocol.hpp"
#include "timer.hpp"
#include <pw_tokenizer/tokenize.h>

namespace fc::logging {

class PigweedLogger {
public:
    static void init(ipc::SpscRingBuffer* tx_ring) {
        tx_ring_ = tx_ring;
        seq_ = 0;
    }

    static void log_token(uint32_t token, const uint8_t* packed_args = nullptr, size_t arg_len = 0) {
        if (!tx_ring_) return;

        ipc::IpcPacket packet;
        packet.header.magic = ipc::IPC_MAGIC;
        packet.header.seq = seq_++;
        packet.header.type = static_cast<uint16_t>(ipc::PacketType::PigweedLog);
        packet.header.timestamp_us = hal::Timer::get_time_us();

        packet.payload.pw_log.token = token;
        packet.payload.pw_log.data_len = static_cast<uint16_t>(arg_len > 106 ? 106 : arg_len);
        if (packed_args && arg_len > 0) {
            memcpy(packet.payload.pw_log.args, packed_args, packet.payload.pw_log.data_len);
        }

        tx_ring_->push(packet, true);
    }

private:
    static inline ipc::SpscRingBuffer* tx_ring_ = nullptr;
    static inline uint16_t seq_ = 0;
};

} // namespace fc::logging

#define PW_LOG_INFO(str) \
    ::fc::logging::PigweedLogger::log_token(PW_TOKENIZE_STRING(str))

#endif // IOPROCESSOR_LOGGING_PW_LOG_BACKEND_HPP
