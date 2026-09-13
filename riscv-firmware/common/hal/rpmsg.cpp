#include "rpmsg.hpp"
#include "msgbox.hpp"
#include <string.h>

namespace hal {

/*
 * VirtIO Ring Internal Descriptors
 */
struct VirtioDesc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct VirtioAvail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VRING_NUM_DESCS];
} __attribute__((packed));

struct VirtioUsedElem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct VirtioUsed {
    uint16_t flags;
    uint16_t idx;
    struct VirtioUsedElem ring[VRING_NUM_DESCS];
} __attribute__((packed));

namespace {
    const struct rpmsg_resource_table *s_rsc = nullptr;
    bool s_vdev_ready = false;
    bool s_ns_announced = false;

    std::atomic<uint32_t> s_rx_count{0};
    std::atomic<uint32_t> s_tx_count{0};

    Rpmsg::EndpointEntry s_endpoints[Rpmsg::MAX_ENDPOINTS];
    size_t s_num_endpoints = 0;

    struct VirtQueueLayout {
        uint32_t da;
        uint32_t num;
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

    void setup_vq(VirtQueueLayout &vq, uint32_t da, uint32_t num, uint32_t align) {
        vq.da = da;
        vq.num = num;
        vq.align = align;
        vq.last_avail_idx = 0;

        if (da == 0) {
            vq.desc = nullptr;
            vq.avail = nullptr;
            vq.used = nullptr;
            return;
        }

        uint8_t *base = reinterpret_cast<uint8_t *>(da);
        vq.desc = reinterpret_cast<volatile struct VirtioDesc *>(base);

        size_t avail_offset = sizeof(struct VirtioDesc) * num;
        vq.avail = reinterpret_cast<volatile struct VirtioAvail *>(base + avail_offset);

        size_t used_offset = (avail_offset + sizeof(struct VirtioAvail) + align - 1) & ~(align - 1);
        vq.used = reinterpret_cast<volatile struct VirtioUsed *>(base + used_offset);
    }
}

void Rpmsg::init(const struct rpmsg_resource_table *rsc) noexcept {
    MsgBox::init();
    s_rsc = rsc;
    s_vdev_ready = false;
    s_ns_announced = false;
    s_rx_count.store(0, std::memory_order_relaxed);
    s_tx_count.store(0, std::memory_order_relaxed);
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
            setup_vq(s_tx_vq, rsc->vdev.vring[0].da, rsc->vdev.vring[0].num, rsc->vdev.vring[0].align);
            setup_vq(s_rx_vq, rsc->vdev.vring[1].da, rsc->vdev.vring[1].num, rsc->vdev.vring[1].align);
            s_vdev_ready = true;
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
    if (s_tx_vq.last_avail_idx == s_tx_vq.avail->idx) return false;

    struct rpmsg_ns_msg {
        char name[32];
        uint32_t addr;
        uint32_t flags;
    } __attribute__((packed)) ns_msg;

    memset(&ns_msg, 0, sizeof(ns_msg));
    strncpy(ns_msg.name, name, sizeof(ns_msg.name) - 1);
    ns_msg.addr = addr;
    ns_msg.flags = 0; // RPMSG_NS_CREATE

    uint16_t avail_slot = s_tx_vq.last_avail_idx % s_tx_vq.num;
    uint16_t desc_idx = s_tx_vq.avail->ring[avail_slot];
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));
    hdr->src = addr;
    hdr->dst = 53; // RPMSG_NS_ADDR
    hdr->len = sizeof(ns_msg);
    hdr->flags = 0;
    hdr->reserved = 0;

    memcpy(const_cast<uint8_t *>(hdr->data), &ns_msg, sizeof(ns_msg));

    s_tx_vq.last_avail_idx++;

    uint16_t used_slot = s_tx_vq.used->idx % s_tx_vq.num;
    s_tx_vq.used->ring[used_slot].id = desc_idx;
    s_tx_vq.used->ring[used_slot].len = sizeof(struct rpmsg_hdr) + sizeof(ns_msg);
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx++;

    MsgBox::send(MsgBox::Channel::Channel1, 0);
    s_ns_announced = true;
    return true;
}

