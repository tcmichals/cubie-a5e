#include "ipc_protocol.hpp"
#include "CppUTest/TestHarness.h"
#include <string.h>

TEST_GROUP(PcieTlpEncodingTest) {
    void setup() {}
    void teardown() {}
};

TEST(PcieTlpEncodingTest, MemoryWrite3DWTlpLayout) {
    fc::ipc::PcieTlpPayload tlp;
    memset(&tlp, 0, sizeof(tlp));

    tlp.tlp_fmt_type = 0x40;  // 3DW Header with Data (MWr 32-bit)
    tlp.traffic_class = 0;    // TC 0
    tlp.length_dw = 4;        // 4 Dwords (16 bytes)
    tlp.requester_id = 0x0100; // Bus 1, Dev 0, Func 0
    tlp.tag = 0x07;
    tlp.last_first_be = 0xFF; // Last=0xF, First=0xF
    tlp.address_lo = 0x80000000;
    tlp.payload_bytes = 16;

    for (int i = 0; i < 16; ++i) {
        tlp.data[i] = static_cast<uint8_t>(0x10 + i);
    }

    // Check packet size
    LONGS_EQUAL(128, sizeof(fc::ipc::IpcPacket));
    LONGS_EQUAL(0x40, tlp.tlp_fmt_type);
    LONGS_EQUAL(4, tlp.length_dw);
    LONGS_EQUAL(0x80000000, tlp.address_lo);
    BYTES_EQUAL(0x10, tlp.data[0]);
    BYTES_EQUAL(0x1F, tlp.data[15]);
}

TEST(PcieTlpEncodingTest, CompletionWithDataTlpLayout) {
    fc::ipc::PcieTlpPayload tlp;
    memset(&tlp, 0, sizeof(tlp));

    tlp.tlp_fmt_type = 0x4A;  // CplD (Completion with Data)
    tlp.length_dw = 2;        // 2 Dwords (8 bytes)
    tlp.requester_id = 0x0000;
    tlp.tag = 0x03;
    tlp.payload_bytes = 8;
    tlp.data[0] = 0xCA;
    tlp.data[1] = 0xFE;
    tlp.data[2] = 0xBA;
    tlp.data[3] = 0xBE;

    LONGS_EQUAL(0x4A, tlp.tlp_fmt_type);
    LONGS_EQUAL(2, tlp.length_dw);
    BYTES_EQUAL(0xCA, tlp.data[0]);
    BYTES_EQUAL(0xFE, tlp.data[1]);
    BYTES_EQUAL(0xBA, tlp.data[2]);
    BYTES_EQUAL(0xBE, tlp.data[3]);
}
