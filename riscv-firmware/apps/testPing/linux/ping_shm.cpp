/*
 * ping_shm.cpp - Ultra-Low-Latency Shared Memory Ping-Pong Host Benchmark
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
 * Pattern: Direct Shared SRAM MMIO (lite-libmetal style)
 *
 * Measures:
 *  - Round-Trip Time (RTT) per packet with nanosecond precision
 *  - Min, Average, Max latency and Jitter
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
#include <sys/mman.h>
#include <time.h>
#include <getopt.h>

#include "../../common/include/shm_ping_protocol.h"

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -n, --count <num>     Number of pings to send (default: 10000, 0 = continuous)\n"
              << "  -d, --delay <us>      Delay between pings in microseconds (default: 0 = max rate)\n"
              << "  -p, --payload <str>   Custom payload string (max 39 chars)\n"
              << "  -t, --timeout <ms>    Pong timeout in milliseconds (default: 1000)\n"
              << "  -m, --mem-dev <path>  Memory device path (default: /dev/mem)\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char *argv[]) {
    uint32_t count = 10000;
    uint32_t delay_us = 0;
    uint32_t timeout_ms = 1000;
    std::string payload_str = "Ping from Linux ARM64";
    const char *mem_dev = "/dev/mem";

    static struct option long_options[] = {
        {"count",   required_argument, 0, 'n'},
        {"delay",   required_argument, 0, 'd'},
        {"payload", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"mem-dev", required_argument, 0, 'm'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:d:p:t:m:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'n': count = std::stoul(optarg); break;
            case 'd': delay_us = std::stoul(optarg); break;
            case 'p': payload_str = optarg; break;
            case 't': timeout_ms = std::stoul(optarg); break;
            case 'm': mem_dev = optarg; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    std::cout << "================================================================\n";
    std::cout << "  Allwinner T527 Shared Memory Ping-Pong Benchmark (Lite-libmetal)\n";
    std::cout << "  Device: " << mem_dev << " @ SRAM Physical 0x" << std::hex << SHM_PING_SRAM_ADDR << std::dec << "\n";
    std::cout << "  Count : " << count << " iterations | Delay: " << delay_us << " us\n";
    std::cout << "================================================================\n";

    // 1. Open device memory
    int fd = open(mem_dev, O_RDWR | O_SYNC);
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to open " << mem_dev << ": " << strerror(errno) << "\n";
        if (errno == EACCES) {
            std::cerr << "[HINT] Running over /dev/mem requires root privileges (sudo).\n";
        }
        return 1;
    }

    // 2. Map SRAM A2 Shared Memory Region
    size_t page_size = sysconf(_SC_PAGESIZE);
    off_t page_base = SHM_PING_SRAM_ADDR & ~(page_size - 1);
    off_t page_offset = SHM_PING_SRAM_ADDR - page_base;
    size_t map_size = SHM_PING_SRAM_SIZE + page_offset;

    void *mapped = mmap(nullptr, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page_base);
    if (mapped == MAP_FAILED) {
        std::cerr << "[ERROR] mmap failed: " << strerror(errno) << "\n";
        close(fd);
        return 1;
    }

    volatile ShmPingChannel *channel = (volatile ShmPingChannel *)((uint8_t *)mapped + page_offset);

    // 3. Clear doorbells
    channel->host_doorbell = 0;
    channel->riscv_doorbell = 0;
    __sync_synchronize();

    std::cout << "[INFO] Shared memory channel mapped successfully. Starting ping-pong...\n\n";

    std::vector<double> latencies_us;
    latencies_us.reserve(count > 0 ? count : 100000);

    uint64_t bench_start_ns = get_time_ns();
    uint32_t seq = 0;
    uint32_t timeouts = 0;

    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        seq++;

        // Prepare Ping Packet
        channel->ping_pkt.magic = SHM_PING_MAGIC;
        channel->ping_pkt.seq = seq;
        channel->ping_pkt.payload_len = std::min((size_t)39, payload_str.size());
        strncpy((char *)channel->ping_pkt.payload, payload_str.c_str(), sizeof(channel->ping_pkt.payload) - 1);
        channel->ping_pkt.payload[sizeof(channel->ping_pkt.payload) - 1] = '\0';

        uint64_t tx_ns = get_time_ns();
        channel->ping_pkt.host_tx_ts_ns = tx_ns;

        // Trigger Doorbell
        __sync_synchronize();
        channel->host_doorbell = 1;

        // Poll for Pong response
        uint64_t timeout_deadline_ns = tx_ns + (uint64_t)timeout_ms * 1000000ULL;
        bool received = false;

        while (get_time_ns() < timeout_deadline_ns) {
            if (channel->riscv_doorbell == 1) {
                __sync_synchronize();
                uint64_t rx_ns = get_time_ns();

                // Validate pong packet
                if (channel->pong_pkt.magic == SHM_PONG_MAGIC && channel->pong_pkt.seq == seq) {
                    double rtt_us = (double)(rx_ns - tx_ns) / 1000.0;
                    latencies_us.push_back(rtt_us);
                    received = true;

                    // Acknowledge pong
                    channel->riscv_doorbell = 0;
                    __sync_synchronize();
                    break;
                }
            }
        }

        if (!received) {
            timeouts++;
            std::cerr << "[WARN] Ping seq=" << seq << " TIMEOUT (" << timeout_ms << " ms)\n";
            channel->host_doorbell = 0;
            channel->riscv_doorbell = 0;
            __sync_synchronize();
        }

        if (delay_us > 0) {
            usleep(delay_us);
        }

        // Periodic live display
        if (seq % 2000 == 0) {
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

        // Standard Deviation / Jitter calculation
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

    munmap(mapped, map_size);
    close(fd);
    return 0;
}
