#ifndef ABSTRACTX_SCHEDULER_HPP
#define ABSTRACTX_SCHEDULER_HPP

#include "task.hpp"
#include <etl/vector.h>

namespace abstractx {

constexpr size_t MAX_TASKS = 16;

class Scheduler {
public:
    static Scheduler& instance() {
        static Scheduler sched;
        return sched;
    }

    bool register_task(AsyncTask task) {
        return tasks_.push_back(task);
    }

    void run_once() {
        for (auto& task : tasks_) {
            if (!task.done()) {
                task.resume();
            }
        }
    }

    [[noreturn]] void run() {
        while (true) {
            run_once();
            // Enter low-power sleep; wakes on any interrupt or MSIP
            __asm__ volatile("wfi");
        }
    }

private:
    Scheduler() = default;
    etl::vector<AsyncTask, MAX_TASKS> tasks_;
};

} // namespace abstractx

#endif // ABSTRACTX_SCHEDULER_HPP
