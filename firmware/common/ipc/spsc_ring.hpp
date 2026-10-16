/*
 * spsc_ring.hpp - Single-producer / single-consumer ring in shared memory
 *
 * Used on both sides of a Linux (Cortex-A55, aarch64) <-> E906 (RV32) link,
 * so the layout is fixed and identical on both:
 *
 *   offset   0  head    written only by the producer
 *   offset  64  tail    written only by the consumer
 *   offset 128  slots[N]
 *
 *  - N is a power of two: a slot is index & (N - 1), no division.
 *  - head and tail are free-running 32-bit counters. They wrap at 2^32,
 *    which N divides, so head - tail is always the fill level and all N
 *    slots are usable (no "one empty slot" rule).
 *  - Indices are 32-bit on purpose: the E906 is RV32 and has no single
 *    64-bit access, so a 64-bit index could be read torn.
 *  - head and tail sit on separate 64-byte lines (the A55 cache line).
 *  - Each side keeps a private copy of the other side's index and reads
 *    the shared one only when the ring looks full (producer) or empty
 *    (consumer). Shared memory here is uncached, so those reads are the
 *    expensive part.
 *  - reserve()/commit() and peek()/release() hand out the slot itself, so
 *    data is written and read in place (no extra copy).
 *  - A fill level above N can only come from a corrupt index; it is
 *    reported through corrupt() and the ring stops.
 *
 * Shared memory is accessed through volatile pointers: on Linux it is
 * mapped as device memory, where only aligned accesses are allowed.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace ipc {

/* Order shared-memory accesses against the other processor. */
inline void barrier() noexcept
{
#if defined(__riscv)
    __asm__ volatile("fence rw, rw" ::: "memory");
#elif defined(__aarch64__)
    __asm__ volatile("dmb sy" ::: "memory");
#else
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
#endif
}

/*
 * Copy to or from shared memory with the widest aligned access the CPU has:
 * 64-bit on aarch64, 32-bit on RV32. `bytes` must be a multiple of 8 and
 * both pointers 8-byte aligned (slot payloads are laid out that way).
 */
inline void copy_shared(volatile void *dst, const volatile void *src, size_t bytes) noexcept
{
#if defined(__aarch64__) || defined(__x86_64__)
    using word = uint64_t;
#else
    using word = uint32_t;
#endif
    volatile word *d = static_cast<volatile word *>(dst);
    const volatile word *s = static_cast<const volatile word *>(src);
    for (size_t i = 0; i < bytes / sizeof(word); ++i) {
        d[i] = s[i];
    }
}

template <typename T, uint32_t N>
struct SpscRing {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "ring size must be a power of two");
    static_assert(std::is_trivially_copyable<T>::value, "slots are shared memory");
    static_assert(sizeof(T) % 8 == 0 && alignof(T) <= 8, "slots must be 8-byte multiples");

    using value_type = T;
    static constexpr uint32_t kSize = N;
    static constexpr uint32_t kMask = N - 1;

    alignas(64) volatile uint32_t head;   // producer only
    alignas(64) volatile uint32_t tail;   // consumer only
    alignas(64) T slots[N];
};

/* Both sides must see the same layout. */
#define IPC_SPSC_LAYOUT_CHECK(Ring)                                            \
    static_assert(offsetof(Ring, head) == 0, "spsc head offset");              \
    static_assert(offsetof(Ring, tail) == 64, "spsc tail offset");             \
    static_assert(offsetof(Ring, slots) == 128, "spsc slots offset")

/* Owner initialises the ring once, before the other side uses it. */
template <typename Ring>
inline void ring_init(volatile Ring *r) noexcept
{
    r->head = 0;
    r->tail = 0;
    barrier();
}

template <typename Ring>
class Producer {
public:
    using T = typename Ring::value_type;

    /* Continues from the ring's current state (e.g. after a restart). */
    explicit Producer(volatile Ring *r) noexcept : r_(r), head_(r->head), tail_(r->tail) {}

    /* The next free slot, or nullptr if the ring is full (or corrupt). */
    volatile T *reserve() noexcept
    {
        if (head_ - tail_ >= Ring::kSize) {
            tail_ = r_->tail;
            barrier();
            if (head_ - tail_ > Ring::kSize) {
                corrupt_ = true;
                return nullptr;
            }
            if (head_ - tail_ == Ring::kSize) {
                return nullptr;
            }
        }
        return &r_->slots[head_ & Ring::kMask];
    }

    /* Publish the slot returned by reserve(). */
    void commit() noexcept
    {
        barrier();              // slot contents before the index that announces them
        r_->head = ++head_;
    }

    uint32_t in_flight() noexcept
    {
        tail_ = r_->tail;
        return head_ - tail_;
    }
    bool corrupt() const noexcept { return corrupt_; }

private:
    volatile Ring *r_;
    uint32_t head_;
    uint32_t tail_;
    bool corrupt_ = false;
};

template <typename Ring>
class Consumer {
public:
    using T = typename Ring::value_type;

    explicit Consumer(volatile Ring *r) noexcept : r_(r), head_(r->head), tail_(r->tail) {}

    /* The oldest unread slot, or nullptr if the ring is empty (or corrupt). */
    const volatile T *peek() noexcept
    {
        if (tail_ == head_) {
            head_ = r_->head;
            barrier();          // index before the slot contents it announces
            if (head_ - tail_ > Ring::kSize) {
                corrupt_ = true;
                head_ = tail_;
                return nullptr;
            }
            if (tail_ == head_) {
                return nullptr;
            }
        }
        return &r_->slots[tail_ & Ring::kMask];
    }

    /* Hand the slot returned by peek() back to the producer. */
    void release() noexcept
    {
        barrier();              // finish reading the slot before giving it back
        r_->tail = ++tail_;
    }

    uint32_t tail() const noexcept { return tail_; }
    bool corrupt() const noexcept { return corrupt_; }

private:
    volatile Ring *r_;
    uint32_t head_;
    uint32_t tail_;
    bool corrupt_ = false;
};

} // namespace ipc
