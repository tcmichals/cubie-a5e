#ifndef IOPROCESSOR_IPC_PROTOCOL_HPP
#define IOPROCESSOR_IPC_PROTOCOL_HPP

#include <stdint.h>

namespace fc::ipc {

constexpr uint32_t IPC_MAGIC = 0x544C5049; // 'TLPI' (PCIe TLP I/O Processor)

enum class PacketType : uint16_t {
    Heartbeat           = 0x0001,
    PcieTlpToFpga       = 0x0010, // Host Linux -> RISC-V -> FPGA (Memory Write/Read TLP)
    PcieTlpFromFpga     = 0x0011, // FPGA -> RISC-V -> Host Linux (Completion/Interrupt TLP)
    RawSpiTransfer      = 0x0020, // Raw SPI frame (IMU, sensors)
    RawUartStream       = 0x0030, // Raw UART serial stream
    PigweedLog          = 0x0040, // pw_tokenizer 4-byte tokenized log
    BarectfTrace        = 0x0050, // CTF binary trace packet
    CrashDump           = 0x00EE, // Fatal register snapshot
};

struct __attribute__((packed, aligned(4))) Header {
    uint32_t magic;         // IPC_MAGIC
    uint16_t seq;           // Monotonic sequence number
    uint16_t type;          // PacketType
    uint64_t timestamp_us;  // Microseconds since boot
};

/*
 * PCIe Transaction Layer Packet (TLP) Structure
 * Supports Memory Read/Write, Configuration, and Completion TLPs across Dual-SPI
 */
struct __attribute__((packed, aligned(4))) PcieTlpPayload {
    uint8_t  tlp_fmt_type;  // TLP Format & Type (e.g. 0x00=MRd32, 0x40=MWr32, 0x4A=CplD)
    uint8_t  traffic_class; // TC (3-bit) + Attributes
    uint16_t length_dw;     // Length in 32-bit DWords (1 to 24)
    uint16_t requester_id;  // Bus/Dev/Fn ID
    uint8_t  tag;           // Transaction tag
    uint8_t  last_first_be; // Last [7:4] and First [3:0] Byte Enables
    uint32_t address_lo;    // Target PCIe Physical Address (Low 32 bits)
    uint32_t address_hi;    // Target PCIe Physical Address (High 32 bits, 0 for 32-bit TLP)
    uint16_t payload_bytes; // Actual valid payload bytes in data array (max 92 bytes)
    uint16_t flags;         // Bit 0: Is Last Chunk of multi-TLP burst
    uint8_t  data[92];      // TLP Data Payload
};

struct __attribute__((packed, aligned(4))) RawSpiPayload {
    uint8_t  cs_id;         // 0=CS0 (FPGA), 1=CS1 (IMU)
    uint8_t  is_dual_mode;  // 0=Single Mode, 1=Dual-IO Mode
    uint16_t length;        // Valid byte count in data (up to 108)
    uint8_t  data[108];     // SPI TX/RX buffer
};

struct __attribute__((packed, aligned(4))) RawUartPayload {
    uint8_t  port_id;       // 2 = UART2
    uint8_t  reserved;
    uint16_t length;        // Valid byte count in stream
    uint8_t  stream[108];   // UART byte stream
};

struct __attribute__((packed, aligned(4))) PigweedLogPayload {
    uint32_t token;         // 32-bit pw_tokenizer hash
    uint16_t data_len;      // Length of packed varint arguments
    uint8_t  args[106];     // Packed argument buffer
};

struct __attribute__((packed, aligned(4))) BarectfTracePayload {
    uint32_t chunk_id;      // Trace stream sequence chunk
    uint16_t data_len;      // Valid bytes in chunk
    uint8_t  stream[106];   // CTF binary stream slice
};

/* Exactly 128 Bytes Fixed Packet Structure */
struct __attribute__((packed, aligned(4))) IpcPacket {
    Header header; // 16 Bytes
    union {
        PcieTlpPayload       tlp;
        RawSpiPayload        spi;
        RawUartPayload       uart;
        PigweedLogPayload    pw_log;
        BarectfTracePayload  trace;
        uint8_t              raw[112];
    } payload;     // 112 Bytes
};

static_assert(sizeof(IpcPacket) == 128, "IpcPacket must be exactly 128 bytes!");

} // namespace fc::ipc

#endif // IOPROCESSOR_IPC_PROTOCOL_HPP
