#ifndef FLIGHT_BRIDGE_LOG_DETOKENIZER_HPP
#define FLIGHT_BRIDGE_LOG_DETOKENIZER_HPP

#include <stdint.h>
#include <string>
#include <unordered_map>
#include "../include/ipc_protocol.hpp"

namespace bridge {

class LogDetokenizer {
public:
    LogDetokenizer();
    void register_token(uint32_t token, const std::string &format_string);
    void print_log(const fc::ipc::IpcPacket &packet);

private:
    std::unordered_map<uint32_t, std::string> token_db_;
};

} // namespace bridge

#endif // FLIGHT_BRIDGE_LOG_DETOKENIZER_HPP
