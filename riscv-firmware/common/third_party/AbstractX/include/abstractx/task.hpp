#ifndef ABSTRACTX_TASK_HPP
#define ABSTRACTX_TASK_HPP

#include <coroutine>
#include <cstdint>
#include <cstddef>

namespace abstractx {

template <size_t PoolSize = 4096>
class StaticCoroutinePool {
public:
    static void* allocate(size_t size) {
        size_t aligned_size = (size + 7) & ~7; // 8-byte alignment
        if (offset_ + aligned_size > PoolSize) {
            return nullptr; // Out of static pool memory
        }
        void* ptr = &pool_[offset_];
        offset_ += aligned_size;
        return ptr;
    }

    static void deallocate(void*, size_t) {
        // Static arena: individual frames retained in pool
    }

    static void reset() {
        offset_ = 0;
    }

private:
    static inline uint8_t pool_[PoolSize] __attribute__((section(".dtcm")));
    static inline size_t offset_ = 0;
};

// AbstractX Task Handle
struct AsyncTask {
    struct promise_type {
        AsyncTask get_return_object() noexcept {
            return AsyncTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}

        void* operator new(size_t size) {
            return StaticCoroutinePool<4096>::allocate(size);
        }
        void operator delete(void* ptr, size_t size) noexcept {
            StaticCoroutinePool<4096>::deallocate(ptr, size);
        }
    };

    std::coroutine_handle<promise_type> handle;

    bool resume() {
        if (handle && !handle.done()) {
            handle.resume();
            return !handle.done();
        }
        return false;
    }

    bool done() const {
        return !handle || handle.done();
    }
};

} // namespace abstractx

#endif // ABSTRACTX_TASK_HPP
