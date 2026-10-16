#include "rpmsg.hpp"
#include "msgbox.hpp"
#include <string.h>
#include <stddef.h>

namespace hal {

/*
 * VirtIO Ring Internal Descriptors
 *
 * Not packed: the virtio layout is naturally aligned, and packing made the
 * compiler split every 16-bit ring index into two byte accesses. Linux updates
 * an index with one 16-bit store, so a byte-wise read could combine old and new
 * halves (e.g. 0x01FF -> 0x0200 read as 0x02FF) and the RX loop then replayed
 * ~65k stale descriptors. Aligned fields give single-copy-atomic accesses.
 */
struct VirtioDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

struct VirtioAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
};

struct VirtioUsedElem {
    uint32_t id;
    uint32_t len;
};

struct VirtioUsed {
    uint16_t flags;
    uint16_t idx;
    struct VirtioUsedElem ring[VRING_NUM_DESCS];
};

static_assert(sizeof(VirtioDesc) == 16, "virtio desc layout");
static_assert(offsetof(VirtioAvail, idx) == 2 && offsetof(VirtioAvail, ring) == 4, "virtio avail layout");
static_assert(sizeof(VirtioUsedElem) == 8, "virtio used elem layout");
static_assert(offsetof(VirtioUsed, idx) == 2 && offsetof(VirtioUsed, ring) == 4, "virtio used layout");
// Natural alignment is what keeps the indices single accesses: packing would
// keep every offset above but drop these to 1 and bring back byte-wise access.
static_assert(alignof(VirtioAvail) == 2 && alignof(VirtioUsed) == 4 && alignof(VirtioDesc) == 8,
              "vring structs must not be packed");

namespace {
    const struct rpmsg_resource_table *s_rsc = nullptr;
    bool s_vdev_ready = false;
    bool s_ns_announced = false;

    std::atomic<uint32_t> s_rx_count{0};
    std::atomic<uint32_t> s_tx_count{0};
    std::atomic<uint32_t> s_ring_errors{0};    // malformed ring state or message from the host
    std::atomic<uint32_t> s_tx_full_waits{0};  // reply had to wait for a free host RX buffer
    std::atomic<uint32_t> s_mbox_full_waits{0}; // doorbell had to wait for mailbox FIFO space

    Rpmsg::EndpointEntry s_endpoints[Rpmsg::MAX_ENDPOINTS];
    size_t s_num_endpoints = 0;

    struct VirtQueueLayout {
        uint32_t da;
        uint32_t num;
        uint32_t mask;      // num - 1: num is a power of two (virtio spec), so slot = idx & mask
        uint32_t align;
        volatile struct VirtioDesc  *desc;
        volatile struct VirtioAvail *avail;
        volatile struct VirtioUsed  *used;
        uint16_t last_avail_idx;
    };

    /*
     * Standard Linux Remoteproc VirtIO RPMsg ring assignment:
     *   vring[0]: Linux Host RX (Remote TX) - Linux pre-fills with empty buffers
     *   vring[1]: Linux Host TX (Remote RX) - Linux puts outgoing messages here
     */
    VirtQueueLayout s_tx_vq; // vring[0]: Remote -> Host (TX)
    VirtQueueLayout s_rx_vq; // vring[1]: Host -> Remote (RX)

    inline uintptr_t translate_da(uint32_t da) {
        // SRAM Space 1 on Allwinner A527 (Host 0x072c0000 -> E906 0x40000000)
        if (da >= 0x072c0000 && da < 0x07300000) {
            return static_cast<uintptr_t>(da - 0x072c0000 + 0x40000000);
        }
        // SRAM Space 0 on Allwinner A527 (Host 0x07280000 -> E906 0x3ffc0000)
        if (da >= 0x07280000 && da < 0x072c0000) {
            return static_cast<uintptr_t>(da - 0x07280000 + 0x3ffc0000);
        }
        // SRAM Space 0 on Allwinner T527 (Host 0x07200000 -> E906 0x3ffc0000)
        if (da >= 0x07200000 && da < 0x07240000) {
            return static_cast<uintptr_t>(da - 0x07200000 + 0x3ffc0000);
        }
        // DDR DRAM and direct E906 local addresses
        return static_cast<uintptr_t>(da);
    }

