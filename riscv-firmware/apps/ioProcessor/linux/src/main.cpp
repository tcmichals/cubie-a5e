#include "ipc_bridge.hpp"
#include "log_detokenizer.hpp"
#include "trace_writer.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <csignal>

static volatile bool g_running = true;

void sig_handler(int) {
    g_running = false;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    std::cout << "===============================================================\n";
    std::cout << "    RADXA CUBIE A5E — FLIGHT CONTROLLER LINUX HOST BRIDGE      \n";
    std::cout << "===============================================================\n";

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    bridge::IpcBridge bridge;
    if (!bridge.open_shm()) {
        std::cerr << "[Main] Error: Could not initialize shared SRAM memory bridge.\n";
        return 1;
    }

    bridge::LogDetokenizer detokenizer;
    bridge::TraceWriter trace_writer;
    trace_writer.open_trace_file("flight_trace.ctf");

    std::cout << "[Main] Bridge active. Listening for telemetry, logs, and traces...\n";

    uint32_t tlp_rx_count = 0;
    uint32_t spi_rx_count = 0;
    uint32_t uart_rx_count = 0;
    auto last_stats_time = std::chrono::steady_clock::now();

    fc::ipc::IpcPacket rx_pkt;
    while (g_running) {
        bool received_any = false;

        // Ingest packets from RISC-V TX Queue
        while (bridge.poll_rx_packet(rx_pkt)) {
            received_any = true;

            switch (static_cast<fc::ipc::PacketType>(rx_pkt.header.type)) {
            case fc::ipc::PacketType::PigweedLog:
                detokenizer.print_log(rx_pkt);
                break;

            case fc::ipc::PacketType::PcieTlpFromFpga:
                tlp_rx_count++;
                break;

            case fc::ipc::PacketType::RawSpiTransfer:
                spi_rx_count++;
                break;

            case fc::ipc::PacketType::RawUartStream:
                uart_rx_count++;
                break;

            case fc::ipc::PacketType::BarectfTrace:
                trace_writer.write_chunk(rx_pkt);
                break;

            default:
                break;
            }
        }

        // Print rate statistics every second
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_stats_time).count() >= 1000) {
            if (tlp_rx_count > 0 || spi_rx_count > 0 || uart_rx_count > 0) {
                std::cout << "[I/O Throughput] PCIe TLPs: " << tlp_rx_count 
                          << " pkts/s | IMU SPI: " << spi_rx_count 
                          << " pkts/s | UART: " << uart_rx_count << " pkts/s\n";
                tlp_rx_count = 0;
                spi_rx_count = 0;
                uart_rx_count = 0;
            }

            // Check if RISC-V halted on a trap
            uint32_t mepc = 0, mcause = 0, mtval = 0;
            if (bridge.check_crash_dump(mepc, mcause, mtval)) {
                std::cerr << "\n[CRITICAL] RISC-V Core Crashed! Trap Dump:\n";
                std::cerr << "  MEPC:   0x" << std::hex << mepc << "\n";
                std::cerr << "  MCAUSE: 0x" << std::hex << mcause << "\n";
                std::cerr << "  MTVAL:  0x" << std::hex << mtval << std::dec << "\n";
            }

            last_stats_time = now;
        }

        if (!received_any) {
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    }

    std::cout << "\n[Main] Shutting down flight bridge daemon...\n";
    trace_writer.close_trace_file();
    bridge.close_shm();
    return 0;
}
