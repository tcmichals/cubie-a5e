#ifndef IOPROCESSOR_LOGGING_TRACE_MANAGER_HPP
#define IOPROCESSOR_LOGGING_TRACE_MANAGER_HPP

#include <stdint.h>
#include <stddef.h>
#include "memory_map.h"
#include <barectf.h>

namespace fc::logging {

class TraceManager {
public:
    static void init() {
        // Point Barectf directly to dedicated 32 KB SRAM C Trace Buffer
        uint8_t *sram_trace_ptr = reinterpret_cast<uint8_t *>(IPC_SHARED_MEM_BASE + IPC_TRACE_BUFFER_OFFSET);
        barectf_init(&ctx_, sram_trace_ptr, IPC_TRACE_BUFFER_SIZE);
    }

    static inline void trace_task_switch(uint32_t task_id, uint32_t state) {
        barectf_trace_task_switch(&ctx_, task_id, state);
    }

    static inline void trace_spi(uint32_t cs_id, uint32_t len, uint32_t duration_ns) {
        barectf_trace_spi_transfer(&ctx_, cs_id, len, duration_ns);
    }

    static inline void trace_uart(uint32_t len, uint32_t timeout_us) {
        barectf_trace_uart_frame(&ctx_, len, timeout_us);
    }

    static inline void flush() {
        barectf_flush_packet(&ctx_);
    }

private:
    static inline struct barectf_ctx ctx_;
};

} // namespace fc::logging

#endif // IOPROCESSOR_LOGGING_TRACE_MANAGER_HPP
