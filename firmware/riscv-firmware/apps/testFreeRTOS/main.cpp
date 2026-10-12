/*
 * main.cpp - testFreeRTOS: Upstream FreeRTOS v11 on Allwinner T527 XuanTie E906
 *
 * Demonstrates:
 * 1. FreeRTOS v11 Kernel running on E906 @ 200 MHz with M-mode preemptive scheduler.
 * 2. Side-by-side task context switch benchmark (10,000 switches).
 * 3. AbstractX lock-free hal::SpscQueue integration inside FreeRTOS tasks.
 * 4. Linux RemoteProc VirtIO RPMsg ("rpmsg-ping-channel") with MSGBOX ISR wakeup.
 * 5. Full CTF 1.8 / barectf trace hooks emitting 64B TLPs to SRAM C ring buffer.
 * 6. Live FreeRTOS task CPU runtime statistics and stack high-water mark reporting.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <atomic>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "hal/trace.hpp"
#include "hal/timer.hpp"
#include "hal/pmp.hpp"
#include "hal/msgbox.hpp"
#include "hal/rpmsg.hpp"
#include "hal/spsc_queue.hpp"
#include "include/resource_table.h"

extern "C" const struct rpmsg_resource_table global_resource_table;
extern "C" void freertos_risc_v_trap_handler(void);

// Task handles
static TaskHandle_t g_task_a_handle = nullptr;
static TaskHandle_t g_task_b_handle = nullptr;
static TaskHandle_t g_rpmsg_task_handle = nullptr;

// AbstractX SPSC Queue test instance
static hal::SpscQueue<uint32_t, 64> g_spsc_queue;

// Statistics
static std::atomic<uint32_t> g_rpmsg_ping_count{0};
static volatile uint32_t g_switch_count = 0;

// ----------------------------------------------------------------------------
// CTF 1.8 / AbstractX Telemetry Trace Structures & SRAM Ring Buffer
// ----------------------------------------------------------------------------
#pragma pack(push, 1)
struct CtfCoroEvent {
    uint8_t  event_id{1};          // 1: Coroutine / Task lifecycle event
    uint64_t timestamp_us{0};      // Microsecond timestamp
    uint32_t task_id{0};           // Task ID / number
    uint32_t handle_addr{0};       // TCB address
    uint8_t  state{0};             // 0: Spawn, 1: Suspend, 2: Resume, 3: Done
    uint8_t  reason{0};            // 0: Normal
    char     coro_name[8]{0};      // Null-terminated task name
    char     awaiter_token[11]{0}; // Null-terminated token/reason
};
static_assert(sizeof(CtfCoroEvent) == 38, "CtfCoroEvent must be exactly 38 bytes");

struct CtfTlpFrame {
    uint8_t      type{0x10};       // DmaStream (0x10)
    uint8_t      flags{0};
    uint8_t      tag{0};           // 0x00 Coroutine / Task Lifecycle
    uint8_t      channel{3};       // Channel 3: Telemetry / Debug
    uint32_t     target_addr{0};
    uint16_t     len_dw{16};       // 16 DWORDs = 64 bytes
    uint16_t     seq{0};           // Monotonic sequence number
    uint64_t     timestamp_ns{0};  // Nanosecond timestamp
    CtfCoroEvent event;            // 38-byte packed CTF payload
    uint8_t      padding[6]{0};    // 20 + 38 + 6 = 64 bytes
};
static_assert(sizeof(CtfTlpFrame) == 64, "CtfTlpFrame must be exactly 64 bytes");
#pragma pack(pop)

// 64-slot Ring buffer placed in Dedicated MCU SRAM C (0x07131000)
static constexpr size_t TRACE_RING_CAPACITY = 64;

struct alignas(4) TraceBufferHeader {
    uint32_t magic;         // 0x544C5052 ("TLPR")
    uint32_t capacity;      // 64
    uint32_t head;          // Monotonic sequence / write count
    uint32_t dropped;       // Dropped events
};

__attribute__((used, section(".sram_c"), aligned(4)))
static volatile struct {
    TraceBufferHeader hdr;
    CtfTlpFrame       frames[TRACE_RING_CAPACITY];
} g_sram_trace_ring;

static std::atomic<uint32_t> g_trace_seq{0};
static volatile bool g_ctf_trace_enabled = false;

static inline void record_task_switch(uint8_t state, const char* token) {
    if (!g_ctf_trace_enabled) return;

    TaskHandle_t cur = xTaskGetCurrentTaskHandle();
    if (!cur) return;

    const char* name = pcTaskGetName(cur);
    uint32_t seq = g_trace_seq.fetch_add(1, std::memory_order_relaxed);
    uint32_t slot = seq % TRACE_RING_CAPACITY;

    uint64_t ticks = hal::Timer::get_ticks();
    uint64_t now_us = ticks / 200ULL; // 200 MHz
    uint64_t now_ns = ticks * 5ULL;

    CtfTlpFrame frame{};
    frame.type = 0x10;
    frame.tag = 0x00;
    frame.channel = 3;
    frame.seq = static_cast<uint16_t>(seq & 0xFFFF);
    frame.timestamp_ns = now_ns;

    frame.event.event_id = 1;
    frame.event.timestamp_us = now_us;
    frame.event.task_id = static_cast<uint32_t>(uxTaskGetTaskNumber(cur));
    frame.event.handle_addr = reinterpret_cast<uintptr_t>(cur);
    frame.event.state = state;
    frame.event.reason = 0;

    if (name) {
        for (size_t i = 0; i < sizeof(frame.event.coro_name) - 1 && name[i]; ++i) {
            frame.event.coro_name[i] = name[i];
        }
    }
    if (token) {
        for (size_t i = 0; i < sizeof(frame.event.awaiter_token) - 1 && token[i]; ++i) {
            frame.event.awaiter_token[i] = token[i];
        }
    }

    // Copy to volatile SRAM ring
    volatile uint32_t* dst = reinterpret_cast<volatile uint32_t*>(&g_sram_trace_ring.frames[slot]);
    const uint32_t* src = reinterpret_cast<const uint32_t*>(&frame);
    for (size_t i = 0; i < sizeof(CtfTlpFrame) / sizeof(uint32_t); ++i) {
        dst[i] = src[i];
    }

    g_sram_trace_ring.hdr.head = seq + 1;
    g_sram_trace_ring.hdr.magic = 0x544C5052;
    g_sram_trace_ring.hdr.capacity = TRACE_RING_CAPACITY;
}

// ----------------------------------------------------------------------------
// FreeRTOS Trace Hooks
// ----------------------------------------------------------------------------
extern "C" {
void vTraceTaskSwitchedIn(void) {
    g_switch_count++;
    record_task_switch(2, "switched_in"); // CoroState::Resume
}

void vTraceTaskSwitchedOut(void) {
    record_task_switch(1, "switched_out"); // CoroState::Suspend
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    (void)xTask;
    hal::Trace::printf("[testFreeRTOS] FATAL: Stack Overflow in task: %s\n", pcTaskName);
    while (1) { __asm__ volatile("wfi"); }
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   configSTACK_DEPTH_TYPE *pulIdleTaskStackSize) {
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *pulTimerTaskStackSize) {
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationMallocFailedHook(void) {
    hal::Trace::puts("[testFreeRTOS] FATAL: FreeRTOS pvPortMalloc failed!\n");
    while (1) { __asm__ volatile("wfi"); }
}

// External PLIC interrupt dispatcher called from FreeRTOS portASM.S
void freertos_risc_v_application_interrupt_handler(uint32_t mcause) {
    (void)mcause;
    volatile uint32_t &plic_reg = *reinterpret_cast<volatile uint32_t*>(0x10000000 + 0x200004);
    uint32_t irq = plic_reg;

    while (irq != 0) {
        if (irq == 48) { // Allwinner T527 MSGBOX Doorbell (PLIC IRQ 48 / 25)
            hal::MsgBox::clear_irq_status(hal::MsgBox::Channel::Channel1);
            if (g_rpmsg_task_handle != nullptr) {
                BaseType_t xHigherPriorityTaskWoken = pdFALSE;
                vTaskNotifyGiveFromISR(g_rpmsg_task_handle, &xHigherPriorityTaskWoken);
                portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            }
        }
        plic_reg = irq; // Signal completion to hardware
        irq = plic_reg;
    }
}

void freertos_risc_v_application_exception_handler(uint32_t mcause) {
    hal::Trace::printf("[testFreeRTOS] FATAL EXCEPTION: mcause=0x%08x\n", mcause);
    while (1) { __asm__ volatile("wfi"); }
}
} // extern "C"

// ----------------------------------------------------------------------------
// Runtime Statistics Reporter
// ----------------------------------------------------------------------------
static void log_task_runtime_stats(void) {
    static constexpr UBaseType_t MAX_TASKS = 8;
    TaskStatus_t status_array[MAX_TASKS];
    uint32_t total_runtime = 0;
    UBaseType_t num_tasks = uxTaskGetSystemState(status_array, MAX_TASKS, &total_runtime);

    hal::Trace::puts("\n================================================================\n");
    hal::Trace::puts("  FreeRTOS v11 XuanTie E906 Task CPU Usage & High Water Mark    \n");
    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  Task Name       | ID | State | Stack Left | Runtime (cyc) | CPU %\n");
    hal::Trace::puts("  --------------------------------------------------------------\n");

    for (UBaseType_t i = 0; i < num_tasks; ++i) {
        float cpu_pct = (total_runtime > 0)
            ? (float)status_array[i].ulRunTimeCounter * 100.0f / (float)total_runtime
            : 0.0f;
        const char *st_str = "UNK ";
        switch (status_array[i].eCurrentState) {
            case eRunning:   st_str = "RUN "; break;
            case eReady:     st_str = "RDY "; break;
            case eBlocked:   st_str = "BLK "; break;
            case eSuspended: st_str = "SUSP"; break;
            case eDeleted:   st_str = "DEL "; break;
            default: break;
        }
        hal::Trace::printf("  %-15s | #%u | %s  | %5u words | %13u | %5.1f%%\n",
                           status_array[i].pcTaskName,
                           (uint32_t)status_array[i].xTaskNumber,
                           st_str,
                           (uint32_t)status_array[i].usStackHighWaterMark,
                           (uint32_t)status_array[i].ulRunTimeCounter,
                           cpu_pct);
    }
    hal::Trace::printf("  FreeRTOS Heap Free : %u bytes | Total Runtime: %u cycles\n",
                       (uint32_t)xPortGetFreeHeapSize(), total_runtime);
    hal::Trace::printf("  SRAM CTF Trace Ring: %p (Head: %u, Cap: %u)\n",
                       (void*)&g_sram_trace_ring, (uint32_t)g_sram_trace_ring.hdr.head, (uint32_t)TRACE_RING_CAPACITY);
    hal::Trace::puts("================================================================\n");
}

// ----------------------------------------------------------------------------
// RPMsg Echo Endpoint Handler
// ----------------------------------------------------------------------------
static void handle_rpmsg_ping(const hal::RpmsgMessage &msg, void *user_data) {
    (void)user_data;
    g_rpmsg_ping_count.fetch_add(1, std::memory_order_relaxed);

    uint8_t pong_buf[RPMSG_BUFFER_SIZE];
    uint16_t resp_len = msg.len;
    if (resp_len > sizeof(pong_buf)) resp_len = sizeof(pong_buf);
    if (msg.data && resp_len > 0) memcpy(pong_buf, msg.data, resp_len);

    if (resp_len >= 4) {
        pong_buf[0] = 0x50; pong_buf[1] = 0x4F; pong_buf[2] = 0x4E; pong_buf[3] = 0x47; // PONG
    }

    hal::Rpmsg::reply(msg, pong_buf, resp_len);
}

// ----------------------------------------------------------------------------
// FreeRTOS RPMsg Worker Task (Event-Driven via MSGBOX PLIC ISR)
// ----------------------------------------------------------------------------
static void rpmsg_worker_task(void *pvParameters) {
    (void)pvParameters;

    hal::Trace::puts("[testFreeRTOS] RPMsg Worker Task active. Waiting for Linux Host...\n");

    bool announced = false;
    uint32_t last_logged_pings = 0;

    while (1) {
        // Wait for MSGBOX Doorbell interrupt notification from ISR
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (hal::Rpmsg::is_driver_ready()) {
            if (!announced) {
                hal::Trace::puts("[testFreeRTOS] Linux VirtIO ONLINE -> Announcing rpmsg-ping-channel\n");
                hal::Rpmsg::announce_service("rpmsg-ping-channel", RPMSG_PING_EPT_ADDR);
                announced = true;
            }

            hal::Rpmsg::poll();
        }

        uint32_t current_pings = g_rpmsg_ping_count.load(std::memory_order_relaxed);
        if (current_pings >= last_logged_pings + 5000) {
            last_logged_pings = current_pings;
            log_task_runtime_stats();
        }
    }
}

// ----------------------------------------------------------------------------
// Benchmark Tasks (10,000 Consecutive Context Switches)
// ----------------------------------------------------------------------------
static constexpr uint32_t BENCHMARK_ROUNDS = 10000;

static void task_b_entry(void *pvParameters) {
    (void)pvParameters;
    for (uint32_t i = 0; i < BENCHMARK_ROUNDS; i++) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xTaskNotifyGive(g_task_a_handle);
    }
    vTaskDelete(nullptr);
}

static void task_a_entry(void *pvParameters) {
    (void)pvParameters;

    hal::Trace::puts("----------------------------------------------------------------\n");
    hal::Trace::printf("[testFreeRTOS] Starting Task Switch Benchmark (%u iterations)...\n", BENCHMARK_ROUNDS);

    uint64_t start_cycles = hal::Timer::get_ticks();

    for (uint32_t i = 0; i < BENCHMARK_ROUNDS; i++) {
        xTaskNotifyGive(g_task_b_handle);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    uint64_t end_cycles = hal::Timer::get_ticks();
    uint64_t total_cycles = end_cycles - start_cycles;
    // Each round is 2 context switches: A -> B and B -> A
    uint32_t total_switches = BENCHMARK_ROUNDS * 2;
    uint32_t cycles_per_switch = (uint32_t)(total_cycles / total_switches);
    // At 200 MHz, 1 cycle = 5 ns
    uint32_t ns_per_switch = cycles_per_switch * 5;

    UBaseType_t stack_a = uxTaskGetStackHighWaterMark(g_task_a_handle);
    UBaseType_t stack_b = uxTaskGetStackHighWaterMark(g_task_b_handle);

    hal::Trace::puts("================================================================\n");
    hal::Trace::puts("  FreeRTOS v11 on XuanTie E906 Empirical Benchmark Results      \n");
    hal::Trace::puts("================================================================\n");
    hal::Trace::printf("  Total Switches     : %u\n", total_switches);
    hal::Trace::printf("  Total Elapsed Cycles: %u\n", (uint32_t)total_cycles);
    hal::Trace::printf("  Cycles Per Switch  : %u cycles\n", cycles_per_switch);
    hal::Trace::printf("  Time Per Switch    : %u ns\n", ns_per_switch);
    hal::Trace::printf("  Task A Stack Left  : %u words (%u bytes used)\n",
                       (uint32_t)stack_a, (uint32_t)(configMINIMAL_STACK_SIZE - stack_a) * 4);
    hal::Trace::printf("  Task B Stack Left  : %u words (%u bytes used)\n",
                       (uint32_t)stack_b, (uint32_t)(configMINIMAL_STACK_SIZE - stack_b) * 4);
    hal::Trace::printf("  FreeRTOS Heap Free : %u bytes\n", (uint32_t)xPortGetFreeHeapSize());
    hal::Trace::puts("================================================================\n");

    // Print full FreeRTOS task CPU runtime statistics and stack metrics
    log_task_runtime_stats();

    // Next: Test AbstractX Lock-Free SPSC Queue within FreeRTOS
    hal::Trace::puts("[testFreeRTOS] Testing AbstractX hal::SpscQueue throughput...\n");
    uint64_t q_start = hal::Timer::get_ticks();
    for (uint32_t i = 0; i < BENCHMARK_ROUNDS; i++) {
        g_spsc_queue.push(i);
        uint32_t val = 0;
        g_spsc_queue.pop(val);
    }
    uint64_t q_cycles = hal::Timer::get_ticks() - q_start;
    hal::Trace::printf("[testFreeRTOS] hal::SpscQueue Push+Pop: %u cycles/op (%u ns)\n",
                       (uint32_t)(q_cycles / BENCHMARK_ROUNDS), (uint32_t)(q_cycles / BENCHMARK_ROUNDS) * 5);

    // Enable live CTF 1.8 TLP tracing for subsequent operational phase
    g_ctf_trace_enabled = true;
    hal::Trace::puts("[testFreeRTOS] CTF 1.8 TLP Tracing activated for live operation.\n");

    vTaskDelete(nullptr);
}

// ----------------------------------------------------------------------------
// Main Application Entry
// ----------------------------------------------------------------------------
int main(void) {
    hal::Trace::init();
    hal::Timer::init();
    hal::Pmp::init();

    hal::Trace::puts("\n================================================================\n");
    hal::Trace::puts("  Allwinner T527 XuanTie E906 testFreeRTOS (FreeRTOS v11.1.0)   \n");
    hal::Trace::puts("  Built with AbstractX Drivers (hal::Rpmsg, SpscQueue, Trace)   \n");
    hal::Trace::puts("================================================================\n");

    // Initialize CTF SRAM trace ring header
    g_sram_trace_ring.hdr.magic = 0x544C5052; // TLPR
    g_sram_trace_ring.hdr.capacity = TRACE_RING_CAPACITY;
    g_sram_trace_ring.hdr.head = 0;
    g_sram_trace_ring.hdr.dropped = 0;

    // Initialize RPMsg engine with standard Resource Table
    hal::Rpmsg::init(&global_resource_table);
    hal::Rpmsg::register_endpoint(RPMSG_PING_EPT_ADDR, handle_rpmsg_ping);
    hal::Rpmsg::register_endpoint(0, handle_rpmsg_ping);

    // Install FreeRTOS RISC-V trap handler
    uintptr_t trap_handler_addr = reinterpret_cast<uintptr_t>(freertos_risc_v_trap_handler);
    __asm__ volatile("csrw mtvec, %0" :: "r"(trap_handler_addr));

    // Enable Hardware Mailbox Channel 1 interrupt
    hal::MsgBox::enable_rx_irq(hal::MsgBox::Channel::Channel1, true);

    // Create Benchmark Tasks (Stack: 512 words = 2048 bytes each)
    xTaskCreate(task_a_entry, "bench_a", 512, nullptr, tskIDLE_PRIORITY + 2, &g_task_a_handle);
    xTaskCreate(task_b_entry, "bench_b", 512, nullptr, tskIDLE_PRIORITY + 2, &g_task_b_handle);

    // Create RPMsg Worker Task (Stack: 1024 words = 4096 bytes)
    xTaskCreate(rpmsg_worker_task, "rpmsg_rx", 1024, nullptr, tskIDLE_PRIORITY + 3, &g_rpmsg_task_handle);

    hal::Trace::puts("[testFreeRTOS] Starting FreeRTOS v11 Scheduler...\n");

    vTaskStartScheduler();

    // Should never reach here
    while (1) { __asm__ volatile("wfi"); }
    return 0;
}
