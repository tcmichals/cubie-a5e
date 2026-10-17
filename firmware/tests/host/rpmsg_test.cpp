/*
 * rpmsg_test.cpp - Host unit tests for common/hal/rpmsg.cpp (CppUTest)
 *
 * The E906 code runs unchanged against a fake Linux host: a resource table,
 * two vrings and message buffers in one block of memory below 4 GB (the
 * firmware stores device addresses as 32-bit values), and a fake MsgBox.
 */

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <vector>

#include "fake_msgbox.hpp"
#include "hal/rpmsg.hpp"

#include "CppUTest/TestHarness.h"

namespace {

/* Linux's view of a vring (virtio spec layout, naturally aligned). */
struct Desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};
struct Avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
};
struct UsedElem {
    uint32_t id;
    uint32_t len;
};
struct Used {
    uint16_t flags;
    uint16_t idx;
    UsedElem ring[VRING_NUM_DESCS];
};

constexpr size_t kBlock = 1 << 20;
constexpr size_t kRsc = 0x0000;
constexpr size_t kVring0 = 0x1000;   // Linux RX: buffers Linux lends the E906 for replies
constexpr size_t kVring1 = 0x4000;   // Linux TX: messages Linux sends to the E906
constexpr size_t kBufs = 0x10000;    // 32 buffers of RPMSG_BUFFER_SIZE
constexpr uint32_t kN = VRING_NUM_DESCS;

struct Vring {
    volatile Desc *desc;
    volatile Avail *avail;
    volatile Used *used;
};

/* Plays Linux: owns the memory, posts messages and lends reply buffers. */
class FakeHost {
public:
    explicit FakeHost(uint32_t num = kN) : num_(num) {
        mem_ = static_cast<uint8_t *>(mmap(nullptr, kBlock, PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0));
        CHECK(mem_ != MAP_FAILED);
        memset(mem_, 0, kBlock);

        rsc_ = reinterpret_cast<rpmsg_resource_table *>(mem_ + kRsc);
        rsc_->vdev.vring[0].da = da(kVring0);
        rsc_->vdev.vring[0].num = num;
        rsc_->vdev.vring[0].align = VRING_ALIGN;
        rsc_->vdev.vring[1].da = da(kVring1);
        rsc_->vdev.vring[1].num = num;
        rsc_->vdev.vring[1].align = VRING_ALIGN;
        rsc_->vdev.status = VIRTIO_CONFIG_S_DRIVER_OK;

        rx_ = layout(kVring0);
        tx_ = layout(kVring1);
    }
    ~FakeHost() { munmap(mem_, kBlock); }

    const rpmsg_resource_table *rsc() const { return rsc_; }
    Vring &rx() { return rx_; }   // vring0
    Vring &tx() { return tx_; }   // vring1

    /* Post one message from Linux (src 1024 -> dst 1024) in TX descriptor `d`. */
    void post(uint16_t d, const void *payload, uint16_t len, uint16_t hdr_len_override = 0) {
        uint8_t *buf = mem_ + kBufs + d * RPMSG_BUFFER_SIZE;
        auto *hdr = reinterpret_cast<rpmsg_hdr *>(buf);
        hdr->src = 1024;
        hdr->dst = 1024;
        hdr->len = hdr_len_override ? hdr_len_override : len;
        memcpy(hdr->data, payload, len);
        tx_.desc[d].addr = da(kBufs + d * RPMSG_BUFFER_SIZE);
        tx_.desc[d].len = RPMSG_BUFFER_SIZE;
        tx_.avail->ring[tx_.avail->idx % num_] = d;
        tx_.avail->idx = tx_.avail->idx + 1;
    }

    /* Lend reply buffer `d` (buffers 16..31) to the E906. */
    void lend(uint16_t d) {
        rx_.desc[d].addr = da(kBufs + (kN + d) * RPMSG_BUFFER_SIZE);
        rx_.desc[d].len = RPMSG_BUFFER_SIZE;
        rx_.avail->ring[rx_.avail->idx % num_] = d;
        rx_.avail->idx = rx_.avail->idx + 1;
    }

    const rpmsg_hdr *reply_buf(uint32_t d) const {
        return reinterpret_cast<const rpmsg_hdr *>(mem_ + kBufs + (kN + d) * RPMSG_BUFFER_SIZE);
    }

private:
    uint32_t da(size_t off) const { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mem_ + off)); }
    Vring layout(size_t off) {
        uint8_t *base = mem_ + off;
        size_t avail_off = sizeof(Desc) * num_;
        size_t used_off = (avail_off + sizeof(uint16_t) * (3 + num_) + VRING_ALIGN - 1) & ~(size_t)(VRING_ALIGN - 1);
        return {reinterpret_cast<volatile Desc *>(base), reinterpret_cast<volatile Avail *>(base + avail_off),
                reinterpret_cast<volatile Used *>(base + used_off)};
    }

    uint32_t num_;
    uint8_t *mem_;
    rpmsg_resource_table *rsc_;
    Vring rx_, tx_;
};

