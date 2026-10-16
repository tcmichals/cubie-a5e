/*
 * stress_rpmsg.cpp - RPMsg stress test: keep both directions of the
 * remoteproc / mailbox / virtio path as full as possible.
 *
 * A writer thread sends pings with blocking write(): when every TX buffer is
 * in use the kernel makes it wait, which is the backpressure being tested.
 * A reader thread receives the E906's pongs. Up to --window pings are in
 * flight at once (0 = no limit, the kernel's TX buffers are the only limit).
 *
 * Every pong is checked:
 *   - malformed (wrong length, tag, payload bytes, or a sequence number that
 *     was never sent or was already answered): error, the run stops at once
 *   - dropped (a sequence number skipped over): counted, the run goes on
 * After the last pong the reader keeps listening for --linger ms; anything
 * that arrives then (a duplicate or a stale reply) is an error.
 *
 * Exit status is 0 only with no errors and no drops.
 */

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <vector>

#if __has_include(<linux/rpmsg.h>)
#include <linux/rpmsg.h>
#else
struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};
#define RPMSG_CREATE_EPT_IOCTL _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#endif

namespace {

constexpr uint32_t RPMSG_PING_EPT_ADDR = 1024;
constexpr size_t HDR_LEN = 16; // tag(4) + seq(4) + tx timestamp(8)

uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

uint8_t pattern_byte(uint32_t seq, size_t i) {
    return (uint8_t)(seq * 31u + i * 7u);
}

void fill_ping(std::vector<uint8_t> &buf, uint32_t seq, uint64_t ts) {
    memcpy(buf.data(), "PING", 4);
    memcpy(buf.data() + 4, &seq, 4);
    memcpy(buf.data() + 8, &ts, 8);
    for (size_t i = HDR_LEN; i < buf.size(); ++i) {
        buf[i] = pattern_byte(seq, i);
    }
}

void hexdump(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n && i < 64; ++i) {
        fprintf(stderr, "%02x%s", p[i], (i % 16 == 15) ? "\n" : " ");
    }
    fprintf(stderr, "\n");
}

/* Wait for a device node to appear (inotify, no sleeping). */
bool wait_for_node(const std::string &path, int timeout_ms) {
    if (access(path.c_str(), F_OK) == 0) {
        return true;
    }
    int in = inotify_init1(IN_CLOEXEC);
    if (in < 0) {
        return false;
    }
    std::string dir = path.substr(0, path.rfind('/'));
    inotify_add_watch(in, dir.c_str(), IN_CREATE | IN_ATTRIB);
    uint64_t deadline = now_ns() + (uint64_t)timeout_ms * 1000000ULL;
    bool found = access(path.c_str(), F_OK) == 0;
    while (!found) {
        uint64_t now = now_ns();
        if (now >= deadline) {
            break;
        }
        struct pollfd p = {in, POLLIN, 0};
        if (poll(&p, 1, (int)((deadline - now) / 1000000ULL) + 1) > 0) {
            char ev[4096];
            (void)!read(in, ev, sizeof(ev));
        }
        found = access(path.c_str(), F_OK) == 0;
    }
    close(in);
    return found;
}

int open_endpoint(std::string dev) {
    if (dev.find("rpmsg_ctrl") != std::string::npos) {
        int ctrl = open(dev.c_str(), O_RDWR);
        if (ctrl < 0) {
            std::cerr << "[ERROR] open " << dev << ": " << strerror(errno) << "\n";
            return -1;
        }
        struct rpmsg_endpoint_info info;
        memset(&info, 0, sizeof(info));
        strncpy(info.name, "rpmsg-ping-channel", sizeof(info.name) - 1);
        info.src = RPMSG_PING_EPT_ADDR;
        info.dst = RPMSG_PING_EPT_ADDR;
        if (ioctl(ctrl, RPMSG_CREATE_EPT_IOCTL, &info) < 0) {
            std::cerr << "[ERROR] RPMSG_CREATE_EPT_IOCTL: " << strerror(errno) << "\n";
            close(ctrl);
            return -1;
        }
        close(ctrl);
        dev = "/dev/rpmsg0";
        if (!wait_for_node(dev, 2000)) {
            std::cerr << "[ERROR] " << dev << " did not appear\n";
            return -1;
        }
    }
    int fd = open(dev.c_str(), O_RDWR);
    if (fd < 0) {
        std::cerr << "[ERROR] open " << dev << ": " << strerror(errno) << "\n";
    }
    return fd;
}

struct Shared {
    std::mutex lock;
    std::condition_variable space;
    uint32_t sent = 0;      // pings written
    uint32_t settled = 0;   // pings answered or known dropped
    bool stop = false;
};

} // namespace

