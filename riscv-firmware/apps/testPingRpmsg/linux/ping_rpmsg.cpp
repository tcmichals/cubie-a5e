/*
 * ping_rpmsg.cpp - Standard Linux RPMsg Ping-Pong Host Benchmark
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
 * Protocol: Standard Linux virtio_rpmsg_bus character interface (/dev/rpmsg0 or /dev/rpmsg_ctrl0)
 *
 * Measures:
 *  - Round-Trip Time (RTT) per packet with nanosecond precision
 *  - Min, Average, Max latency and Jitter (Standard Deviation)
 *  - Latency Histogram / Percentiles (p50, p90, p99, p99.9)
 *  - Transmission throughput (msgs/sec)
 */

#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <time.h>
#include <getopt.h>

#if __has_include(<linux/rpmsg.h>)
#include <linux/rpmsg.h>
#else
struct rpmsg_endpoint_info {
    char name[32];
    uint32_t src;
    uint32_t dst;
};
#define RPMSG_CREATE_EPT_IOCTL  _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)
#endif

#include "../../common/include/resource_table.h"

struct RpmsgPingPayload {
    uint32_t seq;
    uint64_t host_tx_ts_ns;
    char     text[48];
} __attribute__((packed));

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -d, --dev <path>      RPMsg character device (default: /dev/rpmsg0 or /dev/rpmsg_ctrl0)\n"
              << "  -n, --count <num>     Number of pings to send (default: 1000, 0 = continuous)\n"
              << "  -s, --sleep <us>      Sleep between pings in microseconds (default: 1000)\n"
              << "  -p, --payload <str>   Custom payload string (max 47 chars)\n"
              << "  -t, --timeout <ms>    Pong timeout in milliseconds (default: 1000)\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char *argv[]) {
    uint32_t count = 1000;
    uint32_t sleep_us = 1000;
    uint32_t timeout_ms = 1000;
    std::string payload_str = "Ping from Linux RPMsg Host";
    std::string dev_path = "";

    static struct option long_options[] = {
        {"dev",     required_argument, 0, 'd'},
        {"count",   required_argument, 0, 'n'},
        {"sleep",   required_argument, 0, 's'},
        {"payload", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "d:n:s:p:t:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'd': dev_path = optarg; break;
            case 'n': count = std::stoul(optarg); break;
            case 's': sleep_us = std::stoul(optarg); break;
            case 'p': payload_str = optarg; break;
            case 't': timeout_ms = std::stoul(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    std::cout << "================================================================\n";
    std::cout << "  Allwinner T527 Linux VirtIO RPMsg Ping-Pong Benchmark        \n";
    std::cout << "  Protocol: Linux kernel virtio_rpmsg_bus                      \n";
    std::cout << "  Channel : rpmsg-ping-channel (Endpoint Addr: 1024)           \n";
    std::cout << "================================================================\n";

    // Auto-discover RPMsg device if not provided
    if (dev_path.empty()) {
        const char *candidate_devs[] = {
            "/dev/rpmsg0",
            "/dev/rpmsg_ctrl0",
            "/dev/ttyRPMSG0"
        };
        for (const char *cand : candidate_devs) {
            if (access(cand, F_OK) == 0) {
                dev_path = cand;
                break;
            }
        }
        if (dev_path.empty()) {
            dev_path = "/dev/rpmsg0"; // Default fallback
        }
    }

    std::cout << "[INFO] Target device: " << dev_path << "\n";

    int fd = -1;

    // Check if device is rpmsg_ctrlX and create an endpoint
    if (dev_path.find("rpmsg_ctrl") != std::string::npos) {
        std::cout << "[INFO] Detected RPMsg control device. Creating endpoint 'rpmsg-ping-channel'...\n";
        int ctrl_fd = open(dev_path.c_str(), O_RDWR);
        if (ctrl_fd < 0) {
            std::cerr << "[ERROR] Failed to open " << dev_path << ": " << strerror(errno) << "\n";
            return 1;
        }

        struct rpmsg_endpoint_info ept_info;
        memset(&ept_info, 0, sizeof(ept_info));
        strncpy(ept_info.name, "rpmsg-ping-channel", sizeof(ept_info.name) - 1);
        ept_info.src = RPMSG_PING_EPT_ADDR;
        ept_info.dst = RPMSG_PING_EPT_ADDR;

        if (ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &ept_info) < 0) {
            std::cerr << "[ERROR] RPMSG_CREATE_EPT_IOCTL failed: " << strerror(errno) << "\n";
            close(ctrl_fd);
            return 1;
        }
        close(ctrl_fd);
        dev_path = "/dev/rpmsg0";
        usleep(100000); // Allow udev to instantiate /dev/rpmsg0
    }

    fd = open(dev_path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to open " << dev_path << ": " << strerror(errno) << "\n";
        std::cerr << "[HINT] Ensure Linux remoteproc firmware is booted and rpmsg_char driver is loaded.\n";
        std::cerr << "[HINT] modprobe rpmsg_char && modprobe virtio_rpmsg_bus\n";
        return 1;
    }

    std::cout << "[INFO] Successfully opened " << dev_path << ". Starting ping-pong loop...\n\n";

    std::vector<double> latencies_us;
    latencies_us.reserve(count > 0 ? count : 10000);

    uint64_t bench_start_ns = get_time_ns();
    uint32_t seq = 0;
    uint32_t timeouts = 0;

    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        seq++;

        RpmsgPingPayload tx_payload;
        tx_payload.seq = seq;
        strncpy(tx_payload.text, payload_str.c_str(), sizeof(tx_payload.text) - 1);
        tx_payload.text[sizeof(tx_payload.text) - 1] = '\0';

        uint64_t tx_ns = get_time_ns();
        tx_payload.host_tx_ts_ns = tx_ns;

        // Send RPMsg Ping
        ssize_t bytes_written = write(fd, &tx_payload, sizeof(tx_payload));
        if (bytes_written < 0) {
            std::cerr << "[ERROR] write() failed on seq=" << seq << ": " << strerror(errno) << "\n";
            break;
        }

        // Wait for Pong Response using poll()
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, timeout_ms);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            RpmsgPingPayload rx_payload;
            ssize_t bytes_read = read(fd, &rx_payload, sizeof(rx_payload));
            uint64_t rx_ns = get_time_ns();

            if (bytes_read > 0) {
                double rtt_us = (double)(rx_ns - tx_ns) / 1000.0;
                latencies_us.push_back(rtt_us);
            } else {
                timeouts++;
                std::cerr << "[WARN] Ping seq=" << seq << " read() error: " << strerror(errno) << "\n";
            }
        } else if (ret == 0) {
            timeouts++;
            std::cerr << "[WARN] Ping seq=" << seq << " TIMEOUT (" << timeout_ms << " ms)\n";
        } else {
            std::cerr << "[ERROR] poll() error on seq=" << seq << ": " << strerror(errno) << "\n";
            break;
        }

        if (sleep_us > 0) {
            usleep(sleep_us);
        }

        if (seq % 200 == 0) {
            std::cout << "  Progress: " << seq << " packets sent, last RTT: "
                      << std::fixed << std::setprecision(2) << latencies_us.back() << " us\r" << std::flush;
        }
    }

    uint64_t bench_end_ns = get_time_ns();
    double total_time_sec = (double)(bench_end_ns - bench_start_ns) / 1e9;

    std::cout << "\n\n==================== BENCHMARK RESULTS ====================\n";
    std::cout << "Packets Sent   : " << seq << "\n";
    std::cout << "Packets Recv   : " << latencies_us.size() << " ("
              << (seq > 0 ? (double)latencies_us.size() * 100.0 / seq : 0.0) << "% success)\n";
    std::cout << "Timeouts       : " << timeouts << "\n";
    std::cout << "Total Duration : " << std::fixed << std::setprecision(3) << total_time_sec << " s\n";
    std::cout << "Throughput     : " << std::fixed << std::setprecision(1)
              << ((double)latencies_us.size() / total_time_sec) << " msgs/sec\n";

    if (!latencies_us.empty()) {
        std::sort(latencies_us.begin(), latencies_us.end());

        double min_lat = latencies_us.front();
        double max_lat = latencies_us.back();
        double sum = 0;
        for (double l : latencies_us) sum += l;
        double avg_lat = sum / latencies_us.size();

        double variance = 0;
        for (double l : latencies_us) {
            variance += (l - avg_lat) * (l - avg_lat);
        }
        double jitter_stddev = std::sqrt(variance / latencies_us.size());

        size_t p50_idx = (size_t)(latencies_us.size() * 0.50);
        size_t p90_idx = (size_t)(latencies_us.size() * 0.90);
        size_t p99_idx = (size_t)(latencies_us.size() * 0.99);
        size_t p999_idx = (size_t)(latencies_us.size() * 0.999);

        std::cout << "\n------------------- Latency Statistics -------------------\n";
        std::cout << "Min Latency    : " << std::fixed << std::setprecision(3) << min_lat << " us\n";
        std::cout << "Avg Latency    : " << std::fixed << std::setprecision(3) << avg_lat << " us\n";
        std::cout << "Max Latency    : " << std::fixed << std::setprecision(3) << max_lat << " us\n";
        std::cout << "Jitter (StdDev): " << std::fixed << std::setprecision(3) << jitter_stddev << " us\n";
        std::cout << "\n---------------------- Percentiles ----------------------\n";
        std::cout << "50th Percentile: " << std::fixed << std::setprecision(3) << latencies_us[p50_idx] << " us\n";
        std::cout << "90th Percentile: " << std::fixed << std::setprecision(3) << latencies_us[p90_idx] << " us\n";
        std::cout << "99th Percentile: " << std::fixed << std::setprecision(3) << latencies_us[p99_idx] << " us\n";
        std::cout << "99.9th Perc.   : " << std::fixed << std::setprecision(3) << latencies_us[p999_idx] << " us\n";
        std::cout << "==========================================================\n";
    }

    close(fd);
    return 0;
}
