/*
 * ping_dram.cpp - Hybrid SRAM SPSC / DDR DRAM Payload Linux Benchmark
 *
 * Target: Linux Host (ARM64 / x86_64) communicating with Allwinner T527 XuanTie E906
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
#include <string>

#include "dram_spsc_protocol.h"

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
              << "  -w, --window <num>    Messages in flight at once, 1..16 (default: 1)\n"
              << "  -d, --delay <us>      Delay between messages in microseconds (default: 0 = max rate)\n"
              << "  -t, --timeout <ms>    Response timeout in milliseconds (default: 1000)\n"
              << "  -m, --mem-dev <path>  Memory device path (default: /dev/mem)\n"
              << "  -h, --help            Show this help message\n";
}

int main(int argc, char *argv[]) {
    uint32_t count = 10000;
    uint32_t payload_size = 256;
    uint32_t delay_us = 0;
    uint32_t window = 1;
    uint32_t timeout_ms = 1000;
    const char *mem_dev = "/dev/mem";

    static struct option long_options[] = {
        {"count",   required_argument, 0, 'n'},
        {"size",    required_argument, 0, 's'},
        {"delay",   required_argument, 0, 'd'},
        {"window",  required_argument, 0, 'w'},
        {"timeout", required_argument, 0, 't'},
        {"mem-dev", required_argument, 0, 'm'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "n:s:d:w:t:m:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'n': count = std::stoul(optarg); break;
            case 's': payload_size = std::stoul(optarg); break;
            case 'd': delay_us = std::stoul(optarg); break;
            case 'w': window = std::stoul(optarg); break;
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
    if (window < 1) window = 1;
    if (window > DRAM_SPSC_RING_ENTRIES) window = DRAM_SPSC_RING_ENTRIES;
    if (count == 0) count = UINT32_MAX;

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
        std::cerr << "[FAIL] SPSC magic 0x" << std::hex << ctrl->magic
                  << " != 0x" << DRAM_SPSC_MAGIC_INIT << std::dec << ": is testDRAMMsg.elf running?\n";
        return 1;
    }

    std::cout << "[INFO] Control & DRAM regions mapped. Starting SPSC benchmark...\n\n";

    std::vector<double> latencies_us;
    latencies_us.reserve(count < 1000000 ? count : 1000000);

    auto pattern = [](uint32_t seq, size_t k) -> uint8_t { return (uint8_t)(seq * 31u + k * 7u); };

    // Continue from the current ring state using standard ipc Producer & Consumer
    ipc::Producer<DramTxRing> tx_producer(&ctrl->tx_ring);
    ipc::Consumer<DramRxRing> rx_consumer(&ctrl->rx_ring);

    {
        uint64_t deadline = get_time_ns() + (uint64_t)timeout_ms * 1000000ULL;
        while (tx_producer.in_flight() != 0 || rx_consumer.peek() != nullptr) {
            if (get_time_ns() >= deadline) {
                std::cerr << "[FAIL] rings not idle at start (tx in flight: "
                          << tx_producer.in_flight() << ")\n";
                return 1;
            }
        }
    }

    // Send time per message, keyed by seq: at most RING_ENTRIES are in flight
    // and their sequence numbers are consecutive. (Ring slots are not a key:
    // the ring index carries on from the previous run.)
    std::vector<uint64_t> tx_ts(DRAM_SPSC_RING_ENTRIES);
    uint64_t bench_start_ns = get_time_ns();
    uint32_t sent = 0, received = 0, next_seq = 1, expect_seq = 1;
    uint32_t timeouts = 0, corruptions = 0, full_waits = 0;
    uint64_t total_payload_bytes = 0;
    uint64_t last_progress_ns = bench_start_ns;
    std::string why;

    while (received < count && why.empty()) {
        // Post while there is room in the window and in the ping ring
        while (sent < count && sent - received < window) {
            volatile DramSpscDesc *d = tx_producer.reserve();
            if (d == nullptr) {
                if (tx_producer.corrupt()) {
                    why = "tx_producer corrupt";
                    break;
                }
                full_waits++;  // ping ring full: the E906 has not taken the earlier ones yet
                break;
            }
            uint32_t slot = tx_producer.head() & DramTxRing::kMask;
            volatile uint8_t *buf = dram_pool + slot * DRAM_SPSC_MAX_BUF_LEN;
            uint32_t seq = next_seq++;
            for (size_t k = 0; k < payload_size; ++k) {
                buf[k] = pattern(seq, k);
            }
            uint64_t ts = get_time_ns();
            tx_ts[seq & DramTxRing::kMask] = ts;
            d->dram_buf_offset = slot * DRAM_SPSC_MAX_BUF_LEN;
            d->seq = seq;
            d->payload_len = payload_size;
            d->host_tx_ts_ns = ts;
            d->flags = DRAM_SPSC_FLAG_READY;
            tx_producer.commit();
            sent++;
        }

        // Take every reply that is ready
        const volatile DramSpscDesc *r = rx_consumer.peek();
        if (r == nullptr) {
            if (rx_consumer.corrupt()) {
                why = "rx_consumer corrupt";
                break;
            }
            if (get_time_ns() - last_progress_ns > (uint64_t)timeout_ms * 1000000ULL) {
                timeouts++;
                std::cerr << "[FAIL] no reply for " << timeout_ms << " ms (expecting seq " << expect_seq
                          << ", " << (sent - received) << " in flight)\n";
                break;
            }
            continue;
        }

        while (r != nullptr) {
            uint32_t slot = rx_consumer.tail() & DramRxRing::kMask;
            uint64_t now = get_time_ns();
            uint32_t seq = r->seq;
            uint64_t sent_ts = tx_ts[expect_seq & DramTxRing::kMask];

            if (r->flags == DRAM_SPSC_FLAG_ERROR) {
                why = "E906 rejected ping seq " + std::to_string(seq) + " as malformed";
            } else if (r->flags != DRAM_SPSC_FLAG_ACK || seq != expect_seq || r->payload_len != payload_size ||
                       r->dram_buf_offset != (DRAM_SPSC_RING_ENTRIES + slot) * DRAM_SPSC_MAX_BUF_LEN ||
                       r->host_tx_ts_ns != sent_ts) {
                why = "bad reply descriptor (seq " + std::to_string(seq) + ", expected " +
                      std::to_string(expect_seq) + ", flags " + std::to_string(r->flags) + ", len " +
                      std::to_string(r->payload_len) + ")";
            } else {
                volatile uint8_t *buf = dram_pool + r->dram_buf_offset;
                if (buf[0] != 'D' || buf[1] != 'R' || buf[2] != 'A' || buf[3] != 'M') {
                    why = "reply tag missing in seq " + std::to_string(seq);
                }
                for (size_t k = 4; why.empty() && k < payload_size; ++k) {
                    if (buf[k] != pattern(seq, k)) {
                        why = "payload byte " + std::to_string(k) + " wrong in seq " + std::to_string(seq);
                    }
                }
            }
            if (!why.empty()) {
                corruptions++;
                break;
            }
            latencies_us.push_back((double)(now - sent_ts) / 1000.0);
            total_payload_bytes += payload_size * 2;
            received++;
            expect_seq++;
            rx_consumer.release();
            last_progress_ns = now;

            r = rx_consumer.peek();
        }

        if (delay_us > 0) {
            usleep(delay_us);
        }
    }
    if (!why.empty()) {
        std::cerr << "[ERROR] malformed: " << why << "\n";
    }

    uint64_t bench_end_ns = get_time_ns();
    double total_time_sec = (double)(bench_end_ns - bench_start_ns) / 1e9;

    std::cout << "\n\n==================== BENCHMARK RESULTS ====================\n";
    std::cout << "Messages Sent  : " << sent << "\n";
    std::cout << "Messages Recv  : " << received << " ("
              << (sent > 0 ? (double)received * 100.0 / sent : 0.0) << "% success)\n";
    std::cout << "Data Integrity : " << (corruptions == 0 ? "PASS (0 corrupted / mismatched packets)" : "FAIL") << "\n";
    std::cout << "Timeouts       : " << timeouts << "\n";
    std::cout << "Window         : " << window << " (ping ring full waits: " << full_waits << ")\n";
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
    return (received != sent || timeouts > 0 || corruptions > 0) ? 1 : 0;
}
