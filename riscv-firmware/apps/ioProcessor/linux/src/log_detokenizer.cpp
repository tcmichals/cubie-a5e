#include "log_detokenizer.hpp"
#include <iostream>
#include <iomanip>
#include "../../third_party/pigweed/pw_tokenizer/tokenize.h"

namespace bridge {

LogDetokenizer::LogDetokenizer() {
    // Populate default tokens matching the firmware compile-time hashes
    register_token(PW_TOKENIZE_STRING("XuanTie E907 Hardware I/O & PCIe TLP Processor Booting..."), "XuanTie E907 Hardware I/O & PCIe TLP Processor Booting...");
    register_token(PW_TOKENIZE_STRING("AbstractX C++20 Coroutine Scheduler Initialized"), "AbstractX C++20 Coroutine Scheduler Initialized");
    register_token(PW_TOKENIZE_STRING("FPGA PCIe TLP Dual-SPI Bridge Task Initialized"), "FPGA PCIe TLP Dual-SPI Bridge Task Initialized");
    register_token(PW_TOKENIZE_STRING("IMU Sensor Acquisition Task Initialized"), "IMU Sensor Acquisition Task Initialized");
    register_token(PW_TOKENIZE_STRING("UART2 Serial Stream Ingestion Task Initialized"), "UART2 Serial Stream Ingestion Task Initialized");
    register_token(PW_TOKENIZE_STRING("Entering Hard Real-Time I/O Event Loop"), "Entering Hard Real-Time I/O Event Loop");
}

void LogDetokenizer::register_token(uint32_t token, const std::string &format_string) {
    token_db_[token] = format_string;
}

void LogDetokenizer::print_log(const fc::ipc::IpcPacket &packet) {
    uint32_t token = packet.payload.pw_log.token;
    double sec = static_cast<double>(packet.header.timestamp_us) / 1000000.0;

    std::cout << "[" << std::fixed << std::setprecision(6) << sec << " s] [RISC-V] ";

    auto it = token_db_.find(token);
    if (it != token_db_.end()) {
        std::cout << it->second << "\n";
    } else {
        std::cout << "Token [0x" << std::hex << token << std::dec << "] (Unknown string)\n";
    }
}

} // namespace bridge