bool Rpmsg::poll() noexcept {
    if (!is_driver_ready() || s_rx_vq.avail == nullptr) {
        return false;
    }

    if (MsgBox::is_rx_pending(MsgBox::Channel::Channel0)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel0);
    }
    if (MsgBox::is_rx_pending(MsgBox::Channel::Channel1)) {
        (void)MsgBox::receive(MsgBox::Channel::Channel1);
    }

    std::atomic_thread_fence(std::memory_order_acquire);
    uint16_t avail_idx = s_rx_vq.avail->idx;

    if (s_rx_vq.last_avail_idx == avail_idx) {
        return false; // No new packets
    }

    bool processed = false;
    while (s_rx_vq.last_avail_idx != avail_idx) {
        uint16_t desc_idx = s_rx_vq.avail->ring[s_rx_vq.last_avail_idx % s_rx_vq.num];
        volatile struct VirtioDesc *desc = &s_rx_vq.desc[desc_idx];

        if (desc->addr != 0 && desc->len >= sizeof(struct rpmsg_hdr)) {
            volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));

            RpmsgMessage msg;
            msg.src = hdr->src;
            msg.dst = hdr->dst;
            msg.len = hdr->len;
            msg.data = const_cast<const uint8_t *>(hdr->data);
            msg.desc_idx = desc_idx;

            s_rx_count.fetch_add(1, std::memory_order_relaxed);
            processed = true;

            // Dispatch to registered endpoint callback
            for (size_t i = 0; i < s_num_endpoints; ++i) {
                if (s_endpoints[i].addr == msg.dst || s_endpoints[i].addr == 0) {
                    if (s_endpoints[i].cb) {
                        s_endpoints[i].cb(msg, s_endpoints[i].user_data);
                    }
                    break;
                }
            }

            // Return consumed RX descriptor to Host
            uint16_t used_slot = s_rx_vq.used->idx % s_rx_vq.num;
            s_rx_vq.used->ring[used_slot].id = desc_idx;
            s_rx_vq.used->ring[used_slot].len = 0;
            std::atomic_thread_fence(std::memory_order_release);
            s_rx_vq.used->idx++;
        }

        s_rx_vq.last_avail_idx++;
    }

    if (processed) {
        // Kick Linux Host on Channel 1 so Host reclaims transmit buffer
        MsgBox::send(MsgBox::Channel::Channel1, 0);
    }

    return processed;
}

bool Rpmsg::reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept {
    if (!s_vdev_ready || s_tx_vq.avail == nullptr) {
        return false;
    }

    // Allocate an available transmit buffer from Host RX queue (vring[0])
    if (s_tx_vq.last_avail_idx == s_tx_vq.avail->idx) {
        return false; // Host RX pool exhausted
    }

    uint16_t avail_slot = s_tx_vq.last_avail_idx % s_tx_vq.num;
    uint16_t desc_idx = s_tx_vq.avail->ring[avail_slot];
    volatile struct VirtioDesc *desc = &s_tx_vq.desc[desc_idx];
    if (desc->addr == 0) return false;

    volatile struct rpmsg_hdr *hdr = reinterpret_cast<volatile struct rpmsg_hdr *>(static_cast<uintptr_t>(desc->addr));

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
    uint16_t used_slot = s_tx_vq.used->idx % s_tx_vq.num;
    s_tx_vq.used->ring[used_slot].id = desc_idx;
    s_tx_vq.used->ring[used_slot].len = sizeof(struct rpmsg_hdr) + len;
    std::atomic_thread_fence(std::memory_order_release);
    s_tx_vq.used->idx++;

    // Kick Linux host on Channel 1 (Linux rx channel)
    MsgBox::send(MsgBox::Channel::Channel1, 0);

    s_tx_count.fetch_add(1, std::memory_order_relaxed);
    return true;
}

uint32_t Rpmsg::get_rx_count() noexcept {
    return s_rx_count.load(std::memory_order_relaxed);
}

uint32_t Rpmsg::get_tx_count() noexcept {
    return s_tx_count.load(std::memory_order_relaxed);
}

} // namespace hal
