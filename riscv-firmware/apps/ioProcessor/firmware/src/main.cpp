#include "hal/ccu.hpp"
#include "hal/pio.hpp"
#include "hal/spi.hpp"
#include "hal/uart.hpp"
#include "hal/timer.hpp"
#include "hal/msgbox.hpp"
#include "ipc/ringbuffer.hpp"
#include "logging/pw_log_backend.hpp"
#include "logging/trace_manager.hpp"
#include "coroutines/io_tasks.hpp"
#include <abstractx/scheduler.hpp>

// Shared Ring Buffers in SRAM C
static fc::ipc::SpscRingBuffer g_tx_ring(IPC_SHARED_MEM_BASE + IPC_TX_RING_OFFSET);
static fc::ipc::SpscRingBuffer g_rx_ring(IPC_SHARED_MEM_BASE + IPC_RX_RING_OFFSET);

extern "C" {

void msip_dispatch_events(void) {
    // Clear Machine Software Interrupt pending bit
    __asm__ volatile("csrc mip, %0" :: "r"(1 << 3));
    // Step scheduler to resume any awaiting tasks
    abstractx::Scheduler::instance().run_once();
}

void plic_dispatch_interrupt(void) {
    // External interrupt handler (PIO edge / MSGBOX)
    if (fc::hal::MsgBox::has_host_notification()) {
        abstractx::Scheduler::instance().run_once();
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

    // 6. Spawn Core Hardware I/O Tasks
    auto& sched = abstractx::Scheduler::instance();
    sched.register_task(fc::coroutines::fpga_pcie_tlp_task(&g_rx_ring, &g_tx_ring));
    sched.register_task(fc::coroutines::imu_sensor_task(&g_tx_ring));
    sched.register_task(fc::coroutines::uart_stream_task(&g_tx_ring));

    PW_LOG_INFO("Entering Hard Real-Time I/O Event Loop");

    // 7. Run AbstractX Cooperative Coroutine Loop
    sched.run();

    return 0;
}

} // extern "C"
