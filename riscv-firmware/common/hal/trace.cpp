#include "hal/trace.hpp"
#include <cstdarg>
#include <cstdint>
#include <cstddef>

namespace hal {

// RemoteProc trace buffer size, tied directly to g_rproc_trace_buffer defined in resource_table.c
static constexpr size_t TRACE_BUFFER_SIZE = sizeof(::g_rproc_trace_buffer);

static volatile uint32_t g_trace_head = 0;

void Trace::init() noexcept {
    g_trace_head = 0;
    
    // Clear initial byte so buffer can be read safely immediately
    ::g_rproc_trace_buffer[0] = '\0';
}

void Trace::putc(char c) noexcept {
    // Write to memory trace buffer for Linux remoteproc trace0
    uint32_t idx = g_trace_head;
    if (idx < (TRACE_BUFFER_SIZE - 1)) {
        ::g_rproc_trace_buffer[idx]     = c;
        ::g_rproc_trace_buffer[idx + 1] = '\0';
        g_trace_head                  = idx + 1;
    } else {
        // Wrap-around ring buffer behavior
        g_trace_head = 0;
        ::g_rproc_trace_buffer[0] = c;
        ::g_rproc_trace_buffer[1] = '\0';
    }
}

void Trace::puts(const char* s) noexcept {
    if (!s) s = "(null)";
    while (*s) {
        putc(*s++);
    }
}

void Trace::write(const void* data, size_t len) noexcept {
    if (!data || len == 0) return;
    const char* p = reinterpret_cast<const char*>(data);
    for (size_t i = 0; i < len; ++i) {
        putc(p[i]);
    }
}

uint32_t Trace::get_pos() noexcept {
    return g_trace_head;
}

// -----------------------------------------------------------------------------
// Format Helpers with Field-Width and Zero/Space Padding
// -----------------------------------------------------------------------------
static void print_unsigned(uint32_t val, uint32_t base, bool uppercase, uint32_t width, bool pad_zero) noexcept {
    char buf[32];
    int idx = 0;

    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";

    if (val == 0) {
        buf[idx++] = '0';
    } else {
        while (val > 0) {
            buf[idx++] = digits[val % base];
            val /= base;
        }
    }

    int pad = (width > static_cast<uint32_t>(idx)) ? static_cast<int>(width - idx) : 0;
    char pad_char = pad_zero ? '0' : ' ';

    while (pad-- > 0) {
        Trace::putc(pad_char);
    }

    while (idx > 0) {
        Trace::putc(buf[--idx]);
    }
}

static void print_signed(int32_t val, uint32_t width, bool pad_zero) noexcept {
    if (val < 0) {
        Trace::putc('-');
        if (width > 0) width--;
        print_unsigned(static_cast<uint32_t>(-val), 10, false, width, pad_zero);
    } else {
        print_unsigned(static_cast<uint32_t>(val), 10, false, width, pad_zero);
    }
}

// -----------------------------------------------------------------------------
// Formatted Output Engine
// -----------------------------------------------------------------------------
void Trace::vprintf(const char* fmt, va_list args) noexcept {
    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            continue;
        }

        fmt++; // Skip '%'

        // 1. Check for '0' padding flag
        bool pad_zero = false;
        if (*fmt == '0') {
            pad_zero = true;
            fmt++;
        }

        // 2. Parse field width
        uint32_t width = 0;
        while (*fmt >= '0' && *fmt <= '9') {
            width = (width * 10) + (*fmt - '0');
            fmt++;
        }

        // 3. Match specifier
        switch (*fmt) {
            case 'x': {
                uint32_t val = va_arg(args, uint32_t);
                print_unsigned(val, 16, false, width, pad_zero);
                break;
            }
            case 'X': {
                uint32_t val = va_arg(args, uint32_t);
                print_unsigned(val, 16, true, width, pad_zero);
                break;
            }
            case 'u': {
                uint32_t val = va_arg(args, uint32_t);
                print_unsigned(val, 10, false, width, pad_zero);
                break;
            }
            case 'd':
            case 'i': {
                int32_t val = va_arg(args, int32_t);
                print_signed(val, width, pad_zero);
                break;
            }
            case 'p': {
                uint32_t val = reinterpret_cast<uintptr_t>(va_arg(args, void*));
                putc('0');
                putc('x');
                print_unsigned(val, 16, true, 8, true);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                puts(s);
                break;
            }
            case 'c': {
                char c = static_cast<char>(va_arg(args, int));
                putc(c);
                break;
            }
            case '%': {
                putc('%');
                break;
            }
            default:
                putc('%');
                if (*fmt) putc(*fmt);
                break;
        }

        if (*fmt) fmt++;
    }
}

void Trace::printf(const char* fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

// -----------------------------------------------------------------------------
// Canonical Hex Dump Implementation
// -----------------------------------------------------------------------------
void Trace::dump_hex(const void* data, unsigned int len, unsigned long base_addr) noexcept {
    if (!data || len == 0) return;

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);

    for (unsigned int i = 0; i < len; i += 16) {
        // Print base address offset: 0x00020000:
        printf("0x%08x: ", static_cast<uint32_t>(base_addr + i));

        // Hex bytes (16 per line)
        for (unsigned int j = 0; j < 16; ++j) {
            if (i + j < len) {
                printf("%02x ", static_cast<uint32_t>(bytes[i + j]));
            } else {
                puts("   ");
            }
            if (j == 7) putc(' '); // Group split
        }

        // ASCII representation
        puts(" |");
        for (unsigned int j = 0; j < 16; ++j) {
            if (i + j < len) {
                char c = static_cast<char>(bytes[i + j]);
                putc((c >= 32 && c <= 126) ? c : '.');
            } else {
                putc(' ');
            }
        }
        puts("|\n");
    }
}

} // namespace hal