/* Endpoint callback: records what arrived, optionally echoes it. */
std::vector<std::vector<uint8_t>> g_received;
bool g_echo = false;
bool g_reply_ok = true;

void on_message(const hal::RpmsgMessage &msg, void *) {
    g_received.emplace_back(msg.data, msg.data + msg.len);
    if (g_echo) {
        g_reply_ok = hal::Rpmsg::reply(msg, msg.data, msg.len);
    }
}

} // namespace

TEST_GROUP(Rpmsg) {
    FakeHost *host;

    void setup() override {
        fake_msgbox::reset();
        g_received.clear();
        g_echo = false;
        g_reply_ok = true;
        host = new FakeHost();
        hal::Rpmsg::init(host->rsc());
        hal::Rpmsg::register_endpoint(1024, on_message);
        CHECK(hal::Rpmsg::is_driver_ready());
    }
    void teardown() override {
        // Free everything the test allocated (CppUTest checks for leaks per test)
        delete host;
        std::vector<std::vector<uint8_t>>().swap(g_received);
        fake_msgbox::on_send = nullptr;
    }
};

TEST(Rpmsg, DeliversEachPostedMessageOnceAndReturnsItsBuffer) {
    const char a[] = "first", b[] = "second";
    host->post(3, a, sizeof(a));
    host->post(7, b, sizeof(b));

    CHECK_TRUE(hal::Rpmsg::poll());
    LONGS_EQUAL(2, g_received.size());
    MEMCMP_EQUAL(a, g_received[0].data(), sizeof(a));
    MEMCMP_EQUAL(b, g_received[1].data(), sizeof(b));

    // Both TX buffers handed back to Linux, in order
    LONGS_EQUAL(2, host->tx().used->idx);
    LONGS_EQUAL(3, host->tx().used->ring[0].id);
    LONGS_EQUAL(7, host->tx().used->ring[1].id);

    // Nothing new: nothing delivered again
    CHECK_FALSE(hal::Rpmsg::poll());
    LONGS_EQUAL(2, g_received.size());
    LONGS_EQUAL(0, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, IndexFurtherAheadThanTheRingIsRejectedNotReplayed) {
    // What the torn byte-wise read used to produce: an index 255 ahead
    host->tx().avail->idx = 255;

    CHECK_FALSE(hal::Rpmsg::poll());
    LONGS_EQUAL(0, g_received.size());
    LONGS_EQUAL(0, host->tx().used->idx);
    LONGS_EQUAL(1, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, ExactlyAFullRingIsAccepted) {
    uint8_t byte = 0;
    for (uint16_t d = 0; d < kN; ++d) {
        host->post(d, &byte, 1);
    }
    CHECK_TRUE(hal::Rpmsg::poll());
    LONGS_EQUAL(kN, g_received.size());
    LONGS_EQUAL(0, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, IndexWrapsAt65536) {
    // Start both sides just below the 16-bit wrap
    uint8_t byte = 0;
    for (uint32_t i = 0; i < 65530; ++i) {
        host->post(i % kN, &byte, 1);
        CHECK_TRUE(hal::Rpmsg::poll());
    }
    g_received.clear();
    for (uint16_t i = 0; i < 12; ++i) {
        host->post(i, &byte, 1);
    }
    CHECK_TRUE(hal::Rpmsg::poll());
    LONGS_EQUAL(12, g_received.size());
    LONGS_EQUAL(0, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, LengthLargerThanItsBufferIsMalformed) {
    uint8_t byte = 0;
    host->post(0, &byte, 1, RPMSG_BUFFER_SIZE);  // header claims more than the buffer holds

    CHECK_TRUE(hal::Rpmsg::poll());
    LONGS_EQUAL(0, g_received.size());            // not delivered
    LONGS_EQUAL(1, host->tx().used->idx);         // but the buffer is given back
    LONGS_EQUAL(1, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, DescriptorIndexOutsideTheRingIsMalformed) {
    host->tx().avail->ring[0] = kN;               // no such descriptor
    host->tx().avail->idx = 1;

    CHECK_FALSE(hal::Rpmsg::poll());
    LONGS_EQUAL(0, g_received.size());
    LONGS_EQUAL(1, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, ReplyUsesALentBufferAndRingsTheHost) {
    host->lend(5);
    g_echo = true;
    const char ping[] = "PING-1";
    host->post(0, ping, sizeof(ping));

    CHECK_TRUE(hal::Rpmsg::poll());
    CHECK_TRUE(g_reply_ok);
    LONGS_EQUAL(1, host->rx().used->idx);
    LONGS_EQUAL(5, host->rx().used->ring[0].id);
    LONGS_EQUAL(sizeof(rpmsg_hdr) + sizeof(ping), host->rx().used->ring[0].len);
    MEMCMP_EQUAL(ping, host->reply_buf(5)->data, sizeof(ping));
    LONGS_EQUAL(1024, host->reply_buf(5)->dst);
    CHECK(fake_msgbox::sends >= 1);
}

TEST(Rpmsg, ReplyWaitsForAHostBufferInsteadOfDropping) {
    // No buffer lent. The fake host lends one only after the third doorbell.
    int doorbells = 0;
    fake_msgbox::on_send = [&] {
        if (++doorbells == 3) {
            host->lend(0);
        }
    };
    g_echo = true;
    const char ping[] = "wait for me";
    host->post(0, ping, sizeof(ping));

    CHECK_TRUE(hal::Rpmsg::poll());
    CHECK_TRUE(g_reply_ok);                       // delivered, not dropped
    LONGS_EQUAL(1, host->rx().used->idx);
    MEMCMP_EQUAL(ping, host->reply_buf(0)->data, sizeof(ping));
    LONGS_EQUAL(3, hal::Rpmsg::get_tx_full_waits());
}

TEST(Rpmsg, ReplyDrainsHostDoorbellsWhileWaitingForABuffer) {
    // While the E906 waits for a host buffer, Linux keeps returning buffers
    // and kicking. Those kicks must be drained during the wait, or the
    // E906's FIFO fills and Linux's mailbox queue overflows.
    int doorbells = 0;
    fake_msgbox::on_send = [&] {
        if (++doorbells <= 3) {
            fake_msgbox::rx_pending += 4;         // the host kicks back while we wait
        }
        if (doorbells == 3) {
            host->lend(0);
        }
    };
    g_echo = true;
    const char ping[] = "busy host";
    host->post(0, ping, sizeof(ping));

    CHECK_TRUE(hal::Rpmsg::poll());
    CHECK_TRUE(g_reply_ok);
    LONGS_EQUAL(0, fake_msgbox::rx_pending);      // nothing left queued
}

TEST(Rpmsg, DoorbellWaitsForFifoSpaceInsteadOfDropping) {
    fake_msgbox::full_sends_left = 5;             // FIFO full for five attempts
    uint8_t byte = 0;
    host->post(0, &byte, 1);

    CHECK_TRUE(hal::Rpmsg::poll());
    LONGS_EQUAL(1, fake_msgbox::sends);           // the doorbell still went out
    LONGS_EQUAL(5, hal::Rpmsg::get_mbox_full_waits());
}

TEST(Rpmsg, AllQueuedHostDoorbellsAreDrained) {
    uint32_t kicks = hal::Rpmsg::get_host_kicks();
    fake_msgbox::rx_pending = 6;
    hal::Rpmsg::poll();
    LONGS_EQUAL(0, fake_msgbox::rx_pending);
    LONGS_EQUAL(kicks + 6, hal::Rpmsg::get_host_kicks());
}

TEST(Rpmsg, ReplyLongerThanABufferIsRefused) {
    host->lend(0);
    static uint8_t big[RPMSG_BUFFER_SIZE] = {};
    hal::RpmsgMessage msg = {1024, 1024, 0, nullptr, 0};
    CHECK_FALSE(hal::Rpmsg::reply(msg, big, RPMSG_BUFFER_SIZE));
    LONGS_EQUAL(0, host->rx().used->idx);
}

TEST(Rpmsg, RingSizeThatIsNotAPowerOfTwoIsRejected) {
    FakeHost odd(12);
    hal::Rpmsg::init(odd.rsc());
    CHECK_FALSE(hal::Rpmsg::is_driver_ready());
    LONGS_EQUAL(1, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, RingLargerThanTheDescriptorArraysIsRejected) {
    FakeHost big(2 * kN);
    hal::Rpmsg::init(big.rsc());
    CHECK_FALSE(hal::Rpmsg::is_driver_ready());
    LONGS_EQUAL(1, hal::Rpmsg::get_ring_errors());
}

TEST(Rpmsg, SmallerPowerOfTwoRingWorksAcrossWrap) {
    FakeHost small(8);
    hal::Rpmsg::init(small.rsc());
    hal::Rpmsg::register_endpoint(1024, on_message);
    CHECK_TRUE(hal::Rpmsg::is_driver_ready());
    uint8_t byte = 0;
    for (uint16_t i = 0; i < 20; ++i) {   // 20 > 2 * 8: wraps the ring twice
        small.post(i % 8, &byte, 1);
        CHECK_TRUE(hal::Rpmsg::poll());
    }
    LONGS_EQUAL(20, g_received.size());
    LONGS_EQUAL(20, small.tx().used->idx);
    LONGS_EQUAL(0, hal::Rpmsg::get_ring_errors());
}

