#include "ringbuffer.hpp"
#include "ipc_protocol.hpp"
#include "CppUTest/TestHarness.h"
#include <string.h>

TEST_GROUP(SpscRingBufferTest) {
    uint8_t buffer_memory[32768];
    fc::ipc::SpscRingBuffer* ring;

    void setup() {
        memset(buffer_memory, 0, sizeof(buffer_memory));
        ring = new fc::ipc::SpscRingBuffer(reinterpret_cast<uintptr_t>(buffer_memory));
    }

    void teardown() {
        delete ring;
    }
};

TEST(SpscRingBufferTest, PushAndPopSinglePacket) {
    CHECK_TRUE(ring->is_empty());
    CHECK_FALSE(ring->is_full());

    fc::ipc::IpcPacket tx_pkt;
    memset(&tx_pkt, 0, sizeof(tx_pkt));
    tx_pkt.header.magic = fc::ipc::IPC_MAGIC;
    tx_pkt.header.seq = 42;
    tx_pkt.header.type = static_cast<uint16_t>(fc::ipc::PacketType::PcieTlpToFpga);
    tx_pkt.payload.tlp.tlp_fmt_type = 0x40; // 3DW MWr
    tx_pkt.payload.tlp.length_dw = 1;
    tx_pkt.payload.tlp.address_lo = 0xA0001000;
    tx_pkt.payload.tlp.data[0] = 0xDE;
    tx_pkt.payload.tlp.data[1] = 0xAD;

    bool pushed = ring->push(tx_pkt, false);
    CHECK_TRUE(pushed);
    CHECK_FALSE(ring->is_empty());

    fc::ipc::IpcPacket rx_pkt;
    memset(&rx_pkt, 0, sizeof(rx_pkt));
    bool popped = ring->pop(rx_pkt);
    CHECK_TRUE(popped);
    CHECK_TRUE(ring->is_empty());

    BYTES_EQUAL(fc::ipc::IPC_MAGIC, rx_pkt.header.magic);
    LONGS_EQUAL(42, rx_pkt.header.seq);
    LONGS_EQUAL(static_cast<uint16_t>(fc::ipc::PacketType::PcieTlpToFpga), rx_pkt.header.type);
    LONGS_EQUAL(0xA0001000, rx_pkt.payload.tlp.address_lo);
    BYTES_EQUAL(0xDE, rx_pkt.payload.tlp.data[0]);
    BYTES_EQUAL(0xAD, rx_pkt.payload.tlp.data[1]);
}

TEST(SpscRingBufferTest, BufferWrapAroundIntegrity) {
    // Fill and empty buffer multiple times to force head/tail index wrap-around
    for (uint32_t round = 0; round < 100; ++round) {
        fc::ipc::IpcPacket tx_pkt;
        memset(&tx_pkt, 0, sizeof(tx_pkt));
        tx_pkt.header.magic = fc::ipc::IPC_MAGIC;
        tx_pkt.header.seq = static_cast<uint16_t>(round);
        tx_pkt.header.type = static_cast<uint16_t>(fc::ipc::PacketType::RawUartStream);
        tx_pkt.payload.uart.port_id = 2;
        tx_pkt.payload.uart.length = 4;
        tx_pkt.payload.uart.stream[0] = 0xAA;
        tx_pkt.payload.uart.stream[1] = 0xBB;
        tx_pkt.payload.uart.stream[2] = 0xCC;
        tx_pkt.payload.uart.stream[3] = static_cast<uint8_t>(round);

        CHECK_TRUE(ring->push(tx_pkt, false));

        fc::ipc::IpcPacket rx_pkt;
        CHECK_TRUE(ring->pop(rx_pkt));
        LONGS_EQUAL(round, rx_pkt.header.seq);
        BYTES_EQUAL(static_cast<uint8_t>(round), rx_pkt.payload.uart.stream[3]);
    }
    CHECK_TRUE(ring->is_empty());
}