    /*
     * Map one vring. Its size must be a power of two (virtio split ring) and
     * fit the descriptor arrays above; anything else is rejected, so ring
     * slots can be computed with a mask instead of a division.
     */
    bool setup_vq(VirtQueueLayout &vq, uint32_t da, uint32_t num, uint32_t align) {
        vq.desc = nullptr;
        vq.avail = nullptr;
        vq.used = nullptr;
        vq.last_avail_idx = 0;

        if (da == 0 || da == 0xFFFFFFFFUL) {
            return false;
        }
        if (num == 0 || num > VRING_NUM_DESCS || (num & (num - 1)) != 0 ||
            align == 0 || (align & (align - 1)) != 0) {
            return false;
        }
        vq.da = da;
        vq.num = num;
        vq.mask = num - 1;
        vq.align = align;

        uint8_t *base = reinterpret_cast<uint8_t *>(translate_da(da));
        vq.desc = reinterpret_cast<volatile struct VirtioDesc *>(base);

        // avail: flags, idx, ring[num], used_event
        size_t avail_offset = sizeof(struct VirtioDesc) * num;
        vq.avail = reinterpret_cast<volatile struct VirtioAvail *>(base + avail_offset);

        size_t used_offset = (avail_offset + sizeof(uint16_t) * (3 + num) + align - 1) & ~(size_t)(align - 1);
        vq.used = reinterpret_cast<volatile struct VirtioUsed *>(base + used_offset);
        return true;
    }

    /*
     * Number of buffers the host made available that we have not consumed.
     * More than the ring size can only come from a corrupt index: report it
     * and process nothing rather than walk stale descriptors.
     */
    bool avail_pending(const VirtQueueLayout &vq, uint16_t &pending) {
        uint16_t avail_idx = vq.avail->idx;
        std::atomic_thread_fence(std::memory_order_acquire);
        pending = static_cast<uint16_t>(avail_idx - vq.last_avail_idx);
        if (pending > vq.num) {
            s_ring_errors.fetch_add(1, std::memory_order_relaxed);
            pending = 0;
            return false;
        }
        return true;
    }

    /* Ring the host's doorbell; if its FIFO is full, wait for space instead of dropping. */
    void notify_host() {
        while (!MsgBox::send(MsgBox::Channel::Channel1, 0)) {
            s_mbox_full_waits.fetch_add(1, std::memory_order_relaxed);
        }
    }

    /* Drain every doorbell the host queued so its mailbox sees them consumed. */
    void drain_host_doorbells() {
        while (MsgBox::is_rx_pending(MsgBox::Channel::Channel0)) {
            (void)MsgBox::receive(MsgBox::Channel::Channel0);
        }
        while (MsgBox::is_rx_pending(MsgBox::Channel::Channel1)) {
            (void)MsgBox::receive(MsgBox::Channel::Channel1);
        }
    }
}

void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_rsc = rsc;
    s_vdev_ready = false;
    s_ns_announced = false;
    s_rx_count.store(0, std::memory_order_relaxed);
    s_tx_count.store(0, std::memory_order_relaxed);
    s_ring_errors.store(0, std::memory_order_relaxed);
    s_tx_full_waits.store(0, std::memory_order_relaxed);
    s_mbox_full_waits.store(0, std::memory_order_relaxed);
    s_num_endpoints = 0;

    for (size_t i = 0; i < MAX_ENDPOINTS; ++i) {
        s_endpoints[i].addr = 0;
        s_endpoints[i].cb = nullptr;
        s_endpoints[i].user_data = nullptr;
    }
}