int main(int argc, char *argv[]) {
    uint32_t count = 100000;
    uint32_t window = 64;
    uint32_t size = 496;
    uint32_t timeout_ms = 2000;
    uint32_t linger_ms = 300;
    std::string dev;

    static struct option opts[] = {
        {"dev", required_argument, 0, 'd'},   {"count", required_argument, 0, 'n'},
        {"window", required_argument, 0, 'w'}, {"size", required_argument, 0, 's'},
        {"timeout", required_argument, 0, 't'}, {"linger", required_argument, 0, 'l'},
        {"help", no_argument, 0, 'h'},        {0, 0, 0, 0}};
    int c;
    while ((c = getopt_long(argc, argv, "d:n:w:s:t:l:h", opts, nullptr)) != -1) {
        switch (c) {
        case 'd': dev = optarg; break;
        case 'n': count = std::stoul(optarg); break;
        case 'w': window = std::stoul(optarg); break;
        case 's': size = std::stoul(optarg); break;
        case 't': timeout_ms = std::stoul(optarg); break;
        case 'l': linger_ms = std::stoul(optarg); break;
        default:
            std::cout << "Usage: " << argv[0]
                      << " [-n count] [-w window (0=unlimited)] [-s size 16..496]"
                         " [-t timeout_ms] [-l linger_ms] [-d dev]\n";
            return c == 'h' ? 0 : 1;
        }
    }
    if (count == 0) {
        count = 1;
    }
    if (size < HDR_LEN) size = HDR_LEN;
    if (size > 496) size = 496;

    if (dev.empty()) {
        dev = access("/dev/rpmsg0", F_OK) == 0 ? "/dev/rpmsg0" : "/dev/rpmsg_ctrl0";
    }

    std::cout << "================================================================\n"
              << "  RPMsg stress test (both directions)\n"
              << "  Count: " << count << " | Window: " << (window ? std::to_string(window) : "unlimited")
              << " | Payload: " << size << " bytes\n"
              << "================================================================\n";

    int fd = open_endpoint(dev);
    if (fd < 0) {
        return 1;
    }

    // Anything already queued is left over from an earlier run: a duplicate
    // or late reply. That is an error, not something to quietly discard.
    {
        std::vector<uint8_t> junk(512);
        int stale = 0;
        struct pollfd p = {fd, POLLIN, 0};
        while (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
            ssize_t n = read(fd, junk.data(), junk.size());
            if (n <= 0) break;
            if (stale++ == 0) {
                std::cerr << "[ERROR] stale message queued before the run started:\n";
                hexdump(junk.data(), (size_t)n);
            }
        }
        if (stale) {
            std::cerr << "[ERROR] " << stale << " stale message(s) before the run\n";
            std::cout << "Data Integrity : FAIL (" << stale << " stale messages before start)\n";
            close(fd);
            return 1;
        }
    }

    Shared sh;
    std::vector<uint64_t> tx_ts(count + 1, 0);
    std::vector<double> rtt_us;
    rtt_us.reserve(count);
    std::atomic<uint32_t> write_errors{0};
    uint64_t start_ns = now_ns();

    std::thread writer([&] {
        std::vector<uint8_t> buf(size);
        for (uint32_t seq = 1; seq <= count; ++seq) {
            {
                std::unique_lock<std::mutex> lk(sh.lock);
                sh.space.wait(lk, [&] { return sh.stop || window == 0 || sh.sent - sh.settled < window; });
                if (sh.stop) return;
                sh.sent = seq; // before write(): the reply can arrive before write() returns
            }
            uint64_t ts = now_ns();
            tx_ts[seq] = ts;
            fill_ping(buf, seq, ts);
            ssize_t n;
            do {
                n = write(fd, buf.data(), buf.size()); // blocks while no TX buffer is free
            } while (n < 0 && errno == EINTR);
            if (n != (ssize_t)buf.size()) {
                std::cerr << "[ERROR] write seq=" << seq << ": "
                          << (n < 0 ? strerror(errno) : "short write") << "\n";
                write_errors++;
                std::lock_guard<std::mutex> lk(sh.lock);
                sh.stop = true;
                sh.space.notify_all();
                return;
            }
        }
    });

    uint32_t next = 1;      // next sequence number expected
    uint32_t received = 0;
    uint32_t dropped = 0;
    uint32_t timeouts = 0;
    std::string error;
    std::vector<uint8_t> rx(512);

    while (next <= count && error.empty()) {
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, (int)timeout_ms);
        if (r == 0) {
            std::lock_guard<std::mutex> lk(sh.lock);
            if (sh.stop) break;
            timeouts++;
            uint32_t lost = sh.sent - (next - 1);
            std::cerr << "[FAIL] no reply for " << timeout_ms << " ms with " << lost
                      << " ping(s) outstanding (next expected seq=" << next << ")\n";
            dropped += lost;
            break;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            error = std::string("poll: ") + strerror(errno);
            break;
        }
        ssize_t n = read(fd, rx.data(), rx.size());
        uint64_t t = now_ns();
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            error = std::string("read: ") + strerror(errno);
            break;
        }

        uint32_t seq = 0;
        if ((size_t)n == size) memcpy(&seq, rx.data() + 4, 4);
        uint32_t sent_now;
        {
            std::lock_guard<std::mutex> lk(sh.lock);
            sent_now = sh.sent;
        }

        if ((size_t)n != size) {
            error = "wrong length " + std::to_string(n) + " (expected " + std::to_string(size) + ")";
        } else if (memcmp(rx.data(), "PONG", 4) != 0) {
            error = "bad tag";
        } else if (seq < next) {
            error = "duplicate or out-of-order seq=" + std::to_string(seq) +
                    " (expected " + std::to_string(next) + ")";
        } else if (seq > sent_now) {
            error = "seq=" + std::to_string(seq) + " was never sent (sent " + std::to_string(sent_now) + ")";
        } else {
            for (size_t i = HDR_LEN; i < size; ++i) {
                if (rx[i] != pattern_byte(seq, i)) {
                    error = "payload byte " + std::to_string(i) + " wrong in seq=" + std::to_string(seq);
                    break;
                }
            }
        }
        if (!error.empty()) {
            std::cerr << "[ERROR] malformed reply: " << error << "\n";
            hexdump(rx.data(), (size_t)(n > 0 ? n : 0));
            break;
        }

        if (seq > next) {
            dropped += seq - next;
            std::cerr << "[DROP] seq " << next << ".." << (seq - 1) << " never arrived\n";
        }
        rtt_us.push_back((double)(t - tx_ts[seq]) / 1000.0);
        received++;
        next = seq + 1;
        std::lock_guard<std::mutex> lk(sh.lock);
        sh.settled = next - 1;
        sh.space.notify_all();
    }

    {
        std::lock_guard<std::mutex> lk(sh.lock);
        sh.stop = true;
        sh.space.notify_all();
    }
    writer.join();
    uint64_t end_ns = now_ns();

    // Anything arriving after the last reply is a duplicate or a stale reply.
    uint32_t late = 0;
    if (error.empty()) {
        uint64_t deadline = now_ns() + (uint64_t)linger_ms * 1000000ULL;
        for (;;) {
            uint64_t now = now_ns();
            if (now >= deadline) break;
            struct pollfd p = {fd, POLLIN, 0};
            if (poll(&p, 1, (int)((deadline - now) / 1000000ULL) + 1) <= 0) continue;
            ssize_t n = read(fd, rx.data(), rx.size());
            if (n <= 0) continue;
            if (late++ == 0) {
                std::cerr << "[ERROR] message after the last reply:\n";
                hexdump(rx.data(), (size_t)n);
            }
        }
        if (late) {
            error = std::to_string(late) + " message(s) after the last reply";
            std::cerr << "[ERROR] " << error << "\n";
        }
    }
    close(fd);

    double secs = (double)(end_ns - start_ns) / 1e9;
    uint32_t sent;
    {
        std::lock_guard<std::mutex> lk(sh.lock);
        sent = sh.sent - (write_errors ? 1 : 0);
    }

    std::cout << "\n==================== STRESS RESULTS ====================\n";
    std::cout << "Packets Sent   : " << sent << "\n";
    std::cout << "Packets Recv   : " << received << "\n";
    std::cout << "Dropped        : " << dropped << "\n";
    std::cout << "Data Integrity : " << (error.empty() ? "PASS (0 malformed / duplicate / late)" : "FAIL (" + error + ")") << "\n";
    std::cout << "Timeouts       : " << timeouts << "\n";
    std::cout << "Total Duration : " << std::fixed << std::setprecision(3) << secs << " s\n";
    std::cout << "Throughput     : " << std::fixed << std::setprecision(1) << (received / secs) << " msgs/sec\n";
    std::cout << "Bandwidth      : " << std::fixed << std::setprecision(2)
              << ((double)received * size * 2 / 1048576.0 / secs) << " MB/s (bi-directional)\n";
    if (!rtt_us.empty()) {
        std::sort(rtt_us.begin(), rtt_us.end());
        double sum = 0;
        for (double v : rtt_us) sum += v;
        std::cout << "Min Latency    : " << std::setprecision(3) << rtt_us.front() << " us\n";
        std::cout << "Avg Latency    : " << std::setprecision(3) << sum / rtt_us.size() << " us\n";
        std::cout << "Max Latency    : " << std::setprecision(3) << rtt_us.back() << " us\n";
        std::cout << "99th Percentile: " << std::setprecision(3) << rtt_us[(size_t)(rtt_us.size() * 0.99)] << " us\n";
    }
    std::cout << "========================================================\n";

    return (error.empty() && dropped == 0 && write_errors == 0 && received == count) ? 0 : 1;
}
