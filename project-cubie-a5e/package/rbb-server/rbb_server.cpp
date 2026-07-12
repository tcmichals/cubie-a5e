#include <iostream>
#include <thread>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>
#include <cstdint>
#include <cstring>
#include <system_error>

#define SHARED_WINDOW_BASE 0x00078000
#define PACKET_SIZE 64
#define BUFFER_DEPTH 511

struct RingBufferPacket {
    uint8_t data[PACKET_SIZE];
};

struct RingBuffer {
    volatile uint32_t head;
    volatile uint32_t tail;
    RingBufferPacket buffer[BUFFER_DEPTH];
};

// Elevate a std::jthread to a POSIX real-time thread (SCHED_FIFO)
// and pin it to an isolated CPU core for hard realtime performance.
void set_realtime_priority(std::jthread& thread, int priority = 90, int target_cpu = 7) {
    pthread_t native_thread = thread.native_handle();
    
    // 1. Set SCHED_FIFO Priority
    sched_param sch_params;
    sch_params.sched_priority = priority;

    if (pthread_setschedparam(native_thread, SCHED_FIFO, &sch_params) != 0) {
        std::cerr << "Warning: Failed to set SCHED_FIFO real-time priority. "
                  << "Are you running as root (CAP_SYS_NICE)? Falling back to standard scheduling.\n";
    } else {
        std::cout << "Real-time SCHED_FIFO priority set successfully (Priority " << priority << ").\n";
    }

    // 2. Set CPU Affinity (Pin to isolated core)
    // The kernel bootargs use 'isolcpus=7 nohz_full=7 rcu_nocbs=7' to keep CPU 7 100% free of OS noise.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(target_cpu, &cpuset);

    if (pthread_setaffinity_np(native_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Warning: Failed to pin thread to CPU " << target_cpu << "\n";
    } else {
        std::cout << "Thread successfully pinned to isolated CPU " << target_cpu << " for HARD realtime performance.\n";
    }
}

void isr_worker(std::stop_token stoken) {
    // 1. Map the Shared Memory Ring Buffer via /dev/mem
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd < 0) {
        std::cerr << "Failed to open /dev/mem\n";
        return;
    }

    void* map_base = mmap(0, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, SHARED_WINDOW_BASE);
    if (map_base == MAP_FAILED) {
        std::cerr << "Failed to mmap ringbuffer\n";
        close(mem_fd);
        return;
    }
    
    volatile RingBuffer* rb = reinterpret_cast<volatile RingBuffer*>(map_base);
    std::cout << "Mapped RingBuffer at physical " << std::hex << SHARED_WINDOW_BASE << "\n";

    // 2. Open the UIO device for the Mailbox doorbell interrupt
    // NOTE: This assumes the kernel is configured with uio_pdrv_genirq bound to the mailbox node.
    int uio_fd = open("/dev/uio0", O_RDONLY);
    if (uio_fd < 0) {
        std::cerr << "Failed to open /dev/uio0. Have you bound the mailbox to UIO?\n";
        // Fallback or exit depending on deployment. For now, exit.
        return;
    }

    std::cout << "Entering Real-time ISR event loop...\n";

    uint32_t irq_count = 0;
    while (!stoken.stop_requested()) {
        // Block until the RISC-V fires the mailbox interrupt doorbell
        // A read on a UIO device blocks until the hardware IRQ is triggered.
        ssize_t bytes_read = read(uio_fd, &irq_count, sizeof(irq_count));
        
        if (bytes_read == sizeof(irq_count)) {
            // Doorbell rang! Drain the lock-free SPSC ring buffer extremely fast.
            while (rb->head != rb->tail) {
                // Extract packet
                uint8_t packet[PACKET_SIZE];
                std::memcpy(packet, (void*)rb->buffer[rb->tail].data, PACKET_SIZE);
                
                // Memory barrier: ensure we read payload before advancing tail
                __sync_synchronize();
                
                // Free slot
                rb->tail = (rb->tail + 1) % BUFFER_DEPTH;
                
                // Process packet (In a real system, hand off to another thread or queue)
                // std::cout << "Received packet of size " << PACKET_SIZE << "!\n";
            }
            
            // Re-enable the UIO interrupt for the next doorbell
            uint32_t enable = 1;
            write(uio_fd, &enable, sizeof(enable));
        }
    }

    munmap(map_base, 0x10000);
    close(mem_fd);
    close(uio_fd);
}

int main() {
    std::cout << "Starting RISC-V RBB Server (C++20 Realtime Edition)...\n";

    // Prevent memory from being paged to swap.
    // Critical for deterministic realtime execution (ArduPilot style).
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: Failed to lock memory (mlockall). Page faults may cause jitter.\n"
                  << "Are you running as root (CAP_IPC_LOCK)?\n";
    } else {
        std::cout << "Memory successfully locked (no page faults/swapping).\n";
    }

    // Create the background worker using C++20 jthread
    std::jthread worker(isr_worker);
    
    // Elevate it to a real-time POSIX thread
    set_realtime_priority(worker);

    // Main thread can perform telemetry, health checks, or manage a socket API
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "[Main] Heartbeat...\n";
    }

    // jthread automatically joins on destruction
    return 0;
}