bool Rpmsg::is_driver_ready() noexcept {
    if (!s_rsc) return false;

    if (!s_vdev_ready) {
        volatile const struct rpmsg_resource_table *rsc = s_rsc;
        uint8_t status = rsc->vdev.status;

        if ((status & VIRTIO_CONFIG_S_DRIVER_OK) &&
            rsc->vdev.vring[0].da != 0 && rsc->vdev.vring[1].da != 0) {
            bool tx_ok = setup_vq(s_tx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            bool rx_ok = setup_vq(s_rx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            if (tx_ok && rx_ok) {
                s_vdev_ready = true;
            } else {
                s_ring_errors.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return s_vdev_ready;
}

bool Rpmsg::register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data) noexcept {
    if (s_num_endpoints >= MAX_ENDPOINTS) return false;

    for (size_t i = 0; i < s_num_endpoints; ++i) {
        if (s_endpoints[i].addr == addr) {
            s_endpoints[i].cb = cb;
            s_endpoints[i].user_data = user_data;
            return true;
        }
    }

    s_endpoints[s_num_endpoints].addr = addr;
    s_endpoints[s_num_endpoints].cb = cb;
    s_endpoints[s_num_endpoints].user_data = user_data;
    s_num_endpoints++;
    return true;
}

bool Rpmsg::announce_service(const char *name, uint32_t addr) noexcept {
    if (!is_driver_ready() || s_tx_vq.avail == nullptr) return false;
    uint16_t free_bufs = 0;
    if (!avail_pending(s_tx_vq, free_bufs) || free_bufs == 0) return false;

    struct rpmsg_ns_msg {
        char name[32];
        uint32_t addr;
        uint32_t flags;
    } __attribute__((packed)) ns_msg;

    memset(&ns_msg, 0, sizeof(ns_msg));
    strncpy(ns_msg.name, name, sizeof(ns_msg.name) - 1);
    ns_msg.addr = addr;
    ns_msg.flags = 0; // RPMSG_NS_CREATE

    uint16_t avail_slot = s_tx_vq.last_avail_idx & s_tx_vq.mask;
    uint16_t desc_idx = s_tx_vq.avail->ring[avail_slot];
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(static_cast<uint32_t>(desc->addr)));
    hdr->src = addr;
    hdr->dst = 53; // RPMSG_NS_ADDR
    hdr->len = sizeof(ns_msg);
    hdr->flags = 0;
    hdr->reserved = 0;

    memcpy(const_cast<uint8_t *>(hdr->data), &ns_msg, sizeof(ns_msg));

    s_tx_vq.last_avail_idx++;

    uint16_t used_slot = s_tx_vq.used->idx & s_tx_vq.mask;
    s_tx_vq.used->ring[used_slot].id = desc_idx;
    s_tx_vq.used->ring[used_slot].len = sizeof(struct rpmsg_hdr) + sizeof(ns_msg);
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx++;

    notify_host();
    s_ns_announced = true;
    return true;
}

bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) {
        return false;
    }

    drain_host_doorbells();

    uint16_t pending = 0;
    if (!avail_pending(s_rx_vq, pending) || pending == 0) {
        return false;
    }

    for (; pending > 0; --pending) {
        uint16_t desc_idx = s_rx_vq.avail->ring[s_rx_vq.last_avail_idx & s_rx_vq.mask];
        if (desc_idx >= s_rx_vq.num) {
            // A descriptor index outside the ring: nothing safe to return to the host.
            s_ring_errors.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        volatile struct VirtioDesc *desc = &s_rx_vq.desc[desc_idx];
        uint32_t buf_len = desc->len;
        uint32_t buf_da = static_cast<uint32_t>(desc->addr);

        volatile struct rpmsg_hdr *hdr = nullptr;
        if (buf_da != 0 && buf_len >= sizeof(struct rpmsg_hdr)) {
            hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));
            if (hdr->len > buf_len - sizeof(struct rpmsg_hdr)) {
                hdr = nullptr;
            }
        }

        if (hdr == nullptr) {
            // Malformed buffer: count it, deliver nothing, give the buffer back.
            s_ring_errors.fetch_add(1, std::memory_order_relaxed);
        } else {
            RpmsgMessage msg;
            msg.src = hdr->src;
            msg.dst = hdr->dst;
            msg.len = hdr->len;
            msg.data = const_cast<const uint8_t *>(hdr->data);
            msg.desc_idx = desc_idx;

            s_rx_count.fetch_add(1, std::memory_order_relaxed);

            // Dispatch to registered endpoint callback
            for (size_t i = 0; i < s_num_endpoints; ++i) {
                if (s_endpoints[i].addr == msg.dst || s_endpoints[i].addr == 0) {
                    if (s_endpoints[i].cb) {
                        s_endpoints[i].cb(msg, s_endpoints[i].user_data);
                    }
                    break;
                }
            }
        }

        // Return the consumed descriptor to the host
        uint16_t used_idx = s_rx_vq.used->idx;
        s_rx_vq.used->ring[used_idx & s_rx_vq.mask].id = desc_idx;
        s_rx_vq.used->ring[used_idx & s_rx_vq.mask].len = 0;
        std::atomic_thread_fence(std::memory_order_release);
        s_rx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);
        s_rx_vq.last_avail_idx++;
    }

    // Tell the host its transmit buffers are back
    notify_host();
    return true;
}

bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_vdev_ready || s_tx_vq.avail == nullptr) {
        return false;
    }
    if (len > RPMSG_BUFFER_SIZE - sizeof(struct rpmsg_hdr)) {
        return false;
    }

    // Take a free buffer from the host RX ring (vring[0]). If none is free the
    // host has not processed earlier replies yet: ring its doorbell and wait.
    uint16_t free_bufs = 0;
    for (;;) {
        if (!avail_pending(s_tx_vq, free_bufs)) {
            return false;
        }
        if (free_bufs > 0) {
            break;
        }
        s_tx_full_waits.fetch_add(1, std::memory_order_relaxed);
        notify_host();
    }

    uint16_t desc_idx = s_tx_vq.avail->ring[s_tx_vq.last_avail_idx & s_tx_vq.mask];
    if (desc_idx >= s_tx_vq.num) {
        s_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];
    uint32_t buf_da = static_cast<uint32_t>(desc->addr);
    if (buf_da == 0 || desc->len < sizeof(struct rpmsg_hdr) + len) {
        s_ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(translate_da(buf_da));

    hdr->dst = incoming.src;
    hdr->src = incoming.dst;
    hdr->len = len;
    hdr->flags = 0;
    hdr->reserved = 0;

    if (payload && len > 0) {
        uint8_t *dst_buf = reinterpret_cast<uint8_t *>(const_cast<uint8_t *>(hdr->data));
        const uint8_t *src_buf = reinterpret_cast<const uint8_t *>(payload);
        for (uint16_t i = 0; i < len; ++i) {
            dst_buf[i] = src_buf[i];
        }
    }

    s_tx_vq.last_avail_idx++;

    // Add to Used Ring for Linux Host to receive
    uint16_t used_idx = s_tx_vq.used->idx;
    s_tx_vq.used->ring[used_idx & s_tx_vq.mask].id = desc_idx;
    s_tx_vq.used->ring[used_idx & s_tx_vq.mask].len = sizeof(struct rpmsg_hdr) + len;
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx = static_cast<uint16_t>(used_idx + 1);

    notify_host();

    s_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

uint32_t Rpmsg::get_rx_count() noexcept {
    return s_rx_count.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_tx_count() noexcept {
    return s_tx_count.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_ring_errors() noexcept {
    return s_ring_errors.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_tx_full_waits() noexcept {
    return s_tx_full_waits.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_mbox_full_waits() noexcept {
    return s_mbox_full_waits.load(std::memory_order_relaxed);
}

} // namespace hal
