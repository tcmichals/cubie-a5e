#ifndef PIGWEED_PW_SPAN_H
#define PIGWEED_PW_SPAN_H

#include <cstddef>
#include <type_traits>

namespace pw {

template <typename T>
class span {
public:
    constexpr span() noexcept : data_(nullptr), size_(0) {}
    constexpr span(T* data, size_t size) noexcept : data_(data), size_(size) {}

    template <size_t N>
    constexpr span(T (&arr)[N]) noexcept : data_(arr), size_(N) {}

    constexpr T* data() const noexcept { return data_; }
    constexpr size_t size() const noexcept { return size_; }
    constexpr bool empty() const noexcept { return size_ == 0; }

    constexpr T& operator[](size_t idx) const noexcept { return data_[idx]; }

    constexpr T* begin() const noexcept { return data_; }
    constexpr T* end() const noexcept { return data_ + size_; }

private:
    T* data_;
    size_t size_;
};

} // namespace pw

#endif // PIGWEED_PW_SPAN_H
