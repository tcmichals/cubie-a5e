/*
 * ping_uio.cpp - Event-Driven Lite-libmetal UIO Doorbell Ping-Pong Benchmark (C++)
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
 * Pattern: Direct Shared SRAM + UIO Hardware Mailbox Doorbell ISR via epoll
 *
 * Features:
 *  - Event-driven I/O using epoll with 0% idle CPU utilization
 *  - Userspace memory-mapped access to Mailbox registers (map0) and SRAM C (map1)
 *  - Precise Round-Trip Time (RTT) measurements with nanosecond timestamps
 *  - Statistics: Min, Average, Max, Jitter, Percentiles (p50, p90, p99, p99.9), Throughput
 *  - Zero /dev/mem or root privilege requirement when /dev/uio0 permissions are granted
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
#include <sys/epoll.h>
#include <time.h>
#include <getopt.h>

#include "include/shm_ping_protocol.h"

// ANSI Escape Colors
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_CYAN    "\033[36m"

// Hardware Mailbox Channel 1 Tx FIFO Register Offset (Host -> RISC-V)
#define MSGBOX_TX_FIFO_CH1_OFFSET 0x0184

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -u, --uio-dev <path>  UIO device node (default: /dev/uio0)\n"
              << "  -n, --count <num>     Number of pings to send (default: 10000, 0 = continuous)\n"
              << "  -d, --delay <us>      Delay between pings in microseconds (default: 0 = max rate)\n"
              << "  -p, --payload <str>   Custom payload string (max 39 chars)\n"
              << "  -t, --timeout <ms>    Pong timeout in milliseconds (default: 1000)\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char *argv[]) {
    std::string uio_dev = "/dev/uio0";
    uint32_t count = 10000;
    uint32_t delay_us = 0;
    uint32_t timeout_ms = 1000;
    std::string payload_str = "Ping from Linux C++ UIO";

    static struct option long_options[] = {
        {"uio-dev", required_argument, 0, 'u'},
        {"count",   required_argument, 0, 'n'},
        {"delay",   required_argument, 0, 'd'},
        {"payload", required_argument, 0, 'p'},
        {"timeout", required_argument, 0, 't'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "u:n:d:p:t:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'u': uio_dev = optarg; break;
            case 'n': count = std::stoul(optarg); break;
            case 'd': delay_us = std::stoul(optarg); break;
            case 'p': payload_str = optarg; break;
            case 't': timeout_ms = std::stoul(optarg); break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    std::cout << C_CYAN << C_BOLD << "================================================================\n" << C_RESET;
    std::cout << C_CYAN << C_BOLD << "  Allwinner T527 Lite-libmetal UIO Ping-Pong Benchmark (C++)     \n" << C_RESET;
    std::cout << C_CYAN << "  Device  : " << uio_dev << " (Mailbox Doorbell + Dedicated MCU SRAM C)\n" << C_RESET;
    std::cout << C_CYAN << "  Pattern : Event-driven epoll ISR (0% Idle CPU Burn)           \n" << C_RESET;
    std::cout << C_CYAN << "  Count   : " << count << " iterations | Delay: " << delay_us << " us\n" << C_RESET;
    std::cout << C_CYAN << C_BOLD << "================================================================\n\n" << C_RESET;

    // 1. Open UIO Device Node
    int uio_fd = open(uio_dev.c_str(), O_RDWR | O_SYNC);
    if (uio_fd < 0) {
        std::cerr << C_RED << "[ERROR] Failed to open " << uio_dev << ": " << strerror(errno) << C_RESET << "\n";
        std::cerr << C_YELLOW << "[HINT] Ensure 'cubie-a5e-uio.dtbo' is applied via config.txt and CONFIG_UIO is active.\n" << C_RESET;
        return 1;
    }

    size_t page_size = sysconf(_SC_PAGESIZE);

    // 2. Map UIO Regions directly through /dev/uio0 (NO /dev/mem required!)
    // Map 0 (offset 0 * page_size): Mailbox MMIO registers (4 KB)
    void *msgbox_map = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 0);
    if (msgbox_map == MAP_FAILED) {
        std::cerr << C_RED << "[ERROR] Failed to mmap Map 0 (Mailbox): " << strerror(errno) << C_RESET << "\n";
        close(uio_fd);
        return 1;
    }

    // Map 1 (offset 1 * page_size): Dedicated MCU SRAM C (4 KB)
    void *sram_map = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, uio_fd, 1 * page_size);
    if (sram_map == MAP_FAILED) {
        std::cerr << C_RED << "[ERROR] Failed to mmap Map 1 (SRAM C): " << strerror(errno) << C_RESET << "\n";
        munmap(msgbox_map, page_size);
        close(uio_fd);
        return 1;
    }

    volatile uint32_t *msgbox_tx_fifo = (volatile uint32_t *)((uint8_t *)msgbox_map + MSGBOX_TX_FIFO_CH1_OFFSET);
    volatile ShmPingChannel *channel = (volatile ShmPingChannel *)sram_map;

    // 3. Setup epoll for asynchronous interrupt delivery
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        std::cerr << C_RED << "[ERROR] epoll_create1 failed: " << strerror(errno) << C_RESET << "\n";
        munmap(sram_map, page_size);
        munmap(msgbox_map, page_size);
        close(uio_fd);
        return 1;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = uio_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uio_fd, &ev) < 0) {
        std::cerr << C_RED << "[ERROR] epoll_ctl failed: " << strerror(errno) << C_RESET << "\n";
        close(epoll_fd);
        munmap(sram_map, page_size);
        munmap(msgbox_map, page_size);
        close(uio_fd);
        return 1;
    }

    // Clear shared memory doorbells
    channel->host_doorbell = 0;
    channel->riscv_doorbell = 0;
    __sync_synchronize();

    // Helper lambda: unmask/re-enable UIO interrupt
    auto reenable_irq = [&]() {
        uint32_t enable = 1;
        ssize_t s = write(uio_fd, &enable, sizeof(enable));
        (void)s;
    };

    // Helper lambda: ring hardware doorbell
    auto ring_doorbell = [&](uint32_t token) {
        __sync_synchronize();
        channel->host_doorbell = 1;
        __sync_synchronize();
        *msgbox_tx_fifo = token;
    };

    // Initial interrupt unmask
    reenable_irq();

    std::cout << C_GREEN << "[INFO] Lite-libmetal UIO mapped. Starting benchmark...\n\n" << C_RESET;

    std::vector<double> latencies_us;
    latencies_us.reserve(count > 0 ? count : 50000);

    uint64_t bench_start_ns = get_time_ns();
    uint32_t seq = 0;
    uint32_t timeouts = 0;
    struct epoll_event events[1];

    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        seq++;

        // Prepare Ping Packet in SRAM C
        channel->ping_pkt.magic = SHM_PING_MAGIC;
        channel->ping_pkt.seq = seq;
        channel->ping_pkt.payload_len = std::min((size_t)39, payload_str.size());
        strncpy((char *)channel->ping_pkt.payload, payload_str.c_str(), sizeof(channel->ping_pkt.payload) - 1);
        channel->ping_pkt.payload[sizeof(channel->ping_pkt.payload) - 1] = '\0';

        uint64_t tx_ns = get_time_ns();
        channel->ping_pkt.host_tx_ts_ns = tx_ns;

        // Trigger Hardware Mailbox Doorbell
        ring_doorbell(seq);

        // Block on epoll waiting for XuanTie E907 hardware IRQ (0% CPU idle burn)
        int nfds = epoll_wait(epoll_fd, events, 1, timeout_ms);
        uint64_t rx_ns = get_time_ns();

        if (nfds <= 0) {
            timeouts++;
            if (timeouts <= 5) {
                std::cerr << C_YELLOW << "[WARN] Ping seq " << seq << " timed out waiting for UIO doorbell!\n" << C_RESET;
            }
            continue;
        }

        if (events[0].data.fd == uio_fd && (events[0].events & EPOLLIN)) {
            // Read 4-byte cumulative interrupt counter from UIO
            uint32_t irq_count = 0;
            ssize_t bytes_read = read(uio_fd, &irq_count, sizeof(irq_count));
            (void)bytes_read;

            __sync_synchronize();

            // Validate Pong Packet
            if (channel->pong_pkt.magic == SHM_PONG_MAGIC && channel->pong_pkt.seq == seq) {
                double rtt_us = (double)(rx_ns - tx_ns) / 1000.0;
                latencies_us.push_back(rtt_us);
            } else {
                std::cerr << C_RED << "[ERROR] Malformed pong: magic=0x" << std::hex
                          << channel->pong_pkt.magic << ", seq=" << std::dec << channel->pong_pkt.seq << C_RESET << "\n";
            }

            // Re-enable UIO interrupt for next transaction
            reenable_irq();
        }

        if (delay_us > 0) {
            usleep(delay_us);
        }
    }

    uint64_t total_time_ns = get_time_ns() - bench_start_ns;
    double total_time_sec = (double)total_time_ns / 1000000000.0;

    // Clean up
    close(epoll_fd);
    munmap(sram_map, page_size);
    munmap(msgbox_map, page_size);
    close(uio_fd);

    // Compute and print statistics
    size_t num_received = latencies_us.size();
    if (num_received == 0) {
        std::cerr << C_RED << "[ERROR] No pongs received. Remote processor may not be running.\n" << C_RESET;
        return 1;
    }

    std::sort(latencies_us.begin(), latencies_us.end());
    double min_lat = latencies_us.front();
    double max_lat = latencies_us.back();
    double sum_lat = 0.0;
    for (double lat : latencies_us) sum_lat += lat;
    double avg_lat = sum_lat / num_received;

    double variance = 0.0;
    for (double lat : latencies_us) {
        variance += (lat - avg_lat) * (lat - avg_lat);
    }
    double std_dev = std::sqrt(variance / num_received);

    auto percentile = [&](double p) -> double {
        size_t idx = static_cast<size_t>(num_received * (p / 100.0));
        return latencies_us[std::min(idx, num_received - 1)];
    };

    double p50  = percentile(50.0);
    double p90  = percentile(90.0);
    double p99  = percentile(99.0);
    double p999 = percentile(99.9);

    double msg_rate = (double)num_received / total_time_sec;

    std::cout << "\n" << C_GREEN << C_BOLD << "====================== BENCHMARK RESULTS ======================\n" << C_RESET;
    std::cout << "  Total Pings Sent    : " << seq << "\n";
    std::cout << "  Pongs Received      : " << num_received << " (" << std::fixed << std::setprecision(2)
              << (100.0 * num_received / seq) << "%)\n";
    std::cout << "  Timeouts            : " << timeouts << "\n";
    std::cout << "  Total Duration      : " << std::setprecision(4) << total_time_sec << " s\n";
    std::cout << "  Throughput Rate     : " << C_BOLD << std::setprecision(1) << msg_rate << " msgs/sec\n" << C_RESET;
    std::cout << "----------------------------------------------------------------\n";
    std::cout << "  Min RTT Latency     : " << C_BOLD << std::setprecision(3) << min_lat << " us\n" << C_RESET;
    std::cout << "  Avg RTT Latency     : " << C_BOLD << std::setprecision(3) << avg_lat << " us\n" << C_RESET;
    std::cout << "  Max RTT Latency     : " << C_BOLD << std::setprecision(3) << max_lat << " us\n" << C_RESET;
    std::cout << "  Jitter (StdDev)     : " << std::setprecision(3) << std_dev << " us\n";
    std::cout << "----------------------------------------------------------------\n";
    std::cout << "  Percentile 50% (p50): " << std::setprecision(3) << p50 << " us\n";
    std::cout << "  Percentile 90% (p90): " << std::setprecision(3) << p90 << " us\n";
    std::cout << "  Percentile 99% (p99): " << std::setprecision(3) << p99 << " us\n";
    std::cout << "  Percentile 99.9%    : " << std::setprecision(3) << p999 << " us\n";
    std::cout << C_GREEN << C_BOLD << "================================================================\n\n" << C_RESET;

    return 0;
}
