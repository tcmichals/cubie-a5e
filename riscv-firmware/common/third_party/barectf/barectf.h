#ifndef BARECTF_H
#define BARECTF_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CTF Packet Header */
struct barectf_packet_header {
    uint32_t magic;         /* 0xC1FC1FC1 (CTF Magic) */
    uint32_t stream_id;     /* Stream 0 */
    uint32_t packet_size;   /* Bits */
    uint32_t content_size;  /* Bits */
    uint64_t timestamp_begin;
    uint64_t timestamp_end;
    uint32_t packet_seq;
} __attribute__((packed));

/* CTF Event IDs */
enum barectf_event_id {
    BARECTF_EV_TASK_SWITCH  = 0,
    BARECTF_EV_SPI_TRANSFER = 1,
    BARECTF_EV_UART_FRAME   = 2,
    BARECTF_EV_IMU_SAMPLE   = 3,
};

struct barectf_ctx {
    uint8_t *buf;
    size_t   buf_size;
    size_t   buf_pos;
    uint32_t packet_seq;
    uint64_t packet_start_time;
};

void barectf_init(struct barectf_ctx *ctx, uint8_t *buffer, size_t size);
void barectf_trace_task_switch(struct barectf_ctx *ctx, uint32_t task_id, uint32_t state);
void barectf_trace_spi_transfer(struct barectf_ctx *ctx, uint32_t cs_id, uint32_t len, uint32_t duration_ns);
void barectf_trace_uart_frame(struct barectf_ctx *ctx, uint32_t len, uint32_t timeout_us);
void barectf_flush_packet(struct barectf_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BARECTF_H */
