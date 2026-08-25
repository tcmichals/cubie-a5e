#include "barectf.h"
#include <string.h>

#define CTF_MAGIC 0xC1FC1FC1

static inline uint64_t get_ns(void) {
    uint32_t high0, low, high1;
    do {
        __asm__ volatile("rdcycleh %0" : "=r"(high0));
        __asm__ volatile("rdcycle %0"  : "=r"(low));
        __asm__ volatile("rdcycleh %0" : "=r"(high1));
    } while (high0 != high1);
    uint64_t cycles = ((uint64_t)high0 << 32) | low;
    return (cycles * 5ULL) / 3ULL; // 600 MHz to ns
}

void barectf_init(struct barectf_ctx *ctx, uint8_t *buffer, size_t size) {
    ctx->buf = buffer;
    ctx->buf_size = size;
    ctx->buf_pos = sizeof(struct barectf_packet_header);
    ctx->packet_seq = 0;
    ctx->packet_start_time = get_ns();
}

void barectf_flush_packet(struct barectf_ctx *ctx) {
    if (ctx->buf_pos <= sizeof(struct barectf_packet_header)) {
        return;
    }

    struct barectf_packet_header *hdr = (struct barectf_packet_header *)ctx->buf;
    hdr->magic = CTF_MAGIC;
    hdr->stream_id = 0;
    hdr->packet_size = (uint32_t)(ctx->buf_size * 8);
    hdr->content_size = (uint32_t)(ctx->buf_pos * 8);
    hdr->timestamp_begin = ctx->packet_start_time;
    hdr->timestamp_end = get_ns();
    hdr->packet_seq = ctx->packet_seq++;

    // Reset buffer position
    ctx->buf_pos = sizeof(struct barectf_packet_header);
    ctx->packet_start_time = get_ns();
}

static inline bool ensure_space(struct barectf_ctx *ctx, size_t len) {
    if (ctx->buf_pos + len > ctx->buf_size) {
        barectf_flush_packet(ctx);
    }
    return (ctx->buf_pos + len <= ctx->buf_size);
}

void barectf_trace_task_switch(struct barectf_ctx *ctx, uint32_t task_id, uint32_t state) {
    if (!ensure_space(ctx, 16)) return;

    uint64_t ts = get_ns();
    uint8_t ev_id = BARECTF_EV_TASK_SWITCH;

    memcpy(&ctx->buf[ctx->buf_pos], &ev_id, 1);
    memcpy(&ctx->buf[ctx->buf_pos + 1], &ts, 8);
    memcpy(&ctx->buf[ctx->buf_pos + 9], &task_id, 2);
    memcpy(&ctx->buf[ctx->buf_pos + 11], &state, 1);
    ctx->buf_pos += 12;
}

void barectf_trace_spi_transfer(struct barectf_ctx *ctx, uint32_t cs_id, uint32_t len, uint32_t duration_ns) {
    if (!ensure_space(ctx, 20)) return;

    uint64_t ts = get_ns();
    uint8_t ev_id = BARECTF_EV_SPI_TRANSFER;

    memcpy(&ctx->buf[ctx->buf_pos], &ev_id, 1);
    memcpy(&ctx->buf[ctx->buf_pos + 1], &ts, 8);
    memcpy(&ctx->buf[ctx->buf_pos + 9], &cs_id, 1);
    memcpy(&ctx->buf[ctx->buf_pos + 10], &len, 4);
    memcpy(&ctx->buf[ctx->buf_pos + 14], &duration_ns, 4);
    ctx->buf_pos += 18;
}

void barectf_trace_uart_frame(struct barectf_ctx *ctx, uint32_t len, uint32_t timeout_us) {
    if (!ensure_space(ctx, 20)) return;

    uint64_t ts = get_ns();
    uint8_t ev_id = BARECTF_EV_UART_FRAME;

    memcpy(&ctx->buf[ctx->buf_pos], &ev_id, 1);
    memcpy(&ctx->buf[ctx->buf_pos + 1], &ts, 8);
    memcpy(&ctx->buf[ctx->buf_pos + 9], &len, 4);
    memcpy(&ctx->buf[ctx->buf_pos + 13], &timeout_us, 4);
    ctx->buf_pos += 17;
}
