#ifndef ETL_VECTOR_H
#define ETL_VECTOR_H

#include "platform.h"
#include <new>

namespace etl {

template <typename T, size_t MAX_SIZE_>
class vector {
public:
    using value_type = T;
    using size_type = size_t;
    using reference = T&;
    using const_reference = const T&;
    using pointer = T*;
    using const_pointer = const T*;
    using iterator = T*;
    using const_iterator = const T*;

    constexpr vector() : size_(0) {}

    ~vector() {
        clear();
    }

    constexpr bool empty() const { return size_ == 0; }
    constexpr bool full() const { return size_ >= MAX_SIZE_; }
    constexpr size_type size() const { return size_; }
    constexpr size_type max_size() const { return MAX_SIZE_; }
    constexpr size_type capacity() const { return MAX_SIZE_; }

    void clear() {
        for (size_type i = 0; i < size_; ++i) {
            get_ptr(i)->~T();
        }
        size_ = 0;
    }

    bool push_back(const T& value) {
        if (full()) return false;
        new (get_ptr(size_)) T(value);
        size_++;
        return true;
    }

    bool push_back(T&& value) {
        if (full()) return false;
        new (get_ptr(size_)) T(static_cast<T&&>(value));
        size_++;
        return true;
    }

    void pop_back() {
        if (size_ > 0) {
            size_--;
            get_ptr(size_)->~T();
        }
    }

    reference operator[](size_type index) {
        return *get_ptr(index);
    }

    const_reference operator[](size_type index) const {
        return *get_ptr(index);
    }

    reference front() { return (*this)[0]; }
    const_reference front() const { return (*this)[0]; }
    reference back() { return (*this)[size_ - 1]; }
    const_reference back() const { return (*this)[size_ - 1]; }

    pointer data() { return get_ptr(0); }
    const_pointer data() const { return get_ptr(0); }

    iterator begin() { return data(); }
    const_iterator begin() const { return data(); }
    iterator end() { return data() + size_; }
    const_iterator end() const { return data() + size_; }

private:
    pointer get_ptr(size_type index) {
        return reinterpret_cast<pointer>(&storage_[index * sizeof(T)]);
    }

    const_pointer get_ptr(size_type index) const {
        return reinterpret_cast<const_pointer>(&storage_[index * sizeof(T)]);
    }

    alignas(alignof(T)) uint8_t storage_[MAX_SIZE_ * sizeof(T)];
    size_type size_;
};

} // namespace etl

#endif // ETL_VECTOR_H
