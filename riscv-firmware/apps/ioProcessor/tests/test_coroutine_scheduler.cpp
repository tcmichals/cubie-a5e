#include <abstractx/scheduler.hpp>
#include <abstractx/task.hpp>
#include <coroutine>
#include "CppUTest/TestHarness.h"

static int g_task1_steps = 0;
static int g_task2_steps = 0;

struct MockYieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

static abstractx::AsyncTask mock_task1() {
    g_task1_steps++;
    co_await MockYieldAwaiter{};
    g_task1_steps++;
    co_return;
}

static abstractx::AsyncTask mock_task2() {
    g_task2_steps++;
    co_await MockYieldAwaiter{};
    g_task2_steps++;
    co_return;
}

TEST_GROUP(AbstractXSchedulerTest) {
    void setup() {
        g_task1_steps = 0;
        g_task2_steps = 0;
        abstractx::StaticCoroutinePool<16384>::reset();
        abstractx::Scheduler::instance().clear();
    }
    void teardown() {
        abstractx::StaticCoroutinePool<16384>::reset();
        abstractx::Scheduler::instance().clear();
    }
};

TEST(AbstractXSchedulerTest, TaskSpawningAndRunOnce) {
    auto& scheduler = abstractx::Scheduler::instance();
    
    // Register tasks
    CHECK_TRUE(scheduler.register_task(mock_task1()));
    CHECK_TRUE(scheduler.register_task(mock_task2()));
    LONGS_EQUAL(2, scheduler.size());

    // Coroutines eager start reached first suspension
    LONGS_EQUAL(1, g_task1_steps);
    LONGS_EQUAL(1, g_task2_steps);

    // Step scheduler to resume tasks
    scheduler.run_once();

    LONGS_EQUAL(2, g_task1_steps);
    LONGS_EQUAL(2, g_task2_steps);
}

TEST(AbstractXSchedulerTest, TaskCapacityLimits) {
    auto& scheduler = abstractx::Scheduler::instance();
    for (size_t i = 0; i < abstractx::MAX_TASKS; ++i) {
        CHECK_TRUE(scheduler.register_task(mock_task1()));
    }
    LONGS_EQUAL(abstractx::MAX_TASKS, scheduler.size());
    // 17th task must fail without allocating heap
    CHECK_FALSE(scheduler.register_task(mock_task1()));
}
