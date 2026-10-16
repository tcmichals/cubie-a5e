/*
 * ping_uio.cpp - Event-Driven Lite-libmetal UIO Doorbell Ping-Pong Benchmark (C++)
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E906
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

#include "shm_ping_host.hpp"

// ANSI Escape Colors
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_CYAN    "\033[36m"

/*
 * ARM message box bank (UIO map 0), port 2 = the E906 link, slot 0 = the
 * E906's doorbell (its MsgBox Channel 0). Offsets from sun55i-msgbox.h.
 */
#define MSGBOX_RD_IRQ_EN      0x0220 /* READ_IRQ_ENABLE(2) */
#define MSGBOX_RD_IRQ_STATUS  0x0224 /* READ_IRQ_STATUS(2), write 1 to clear */
#define MSGBOX_MSG_STATUS     0x0260 /* MSG_STATUS(2, 0): entries queued */
#define MSGBOX_MSG_FIFO       0x0270 /* MSG_FIFO(2, 0): read pops one entry */
#define MSGBOX_RD_IRQ_BIT     0x1u   /* RD_IRQ_EN_BIT(0) */

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

    volatile uint8_t *mbox = (volatile uint8_t *)msgbox_map;
    auto mbox_reg = [&](uint32_t off) -> volatile uint32_t & { return *(volatile uint32_t *)(mbox + off); };
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

    // Interrupt-driven wake-up: the E906 rings slot 0 after posting a pong.
    // Empty the FIFO, clear the pending bit, enable the receive interrupt.
    auto drain_doorbells = [&]() -> uint32_t {
        uint32_t n = 0;
        while (mbox_reg(MSGBOX_MSG_STATUS) & 0xF) {
            uint32_t token = mbox_reg(MSGBOX_MSG_FIFO); // the read pops the entry
            (void)token;
            n++;
        }
        mbox_reg(MSGBOX_RD_IRQ_STATUS) = MSGBOX_RD_IRQ_BIT;
        return n;
    };
    auto unmask_irq = [&]() {
        uint32_t enable = 1;
        if (write(uio_fd, &enable, sizeof(enable)) != sizeof(enable)) {
            std::cerr << C_RED << "[ERROR] UIO unmask failed: " << strerror(errno) << C_RESET << "\n";
        }
    };
    // The message box must be clocked and out of reset, or no doorbell can
    // arrive. generic-uio does not switch either on. A clocked block keeps a
    // value written to its IRQ enable register; an unclocked one reads 0.
    mbox_reg(MSGBOX_RD_IRQ_EN) = mbox_reg(MSGBOX_RD_IRQ_EN) | MSGBOX_RD_IRQ_BIT;
    if (!(mbox_reg(MSGBOX_RD_IRQ_EN) & MSGBOX_RD_IRQ_BIT)) {
        std::cerr << C_RED << "[FAIL] message box not clocked or held in reset: IRQ enable at 0x"
                  << std::hex << MSGBOX_RD_IRQ_EN << std::dec << " reads back 0" << C_RESET << "\n";
        return 1;
    }
    drain_doorbells();

    std::cout << C_GREEN << "[INFO] Lite-libmetal UIO mapped. Starting benchmark...\n\n" << C_RESET;

    std::vector<double> latencies_us;
    latencies_us.reserve(count > 0 ? count : 50000);

    uint32_t seq = 0;
    uint32_t sent = 0;
    uint32_t timeouts = 0;
    uint32_t corruptions = 0;
    uint64_t irq_wakeups = 0;
    bool irq_missing = false;
    std::string why;
    if (!shm_ping::sync(channel, timeout_ms, seq, why)) {
        std::cerr << C_RED << "[FAIL] " << why << C_RESET << "\n";
        return 1;
    }
    uint64_t bench_start_ns = shm_ping::now_ns();
    struct epoll_event events[1];

    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        ++seq;
        sent++;

        // Post the ping (the slot is free: pong_seq == seq - 1)
        uint64_t tx_ns = 0, rx_ns = 0;
        channel->ping_pkt.magic = SHM_PING_MAGIC;
        channel->ping_pkt.seq = seq;
        channel->ping_pkt.riscv_cycles = 0;
        channel->ping_pkt.payload_len = sizeof(channel->ping_pkt.payload);
        for (size_t k = 0; k < sizeof(channel->ping_pkt.payload); ++k) {
            channel->ping_pkt.payload[k] = (char)shm_ping::pattern_byte(seq, k);
        }
        tx_ns = shm_ping::now_ns();
        channel->ping_pkt.host_tx_ts_ns = tx_ns;
        __sync_synchronize();
        channel->ping_seq = seq;

        // Sleep until the E906's doorbell interrupt, then check pong_seq.
        // pong_seq is checked again after unmasking so a pong that landed
        // before the interrupt was armed is not slept through.
        uint64_t deadline = tx_ns + (uint64_t)timeout_ms * 1000000ULL;
        bool answered = false;
        bool timed_out = false;
        for (;;) {
            if (channel->pong_seq == seq) { answered = true; break; }
            unmask_irq();
            if (channel->pong_seq == seq) { answered = true; break; }
            uint64_t now = shm_ping::now_ns();
            if (now >= deadline) { timed_out = true; break; }
            int n = epoll_wait(epoll_fd, events, 1, (int)((deadline - now) / 1000000ULL) + 1);
            if (n < 0 && errno != EINTR) {
                std::cerr << C_RED << "[ERROR] epoll_wait: " << strerror(errno) << C_RESET << "\n";
                timed_out = true;
                break;
            }
            if (n > 0) {
                uint32_t irq_count;
                (void)!read(uio_fd, &irq_count, sizeof(irq_count));
                irq_wakeups++;
                drain_doorbells();
            } else if (n == 0 && channel->pong_seq == seq) {
                // The pong arrived but its doorbell interrupt never did.
                std::cerr << C_RED << "[FAIL] seq " << seq << " answered, but no doorbell interrupt in "
                          << timeout_ms << " ms (is the message box clocked?)" << C_RESET << "\n";
                irq_missing = true;
                break;
            }
        }
        if (irq_missing) {
            break;
        }
        rx_ns = shm_ping::now_ns();

        if (timed_out) {
            timeouts++;
            std::cerr << C_RED << "[FAIL] no pong for seq " << seq << " (" << timeout_ms << " ms)" << C_RESET << "\n";
            break;
        }
        (void)answered;
        __sync_synchronize();

        // Check every byte of the pong
        uint32_t magic = channel->pong_pkt.magic;
        bool ok = magic == SHM_PONG_MAGIC && channel->pong_pkt.seq == seq &&
                  channel->pong_pkt.host_tx_ts_ns == tx_ns &&
                  channel->pong_pkt.payload[0] == 'P' && channel->pong_pkt.payload[1] == 'O' &&
                  channel->pong_pkt.payload[2] == 'N' && channel->pong_pkt.payload[3] == 'G';
        for (size_t k = 4; ok && k < sizeof(channel->pong_pkt.payload); ++k) {
            ok = (uint8_t)channel->pong_pkt.payload[k] == shm_ping::pattern_byte(seq, k);
        }
        if (!ok) {
            corruptions++;
            std::cerr << C_RED << "[ERROR] malformed pong for seq " << seq << ": magic=0x" << std::hex << magic
                      << std::dec << " seq=" << channel->pong_pkt.seq
                      << (magic == SHM_ERROR_MAGIC ? " (E906 rejected the ping)" : "") << C_RESET << "\n";
            break;
        }
        latencies_us.push_back((double)(rx_ns - tx_ns) / 1000.0);

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
    std::cout << "  Total Pings Sent    : " << sent << "\n";
    std::cout << "  Pongs Received      : " << num_received << " (" << std::fixed << std::setprecision(2)
              << (100.0 * num_received / sent) << "%)\n";
    std::cout << "  IRQ Wake-ups        : " << irq_wakeups << "\n";
    std::cout << "  Data Integrity      : " << (corruptions == 0 ? "PASS (0 corrupted / mismatched packets)" : "FAIL") << "\n";
    if (corruptions > 0) {
        std::cout << "  Corrupted Packets   : " << corruptions << "\n";
    }
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

    return (num_received != sent || timeouts > 0 || corruptions > 0 || irq_missing) ? 1 : 0;
}
