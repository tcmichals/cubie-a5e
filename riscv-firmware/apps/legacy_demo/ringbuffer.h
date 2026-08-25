/* ringbuffer.h - Allocation-free ring buffer for SPI telemetry */
#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PACKET_SIZE 64
#define BUFFER_DEPTH 511

typedef struct {
    uint8_t data[PACKET_SIZE];
} packet_t;

typedef struct {
    packet_t buffer[BUFFER_DEPTH];
    volatile uint32_t head;
    volatile uint32_t tail;
} ringbuffer_t;

void ringbuffer_init(ringbuffer_t *rb);
bool ringbuffer_push(ringbuffer_t *rb, const uint8_t *data, uint32_t len);
bool ringbuffer_pop(ringbuffer_t *rb, uint8_t *out_data);

#ifdef __cplusplus
}
#endif

#endif // RINGBUFFER_H
