#ifndef FLIGHT_BRIDGE_TRACE_WRITER_HPP
#define FLIGHT_BRIDGE_TRACE_WRITER_HPP

#include <stdint.h>
#include <string>
#include <fstream>
#include "../include/ipc_protocol.hpp"

namespace bridge {

class TraceWriter {
public:
    TraceWriter();
    ~TraceWriter();

    bool open_trace_file(const std::string &path = "flight_trace.ctf");
    void close_trace_file();
    void write_chunk(const fc::ipc::IpcPacket &packet);

private:
    std::ofstream file_;
    size_t total_bytes_written_;
};

} // namespace bridge

#endif // FLIGHT_BRIDGE_TRACE_WRITER_HPP
