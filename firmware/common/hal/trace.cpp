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

void Trace::print_float(float val, int decimals) noexcept {
    // Check for NaN
    if (val != val) {
        puts("NaN");
        return;
    }
    // Check for Inf
    if (val > 1e38f || val < -1e38f) {
        puts("Inf");
        return;
    }
    if (val < 0.0f) {
        putc('-');
        val = -val;
    }

    // Rounding offset based on decimals
    double round = 0.5;
    for (int i = 0; i < decimals; ++i) {
        round /= 10.0;
    }
    double dval = static_cast<double>(val) + round;

    uint32_t int_part = static_cast<uint32_t>(dval);
    print_unsigned(int_part, 10, false, 0, false);

    if (decimals > 0) {
        putc('.');
        double frac = dval - static_cast<double>(int_part);
        for (int i = 0; i < decimals; ++i) {
            frac *= 10.0;
            uint32_t digit = static_cast<uint32_t>(frac);
            if (digit > 9) digit = 9;
            putc('0' + digit);
            frac -= digit;
        }
    }
}

void Trace::print_uint(uint32_t val) noexcept {
    print_unsigned(val, 10, false, 0, false);
}

void Trace::print_int(int32_t val) noexcept {
    print_signed(val, 0, false);
}

void Trace::print_hex(uint32_t val, bool prefix) noexcept {
    if (prefix) {
        putc('0');
        putc('x');
    }
    print_unsigned(val, 16, false, 8, true);
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

        // 2b. Parse precision (.prec)
        uint32_t precision = 6;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (*fmt >= '0' && *fmt <= '9') {
                precision = (precision * 10) + (*fmt - '0');
                fmt++;
            }
        }

        // 3. Match specifier
        switch (*fmt) {
            case 'f': {
                double val = va_arg(args, double);
                print_float(val, precision);
                break;
            }
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

/*
 * C Linkage Wrappers for C Source Files
 */
extern "C" {

void trace_init(void) {
    hal::Trace::init();
}

void trace_putc(char c) {
    hal::Trace::putc(c);
}

void trace_puts(const char *s) {
    hal::Trace::puts(s);
}

void trace_printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    hal::Trace::vprintf(fmt, args);
    va_end(args);
}

void trace_put_uint(uint32_t val) {
    hal::Trace::print_uint(val);
}

void trace_put_hex(uint32_t val) {
    hal::Trace::print_hex(val);
}

void trace_put_float(float val, int decimals) {
    hal::Trace::print_float(val, decimals);
}

} // extern "C"