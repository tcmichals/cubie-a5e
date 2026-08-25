/* ringbuffer.c - Core ring buffer implementation */
#include "ringbuffer.h"

/* 
 * SPSC (Single-Producer / Single-Consumer) Architecture Analysis:
 * 
 * This ring buffer is strictly lock-free and multiprocessing-safe between the ARM host 
 * and the RISC-V co-processor because it adheres to the SPSC pattern:
 * 
 * 1. Single Producer (RISC-V): Only the RISC-V core ever modifies the `head` pointer 
 *    and writes to the array elements.
 * 2. Single Consumer (ARM Linux): Only the ARM core ever modifies the `tail` pointer 
 *    and reads the array elements.
 * 
 * Because the producer never writes to `tail`, and the consumer never writes to `head`, 
 * there are no Write-Write (W-W) race conditions. There is no need for slow mutexes, 
 * spinlocks, or atomic CAS (Compare-And-Swap) operations.
 * 
 * CRITICAL SAFETY REQUIREMENT: Memory Barriers
 * While the logic is inherently safe, the C Compiler (GCC) or out-of-order CPUs 
 * could reorder the pointer update to happen BEFORE the data payload is fully written. 
 * If that happens, the ARM core would see the new `head` and read incomplete data.
 * Therefore, we MUST use full memory barriers (__sync_synchronize) before updating pointers.
 */

void ringbuffer_init(ringbuffer_t *rb) {
    rb->head = 0;
    rb->tail = 0;
}

bool ringbuffer_push(ringbuffer_t *rb, const uint8_t *data, uint32_t len) {
    uint32_t next_head = (rb->head + 1) % BUFFER_DEPTH;
    
    if (next_head == rb->tail) {
        /* Buffer is full, drop data to prevent latency spikes */
        return false;
    }
    
    uint32_t copy_len = (len < PACKET_SIZE) ? len : PACKET_SIZE;
    
    /* 1. Write the payload data */
    for (uint32_t i = 0; i < copy_len; i++) {
        rb->buffer[rb->head].data[i] = data[i];
    }
    for (uint32_t i = copy_len; i < PACKET_SIZE; i++) {
        rb->buffer[rb->head].data[i] = 0;
    }
    
    /* 2. MEMORY BARRIER: Ensure all payload bytes are flushed to SRAM 
          before the head pointer is updated and made visible to ARM. */
    __sync_synchronize();
    
    /* 3. Commit the message by updating the head pointer */
    rb->head = next_head;
    return true;
}

bool ringbuffer_pop(ringbuffer_t *rb, uint8_t *out_data) {
    if (rb->head == rb->tail) {
        /* Buffer is empty */
        return false;
    }
    
    /* 1. Read the payload data */
    for (uint32_t i = 0; i < PACKET_SIZE; i++) {
        out_data[i] = rb->buffer[rb->tail].data[i];
    }
    
    /* 2. MEMORY BARRIER: Ensure all payload bytes are fully read from SRAM 
          before we increment the tail pointer and free the slot. */
    __sync_synchronize();
    
    /* 3. Free the message slot by updating the tail pointer */
    rb->tail = (rb->tail + 1) % BUFFER_DEPTH;
    return true;
}
