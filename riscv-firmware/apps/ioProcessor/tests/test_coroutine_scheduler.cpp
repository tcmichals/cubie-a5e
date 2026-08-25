#include <abstractx/coro.hpp>
#include "isr_dispatcher.hpp"
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
