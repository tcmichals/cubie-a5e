#include <atomic>
std::atomic<uint32_t> counter{0};
void increment() {
    counter.fetch_add(1, std::memory_order_release);
}
