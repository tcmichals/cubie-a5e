#include <abstractx/coro.hpp>
#include "isr_dispatcher.hpp"
#include "timer.hpp"
#include <coroutine>
#include "CppUTest/TestHarness.h"

static int g_task1_steps = 0;
static int g_task2_steps = 0;

static abstractx::Task<void> mock_task1() {
    g_task1_steps++;
    co_await abstractx::yield();
    g_task1_steps++;
    co_return;
}

static abstractx::Task<void> mock_task2() {
    g_task2_steps++;
    co_await abstractx::yield();
    g_task2_steps++;
    co_return;
}

TEST_GROUP(AbstractXSchedulerTest) {
    void setup() {
        g_task1_steps = 0;
        g_task2_steps = 0;
    }
    void teardown() {}
};

TEST(AbstractXSchedulerTest, TaskSpawningAndStep) {
    auto t1 = mock_task1();
    auto t2 = mock_task2();

    // In AbstractX Task, initial_suspend is suspend_always
    LONGS_EQUAL(0, g_task1_steps);
    LONGS_EQUAL(0, g_task2_steps);

    // Resume first step up to yield
    CHECK_TRUE(t1.resume());
    CHECK_TRUE(t2.resume());
    LONGS_EQUAL(1, g_task1_steps);
    LONGS_EQUAL(1, g_task2_steps);

    // Resume second step to completion
    t1.resume();
    t2.resume();
    LONGS_EQUAL(2, g_task1_steps);
    LONGS_EQUAL(2, g_task2_steps);
    CHECK_TRUE(t1.done());
    CHECK_TRUE(t2.done());
}

TEST(AbstractXSchedulerTest, IsrDispatcherSafePostingAndResume) {
    auto t1 = mock_task1();
    t1.resume(); // Advance to yield
    LONGS_EQUAL(1, g_task1_steps);

    // ISR posts handle to SPSC queue (does NOT execute coroutine in ISR context)
    fc::hal::IsrDispatcher::isr_post_resume(t1.handle());
    LONGS_EQUAL(1, g_task1_steps);

    // Main thread dispatcher drains SPSC queue and safely resumes coroutine
    fc::hal::IsrDispatcher::process_ready_coroutines();
    LONGS_EQUAL(2, g_task1_steps);
    CHECK_TRUE(t1.done());
}

static int g_timer_task_steps = 0;
static abstractx::Task<void> mock_timer_sleep_task() {
    g_timer_task_steps++;
    co_await fc::hal::Timer::async_sleep_ms(5); // Sleep 5 ms via ETL timer
    g_timer_task_steps++;
    co_return;
}

TEST(AbstractXSchedulerTest, EtlTimerAsynchronousSleep) {
    fc::hal::Timer::init();
    g_timer_task_steps = 0;

    auto task = mock_timer_sleep_task();
    task.resume(); // Starts and suspends at async_sleep_ms(5)
    LONGS_EQUAL(1, g_timer_task_steps);

    // Advance 4 ticks: timer should NOT expire yet
    for (int i = 0; i < 4; ++i) {
        fc::hal::Timer::handle_tick_irq(1);
    }
    fc::hal::IsrDispatcher::process_ready_coroutines();
    LONGS_EQUAL(1, g_timer_task_steps);

    // 5th tick: ETL timer expires, posts handle to IsrDispatcher
    fc::hal::Timer::handle_tick_irq(1);

    // Main thread dispatcher drains queue and resumes task
    fc::hal::IsrDispatcher::process_ready_coroutines();
    LONGS_EQUAL(2, g_timer_task_steps);
    CHECK_TRUE(task.done());
}
