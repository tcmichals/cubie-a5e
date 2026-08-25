#include "trace_writer.hpp"
#include <iostream>

namespace bridge {

TraceWriter::TraceWriter() : total_bytes_written_(0) {}

TraceWriter::~TraceWriter() {
    close_trace_file();
}

bool TraceWriter::open_trace_file(const std::string &path) {
    file_.open(path, std::ios::binary | std::ios::out);
    if (!file_.is_open()) {
        std::cerr << "[Trace Writer] Failed to open " << path << " for writing\n";
        return false;
    }
    std::cout << "[Trace Writer] Logging binary CTF trace stream to " << path << "\n";
    return true;
}

void TraceWriter::close_trace_file() {
    if (file_.is_open()) {
        file_.close();
        std::cout << "[Trace Writer] Saved " << total_bytes_written_ << " bytes to CTF trace\n";
    }
}

void TraceWriter::write_chunk(const fc::ipc::IpcPacket &packet) {
    if (!file_.is_open()) return;

    uint16_t len = packet.payload.trace.data_len;
    if (len > 0 && len <= sizeof(packet.payload.trace.stream)) {
        file_.write(reinterpret_cast<const char *>(packet.payload.trace.stream), len);
        total_bytes_written_ += len;
    }
}

} // namespace bridge
