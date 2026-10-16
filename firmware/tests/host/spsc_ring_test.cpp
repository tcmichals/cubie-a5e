/*
 * spsc_ring_test.cpp - Host unit tests for common/ipc/spsc_ring.hpp (CppUTest)
 */

#include <cstdint>
#include <cstring>
#include <thread>

#include "ipc/spsc_ring.hpp"

#include "CppUTest/TestHarness.h"

namespace {

struct Item {
    uint32_t seq;
    uint32_t check;
    uint64_t payload[3];
};

using Ring = ipc::SpscRing<Item, 8>;
IPC_SPSC_LAYOUT_CHECK(Ring);

void put(ipc::Producer<Ring> &p, uint32_t seq)
{
    volatile Item *s = p.reserve();
    CHECK(s != nullptr);
    s->seq = seq;
    s->check = ~seq;
    p.commit();
}

uint32_t get(ipc::Consumer<Ring> &c)
{
    const volatile Item *s = c.peek();
    CHECK(s != nullptr);
    uint32_t seq = s->seq;
    LONGS_EQUAL(~seq, s->check);
    c.release();
    return seq;
}

} // namespace

/* Static storage: the ring is over-aligned (64 bytes) and CppUTest's own
 * operator new, which allocates the test-group objects, does not support
 * over-aligned types. */
Ring g_ring;

TEST_GROUP(SpscRing) {
    Ring &ring = g_ring;
    void setup() override
    {
        memset(&ring, 0xa5, sizeof(ring));
        ipc::ring_init(&ring);
    }
};

TEST(SpscRing, UsesEveryOneOfItsSlots)
{
    ipc::Producer<Ring> p(&ring);
    ipc::Consumer<Ring> c(&ring);
    for (uint32_t i = 0; i < Ring::kSize; ++i) {
        put(p, i);
    }
    POINTERS_EQUAL(nullptr, p.reserve());   // full after N, not N - 1
    LONGS_EQUAL(Ring::kSize, p.in_flight());
    for (uint32_t i = 0; i < Ring::kSize; ++i) {
        LONGS_EQUAL(i, get(c));
    }
    POINTERS_EQUAL(nullptr, c.peek());
    CHECK_FALSE(p.corrupt());
    CHECK_FALSE(c.corrupt());
}

TEST(SpscRing, ProducerSeesSpaceOnceTheConsumerReleases)
{
    ipc::Producer<Ring> p(&ring);
    ipc::Consumer<Ring> c(&ring);
    for (uint32_t i = 0; i < Ring::kSize; ++i) {
        put(p, i);
    }
    POINTERS_EQUAL(nullptr, p.reserve());
    LONGS_EQUAL(0, get(c));
    put(p, Ring::kSize);                     // the freed slot is reused
    POINTERS_EQUAL(nullptr, p.reserve());
}

TEST(SpscRing, IndicesWrapAt2To32)
{
    ring.head = 0xfffffff0u;
    ring.tail = 0xfffffff0u;
    ipc::Producer<Ring> p(&ring);
    ipc::Consumer<Ring> c(&ring);
    for (uint32_t i = 0; i < 100; ++i) {    // crosses 0xffffffff -> 0
        put(p, i);
        LONGS_EQUAL(i, get(c));
    }
    LONGS_EQUAL(0xfffffff0u + 100u, ring.head);
    LONGS_EQUAL(ring.head, ring.tail);
}

TEST(SpscRing, IndexFurtherAheadThanTheRingIsCorrupt)
{
    ipc::Consumer<Ring> c(&ring);
    ring.head = Ring::kSize + 1;             // more than the ring can hold
    POINTERS_EQUAL(nullptr, c.peek());
    CHECK_TRUE(c.corrupt());

    ring.head = 0;
    ring.tail = Ring::kSize + 1;             // consumer ahead of producer
    ipc::Producer<Ring> p(&ring);
    ring.tail = 0;
    ring.head = 0;
    ipc::Producer<Ring> p2(&ring);
    ring.tail = 0u - (Ring::kSize + 1);      // tail moved backwards past the window
    for (uint32_t i = 0; i < Ring::kSize; ++i) {
        put(p2, i);
    }
    POINTERS_EQUAL(nullptr, p2.reserve());
    CHECK_TRUE(p2.corrupt());
    (void)p;
}

TEST(SpscRing, TwoThreadsMoveAMillionItemsInOrder)
{
    constexpr uint32_t kCount = 1000000;
    std::thread producer([&] {
        ipc::Producer<Ring> p(&ring);
        for (uint32_t i = 0; i < kCount; ++i) {
            volatile Item *s;
            while ((s = p.reserve()) == nullptr) {
            }
            s->seq = i;
            s->check = ~i;
            p.commit();
        }
    });
    ipc::Consumer<Ring> c(&ring);
    uint32_t bad = 0;
    for (uint32_t i = 0; i < kCount; ++i) {
        const volatile Item *s;
        while ((s = c.peek()) == nullptr) {
        }
        if (s->seq != i || s->check != ~i) {
            ++bad;
        }
        c.release();
    }
    producer.join();
    LONGS_EQUAL(0, bad);
    CHECK_FALSE(c.corrupt());
}

TEST(SpscRing, CopySharedMovesWholeWords)
{
    alignas(8) uint8_t src[64], dst[64];
    for (unsigned i = 0; i < sizeof(src); ++i) {
        src[i] = static_cast<uint8_t>(i * 7 + 1);
    }
    memset(dst, 0, sizeof(dst));
    ipc::copy_shared(dst, src, 56);
    MEMCMP_EQUAL(src, dst, 56);
    BYTES_EQUAL(0, dst[56]);                 // stops at the requested size
}
