/*
 * spsc_queue_test.cpp - Host unit tests for common/hal/spsc_queue.hpp (CppUTest)
 */

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "hal/spsc_queue.hpp"
#include "CppUTest/TestHarness.h"

namespace {

hal::SpscQueue<uint32_t, 16> g_test_queue;

struct MoveOnlyItem {
    uint32_t val;
    explicit MoveOnlyItem(uint32_t v = 0) : val(v) {}
    MoveOnlyItem(const MoveOnlyItem&) = delete;
    MoveOnlyItem& operator=(const MoveOnlyItem&) = delete;
    MoveOnlyItem(MoveOnlyItem&& o) noexcept : val(o.val) { o.val = 0; }
    MoveOnlyItem& operator=(MoveOnlyItem&& o) noexcept {
        val = o.val;
        o.val = 0;
        return *this;
    }
};

hal::SpscQueue<MoveOnlyItem, 8> g_move_queue;

} // namespace

TEST_GROUP(HalSpscQueue) {
    void setup() override {
        g_test_queue.reset();
        g_move_queue.reset();
    }
};

TEST(HalSpscQueue, InitiallyEmpty) {
    CHECK_TRUE(g_test_queue.empty());
    CHECK_TRUE(g_test_queue.is_empty());
    CHECK_FALSE(g_test_queue.is_full());
    LONGS_EQUAL(0, g_test_queue.size());
    LONGS_EQUAL(16, g_test_queue.capacity());
}

TEST(HalSpscQueue, PushAndPopFullCapacity) {
    for (uint32_t i = 0; i < 16; ++i) {
        CHECK_TRUE(g_test_queue.push(i + 100));
    }
    CHECK_TRUE(g_test_queue.is_full());
    CHECK_FALSE(g_test_queue.push(999)); // Full: cannot push 17th item

    uint32_t peek_val = 0;
    CHECK_TRUE(g_test_queue.peek(peek_val));
    LONGS_EQUAL(100, peek_val);

    for (uint32_t i = 0; i < 16; ++i) {
        uint32_t val = 0;
        CHECK_TRUE(g_test_queue.pop(val));
        LONGS_EQUAL(i + 100, val);
    }
    CHECK_TRUE(g_test_queue.empty());
    uint32_t dummy = 0;
    CHECK_FALSE(g_test_queue.pop(dummy));
}

TEST(HalSpscQueue, MoveSemantics) {
    CHECK_TRUE(g_move_queue.push(MoveOnlyItem(42)));
    MoveOnlyItem out(0);
    CHECK_TRUE(g_move_queue.pop(out));
    LONGS_EQUAL(42, out.val);
}

TEST(HalSpscQueue, MultiThreadedStress) {
    constexpr uint32_t kTotal = 100000;
    hal::SpscQueue<uint32_t, 128> queue;

    std::thread producer([&] {
        for (uint32_t i = 1; i <= kTotal; ++i) {
            while (!queue.push(i)) {
                // spin
            }
        }
    });

    std::vector<uint32_t> received;
    received.reserve(kTotal);

    std::thread consumer([&] {
        uint32_t val = 0;
        while (received.size() < kTotal) {
            if (queue.pop(val)) {
                received.push_back(val);
            }
        }
    });

    producer.join();
    consumer.join();

    LONGS_EQUAL(kTotal, received.size());
    for (uint32_t i = 0; i < kTotal; ++i) {
        LONGS_EQUAL(i + 1, received[i]);
    }
}
