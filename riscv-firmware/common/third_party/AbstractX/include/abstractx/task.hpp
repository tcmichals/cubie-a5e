#ifndef ABSTRACTX_TASK_HPP
#define ABSTRACTX_TASK_HPP

#include <coroutine>
#include <cstdint>
#include <cstddef>

namespace abstractx {

template <size_t PoolSize = 16384>
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

    static void deallocate(void*, size_t) noexcept {
        // Static arena pool
    }

    static void reset() {
        offset_ = 0;
    }

private:
#if defined(__riscv)
    static inline uint8_t pool_[PoolSize] __attribute__((section(".dtcm"), aligned(8)));
#else
    alignas(8) static inline uint8_t pool_[PoolSize];
#endif
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
            return StaticCoroutinePool<16384>::allocate(size);
        }
        void operator delete(void* ptr, size_t size) noexcept {
            StaticCoroutinePool<16384>::deallocate(ptr, size);
        }
    };

    std::coroutine_handle<promise_type> handle{nullptr};

    bool resume() {
        if (handle && handle.address() != nullptr && !handle.done()) {
            handle.resume();
            return (handle && handle.address() != nullptr && !handle.done());
        }
        return false;
    }

    bool done() const {
        if (!handle || handle.address() == nullptr) return true;
        return handle.done();
    }
};

} // namespace abstractx

#endif // ABSTRACTX_TASK_HPP
