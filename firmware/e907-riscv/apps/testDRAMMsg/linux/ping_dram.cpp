/*
 * ping_dram.cpp - Hybrid SRAM SPSC / DDR DRAM Payload Linux Benchmark
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E907
 * Pattern: SPSC Queue Descriptors in SRAM A2 (0x00040000) + Payload Buffers in DDR (0x48100000)
 *
 * Measures:
 *  - Round-Trip Time (RTT) per message with nanosecond precision
 *  - Min, Average, Max latency and Jitter (Standard Deviation)
 *  - Percentiles (50th, 90th, 99th, 99.9th)
 *  - Message Throughput (msgs/sec) & Data Bandwidth (MB/sec)
 *  - Direct comparison with on-chip SRAM access
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

#include "../../common/include/dram_spsc_protocol.h"

static inline uint64_t get_time_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void print_usage(const char *prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "Options:\n"
              << "  -n, --count <num>     Number of messages to send (default: 10000, 0 = continuous)\n"
              << "  -s, --size <bytes>    Payload size in bytes (default: 256, max: 4096)\n"
              << "  -d, --delay <us>      Delay between messages in microseconds (default: 0 = max rate)\n"
              << "  -t, --timeout <ms>    Response timeout in milliseconds (default: 1000)\n"
              << "  -m, --mem-dev <path>  Memory device path (default: /dev/mem)\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char *argv[]) {
    uint32_t count = 10000;
    uint32_t payload_size = 256;
    uint32_t delay_us = 0;
    uint32_t timeout_ms = 1000;
    const char *mem_dev = "/dev/mem";

    static struct option long_options[] = {
        {"count",   required_argument, 0, 'n'},
        {"size",    required_argument, 0, 's'},
        {"delay",   required_argument, 0, 'd'},
        {"timeout", required_argument, 0, 't'},
        {"mem-dev", required_argument, 0, 'm'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:s:d:t:m:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'n': count = std::stoul(optarg); break;
            case 's': payload_size = std::stoul(optarg); break;
            case 'd': delay_us = std::stoul(optarg); break;
            case 't': timeout_ms = std::stoul(optarg); break;
            case 'm': mem_dev = optarg; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    if (payload_size > DRAM_SPSC_MAX_BUF_LEN) {
        std::cerr << "[ERROR] Maximum payload size is " << DRAM_SPSC_MAX_BUF_LEN << " bytes.\n";
        return 1;
    }
    if (payload_size < 4) {
        payload_size = 4;
    }

    std::cout << "================================================================\n";
    std::cout << "  Allwinner T527 Hybrid SRAM SPSC / DDR DRAM Benchmark          \n";
    std::cout << "  Control  : SRAM A2 Physical 0x" << std::hex << DRAM_SPSC_SRAM_ADDR << std::dec << "\n";
    std::cout << "  Payloads : DDR DRAM Physical 0x" << std::hex << DRAM_SPSC_DRAM_ADDR << std::dec << " (1 MB Pool)\n";
    std::cout << "  Payload  : " << payload_size << " bytes/msg | Count: " << count << " iterations\n";
    std::cout << "================================================================\n";

    // 1. Open /dev/mem
    int fd = open(mem_dev, O_RDWR | O_SYNC);
    if (fd < 0) {
        std::cerr << "[ERROR] Failed to open " << mem_dev << ": " << strerror(errno) << "\n";
        if (errno == EACCES) {
            std::cerr << "[HINT] Accessing physical memory via /dev/mem requires root privileges (sudo).\n";
        }
        return 1;
    }

    size_t page_size = sysconf(_SC_PAGESIZE);

    // 2. Map SRAM A2 Control Block (0x00040000)
    off_t sram_page_base = DRAM_SPSC_SRAM_ADDR & ~(page_size - 1);
    off_t sram_page_offset = DRAM_SPSC_SRAM_ADDR - sram_page_base;
    size_t sram_map_size = DRAM_SPSC_SRAM_SIZE + sram_page_offset;

    void *sram_mapped = mmap(nullptr, sram_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, sram_page_base);
    if (sram_mapped == MAP_FAILED) {
        std::cerr << "[ERROR] mmap SRAM A2 failed: " << strerror(errno) << "\n";
        close(fd);
        return 1;
    }
    volatile DramSpscControlBlock *ctrl = (volatile DramSpscControlBlock *)((uint8_t *)sram_mapped + sram_page_offset);

    // 3. Map DDR DRAM Payload Buffer Pool (0x48100000)
    off_t dram_page_base = DRAM_SPSC_DRAM_ADDR & ~(page_size - 1);
    off_t dram_page_offset = DRAM_SPSC_DRAM_ADDR - dram_page_base;
    size_t dram_map_size = DRAM_SPSC_DRAM_SIZE + dram_page_offset;

    void *dram_mapped = mmap(nullptr, dram_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, dram_page_base);
    if (dram_mapped == MAP_FAILED) {
        std::cerr << "[ERROR] mmap DDR DRAM failed: " << strerror(errno) << "\n";
        munmap(sram_mapped, sram_map_size);
        close(fd);
        return 1;
    }
    volatile uint8_t *dram_pool = (volatile uint8_t *)dram_mapped + dram_page_offset;

    // 4. Verify initialization
    if (ctrl->magic != DRAM_SPSC_MAGIC_INIT) {
        std::cerr << "[WARN] SPSC Magic 0x" << std::hex << ctrl->magic
                  << " != Expected 0x" << DRAM_SPSC_MAGIC_INIT << std::dec << "\n";
        std::cerr << "[WARN] Remote processor firmware (testDRAMMsg.elf) may not be running yet.\n";
    }

    std::cout << "[INFO] Control & DRAM regions mapped. Starting SPSC benchmark...\n\n";

    std::vector<double> latencies_us;
    latencies_us.reserve(count > 0 ? count : 100000);

    // Pre-generate test pattern
    std::vector<uint8_t> tx_pattern(payload_size);
    for (size_t i = 0; i < payload_size; ++i) {
        tx_pattern[i] = static_cast<uint8_t>((i & 0xFF) ^ 0xA5);
    }

    uint64_t bench_start_ns = get_time_ns();
    uint32_t seq = 0;
    uint32_t timeouts = 0;
    uint64_t total_payload_bytes = 0;

    for (uint32_t i = 0; count == 0 || i < count; ++i) {
        seq++;

        uint32_t host_head = ctrl->host_head;
        uint32_t tx_slot = host_head % DRAM_SPSC_RING_ENTRIES;

        volatile DramSpscDesc *tx_desc = &ctrl->tx_ring[tx_slot];
        uint32_t tx_dram_offset = tx_desc->dram_buf_offset;

        // Write payload to DDR DRAM
        volatile uint8_t *tx_buf = dram_pool + tx_dram_offset;
        for (size_t b = 0; b < payload_size; ++b) {
            tx_buf[b] = tx_pattern[b];
        }

        uint64_t tx_ns = get_time_ns();
        tx_desc->seq = seq;
        tx_desc->payload_len = payload_size;
        tx_desc->host_tx_ts_ns = tx_ns;
        tx_desc->flags = 1; // READY

        // Commit memory writes to DRAM & SRAM
        __sync_synchronize();

        // Advance host head in SRAM and assert doorbell
        ctrl->host_head = host_head + 1;
        ctrl->host_doorbell = 1;

        // Wait for RISC-V response
        uint64_t timeout_deadline_ns = tx_ns + (uint64_t)timeout_ms * 1000000ULL;
        bool received = false;

        uint32_t rx_slot = ctrl->host_tail % DRAM_SPSC_RING_ENTRIES;
        volatile DramSpscDesc *rx_desc = &ctrl->rx_ring[rx_slot];

        while (get_time_ns() < timeout_deadline_ns) {
            if (ctrl->riscv_doorbell == 1 || rx_desc->flags == 2) {
                __sync_synchronize();
                uint64_t rx_ns = get_time_ns();

                if (rx_desc->seq == seq) {
                    double rtt_us = (double)(rx_ns - tx_ns) / 1000.0;
                    latencies_us.push_back(rtt_us);
                    total_payload_bytes += payload_size * 2; // Bi-directional
                    received = true;

                    // Advance host tail and clear doorbell
                    rx_desc->flags = 0;
                    ctrl->host_tail = ctrl->host_tail + 1;
                    ctrl->riscv_doorbell = 0;
                    __sync_synchronize();
                    break;
                }
            }
        }

        if (!received) {
            timeouts++;
            std::cerr << "[WARN] DRAM Msg seq=" << seq << " TIMEOUT (" << timeout_ms << " ms)\n";
            ctrl->host_doorbell = 0;
            ctrl->riscv_doorbell = 0;
            __sync_synchronize();
        }

        if (delay_us > 0) {
            usleep(delay_us);
        }

        if (seq % 2000 == 0) {
            std::cout << "  Progress: " << seq << " msgs sent, last RTT: "
                      << std::fixed << std::setprecision(2) << latencies_us.back() << " us\r" << std::flush;
        }
    }

    uint64_t bench_end_ns = get_time_ns();
    double total_time_sec = (double)(bench_end_ns - bench_start_ns) / 1e9;

    std::cout << "\n\n==================== BENCHMARK RESULTS ====================\n";
    std::cout << "Messages Sent  : " << seq << "\n";
    std::cout << "Messages Recv  : " << latencies_us.size() << " ("
              << (seq > 0 ? (double)latencies_us.size() * 100.0 / seq : 0.0) << "% success)\n";
    std::cout << "Timeouts       : " << timeouts << "\n";
    std::cout << "Total Duration : " << std::fixed << std::setprecision(3) << total_time_sec << " s\n";
    std::cout << "Throughput     : " << std::fixed << std::setprecision(1)
              << ((double)latencies_us.size() / total_time_sec) << " msgs/sec\n";
    std::cout << "Bandwidth      : " << std::fixed << std::setprecision(2)
              << ((double)total_payload_bytes / (1024.0 * 1024.0 * total_time_sec)) << " MB/sec (Bidirectional)\n";

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

    munmap(dram_mapped, dram_map_size);
    munmap(sram_mapped, sram_map_size);
    close(fd);
    return 0;
}
