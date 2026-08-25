#include "io_tasks.hpp"
#include "hardware_awaiters.hpp"
#include "../hal/spi.hpp"
#include "../hal/uart.hpp"
#include "../hal/timer.hpp"
#include "../hal/pio.hpp"
#include "../logging/pw_log_backend.hpp"
#include "../logging/trace_manager.hpp"
#include <etl/array.h>
#include <etl/vector.h>
#include <string.h>

namespace fc::coroutines {

// PCIe TLP Bridge Coroutine Task (Dual-SPI0 <-> FPGA CS0)
abstractx::AsyncTask fpga_pcie_tlp_task(ipc::SpscRingBuffer *rx_ring, ipc::SpscRingBuffer *tx_ring) {
    etl::array<uint8_t, 128> spi_tx_buf;
    etl::array<uint8_t, 128> spi_rx_buf;
    ipc::IpcPacket in_tlp;
    uint16_t tx_seq = 0;

    PW_LOG_INFO("FPGA PCIe TLP Dual-SPI Bridge Task Initialized");

    while (true) {
        bool work_done = false;

        // 1. Process Downlink TLPs from Host Linux -> Transmit over Dual-SPI0 to FPGA
        if (rx_ring && rx_ring->pop(in_tlp)) {
            if (in_tlp.header.type == static_cast<uint16_t>(ipc::PacketType::PcieTlpToFpga)) {
                size_t tlp_len = sizeof(ipc::PcieTlpPayload);
                memcpy(spi_tx_buf.data(), &in_tlp.payload.tlp, tlp_len);

                uint64_t t0 = hal::Timer::get_time_ns();
                hal::Spi0::transceive_fpga_dual(spi_tx_buf.data(), spi_rx_buf.data(), tlp_len);
                uint64_t duration_ns = hal::Timer::get_time_ns() - t0;

                logging::TraceManager::trace_spi(0 /* CS0 */, (uint32_t)tlp_len, (uint32_t)duration_ns);
                work_done = true;
            }
        }

        // 2. Process Uplink TLPs from FPGA -> Transmit over Shared SRAM IPC to Host Linux
        if (hal::Pio::get_fpga_frame_ready()) {
            spi_tx_buf.fill(0xFF); // Dummy TX for read burst
            size_t tlp_len = sizeof(ipc::PcieTlpPayload);

            uint64_t t0 = hal::Timer::get_time_ns();
            hal::Spi0::transceive_fpga_dual(spi_tx_buf.data(), spi_rx_buf.data(), tlp_len);
            uint64_t duration_ns = hal::Timer::get_time_ns() - t0;

            logging::TraceManager::trace_spi(0 /* CS0 */, (uint32_t)tlp_len, (uint32_t)duration_ns);

            if (tx_ring) {
                ipc::IpcPacket out_pkt;
                out_pkt.header.magic = ipc::IPC_MAGIC;
                out_pkt.header.seq = tx_seq++;
                out_pkt.header.type = static_cast<uint16_t>(ipc::PacketType::PcieTlpFromFpga);
                out_pkt.header.timestamp_us = hal::Timer::get_time_us();

                memcpy(&out_pkt.payload.tlp, spi_rx_buf.data(), tlp_len);
                tx_ring->push(out_pkt, true); // Ring mailbox doorbell
            }
            work_done = true;
        }

        if (!work_done) {
            co_await sleep_us(100); // Poll rate / await next TLP event
        }
    }
}

// Raw IMU Sensor Acquisition Task (Single-SPI0 CS1)
abstractx::AsyncTask imu_sensor_task(ipc::SpscRingBuffer *tx_ring) {
    etl::array<uint8_t, 32> tx_cmd;
    etl::array<uint8_t, 32> rx_data;
    uint16_t seq = 0;

    tx_cmd.fill(0x00);
    tx_cmd[0] = 0x80 | 0x3B; // Read Accel/Gyro registers

    PW_LOG_INFO("IMU Sensor Acquisition Task Initialized");

    while (true) {
        co_await sleep_us(1000); // 1 kHz Rate Loop

        uint64_t t0 = hal::Timer::get_time_ns();
        hal::Spi0::transceive_imu_single(tx_cmd.data(), rx_data.data(), 16);
        uint64_t duration_ns = hal::Timer::get_time_ns() - t0;

        logging::TraceManager::trace_spi(1 /* CS1 */, 16, (uint32_t)duration_ns);

        if (tx_ring) {
            ipc::IpcPacket pkt;
            pkt.header.magic = ipc::IPC_MAGIC;
            pkt.header.seq = seq++;
            pkt.header.type = static_cast<uint16_t>(ipc::PacketType::RawSpiTransfer);
            pkt.header.timestamp_us = hal::Timer::get_time_us();

            pkt.payload.spi.cs_id = 1; // IMU
            pkt.payload.spi.is_dual_mode = 0;
            pkt.payload.spi.length = 16;
            memcpy(pkt.payload.spi.data, rx_data.data(), 16);

            tx_ring->push(pkt, false);
        }
    }
}

// Raw UART2 Serial Stream Ingestion Task (Pins 11 & 13)
abstractx::AsyncTask uart_stream_task(ipc::SpscRingBuffer *tx_ring) {
    etl::array<uint8_t, 108> rx_buf;
    uint16_t seq = 0;

    PW_LOG_INFO("UART2 Serial Stream Ingestion Task Initialized");

    while (true) {
        size_t bytes = co_await read_uart2_frame(rx_buf.data(), rx_buf.size(), 500);
        if (bytes > 0) {
            logging::TraceManager::trace_uart((uint32_t)bytes, 500);

            if (tx_ring) {
                ipc::IpcPacket pkt;
                pkt.header.magic = ipc::IPC_MAGIC;
                pkt.header.seq = seq++;
                pkt.header.type = static_cast<uint16_t>(ipc::PacketType::RawUartStream);
                pkt.header.timestamp_us = hal::Timer::get_time_us();

                pkt.payload.uart.port_id = 2;
                pkt.payload.uart.length = static_cast<uint16_t>(bytes);
                memcpy(pkt.payload.uart.stream, rx_buf.data(), bytes);

                tx_ring->push(pkt, false);
            }
        } else {
            co_await sleep_ms(2);
        }
    }
}

} // namespace fc::coroutines
