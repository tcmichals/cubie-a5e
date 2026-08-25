#ifndef PIGWEED_PW_TOKENIZER_H
#define PIGWEED_PW_TOKENIZER_H

#include <stdint.h>
#include <stddef.h>

namespace pw::tokenizer {

using Token = uint32_t;

// Compile-time 32-bit FNV-1a Hash for string tokenization
constexpr Token HashString(const char* str, size_t len) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint8_t>(str[i]);
        hash *= 16777619u;
    }
    return hash;
}

template <size_t N>
constexpr Token Tokenize(const char (&str)[N]) {
    return HashString(str, N - 1);
}

} // namespace pw::tokenizer

#define PW_TOKENIZE_STRING(str) (::pw::tokenizer::Tokenize(str))

#endif // PIGWEED_PW_TOKENIZER_H
