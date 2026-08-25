#ifndef ETL_CIRCULAR_BUFFER_H
#define ETL_CIRCULAR_BUFFER_H

#include "platform.h"

namespace etl {

template <typename T, size_t MAX_SIZE_>
class circular_buffer {
public:
    using value_type = T;
    using size_type = size_t;

    constexpr circular_buffer() : head_(0), tail_(0), count_(0) {}

    constexpr bool empty() const { return count_ == 0; }
    constexpr bool full() const { return count_ >= MAX_SIZE_; }
    constexpr size_type size() const { return count_; }
    constexpr size_type max_size() const { return MAX_SIZE_; }

    void clear() {
        head_ = 0;
        tail_ = 0;
        count_ = 0;
    }

    bool push(const T& value) {
        if (full()) return false;
        buffer_[head_] = value;
        head_ = (head_ + 1) % MAX_SIZE_;
        count_++;
        return true;
    }

    bool pop(T& value) {
        if (empty()) return false;
        value = buffer_[tail_];
        tail_ = (tail_ + 1) % MAX_SIZE_;
        count_--;
        return true;
    }

private:
    T buffer_[MAX_SIZE_];
    size_type head_;
    size_type tail_;
    size_type count_;
};

} // namespace etl

#endif // ETL_CIRCULAR_BUFFER_H
