#include "ccu.hpp"
#include "pio.hpp"
#include "spi.hpp"
#include "uart.hpp"
#include "timer.hpp"
#include "msgbox.hpp"
#include "isr_dispatcher.hpp"
#include "ringbuffer.hpp"
#include "pw_log_backend.hpp"
#include "trace_manager.hpp"
#include "io_tasks.hpp"
#include <abstractx/coro.hpp>

// Shared Ring Buffers in SRAM C
static fc::ipc::SpscRingBuffer g_tx_ring(IPC_SHARED_MEM_BASE + IPC_TX_RING_OFFSET);
static fc::ipc::SpscRingBuffer g_rx_ring(IPC_SHARED_MEM_BASE + IPC_RX_RING_OFFSET);

extern "C" {

void msip_dispatch_events(void) {
    // Clear Machine Software Interrupt pending bit
    __asm__ volatile("csrc mip, %0" :: "r"(1 << 3));
    // Safely resume any coroutines posted by ISRs in thread context
    fc::hal::IsrDispatcher::process_ready_coroutines();
}

void plic_dispatch_interrupt(void) {
    // Top-Half External interrupt handlers (strictly non-blocking, post handles to SPSC queue)
    fc::hal::Spi0::handle_irq();
    fc::hal::Uart2::handle_irq();

    if (fc::hal::MsgBox::has_host_notification()) {
        // Trigger MSIP to wake main thread scheduler
        __asm__ volatile("csrs mip, %0" :: "r"(1 << 3));
    }
}

void timer_dispatch_interrupt(void) {
    // Timer tick dispatcher
}

int main(void) {
    // 1. Initialize Hardware Clocks & Resets
    fc::hal::Ccu::init();

    // 2. Initialize PIO Pinmux (Port B & Port C)
    fc::hal::Pio::init();

    // 3. Initialize Peripherals
    fc::hal::Spi0::init(25000000); // 25 MHz Dual/Single SPI
    fc::hal::Uart2::init(115200);  // 115200 Baud UART2
    fc::hal::MsgBox::init();

    // 4. Initialize Shared Memory IPC Queues
    g_tx_ring.init();
    g_rx_ring.init();

    // 5. Initialize Diagnostics
    fc::logging::PigweedLogger::init(&g_tx_ring);
    fc::logging::TraceManager::init();

    PW_LOG_INFO("XuanTie E907 Hardware I/O & PCIe TLP Processor Booting...");
    PW_LOG_INFO("AbstractX C++20 Coroutine Scheduler Initialized");

    // 6. Spawn Core Hardware I/O Tasks (Eager start up to first awaiter)
    auto tlp_task = fc::coroutines::fpga_pcie_tlp_task(&g_rx_ring, &g_tx_ring);
    auto imu_task = fc::coroutines::imu_sensor_task(&g_tx_ring);
    auto uart_task = fc::coroutines::uart_stream_task(&g_tx_ring);

    tlp_task.resume();
    imu_task.resume();
    uart_task.resume();

    PW_LOG_INFO("Entering Hard Real-Time I/O Event Loop");

    // 7. Run Cooperative Event Loop (Woken by hardware interrupts and MSIP)
    while (true) {
        fc::hal::IsrDispatcher::process_ready_coroutines();
#if defined(__riscv)
        __asm__ volatile("wfi");
#endif
    }

    return 0;
}

} // extern "C"
