#include "trace.hpp"

// S_UART0 Hardware Registers on Allwinner T527
#define S_UART0_BASE_ADDR   0x07080000UL
#define S_UART0_THR         (*(volatile uint32_t *)(S_UART0_BASE_ADDR + 0x00))
#define S_UART0_LSR         (*(volatile uint32_t *)(S_UART0_BASE_ADDR + 0x14))

extern "C" char g_rproc_trace_buffer[];

namespace hal {

volatile uint32_t Trace::s_pos = 0;
bool Trace::s_serial_mirror = false;

static char *get_trace_buffer() noexcept {
    return g_rproc_trace_buffer;
}

void Trace::init(bool enable_serial_mirror) noexcept {
    char *buf = get_trace_buffer();
    for (uint32_t i = 0; i < CONFIG_RPROC_TRACE0_LEN; ++i) {
        buf[i] = '\0';
    }
    s_pos = 0;
    s_serial_mirror = enable_serial_mirror;
}

void Trace::putc(char c) noexcept {
    // 1. Output to RemoteProc trace0 buffer
    char *buf = get_trace_buffer();
    if (s_pos >= (CONFIG_RPROC_TRACE0_LEN - 1)) {
        s_pos = 0; // Wrap circular trace buffer
    }
    buf[s_pos++] = c;
    buf[s_pos] = '\0';

    // 2. Mirror to S_UART0 hardware serial if enabled
    if (s_serial_mirror) {
        // Wait for Transmitter Holding Register Empty (bit 5)
        while ((S_UART0_LSR & (1UL << 5)) == 0) {
            // spin
        }
        S_UART0_THR = static_cast<uint32_t>(c);
        if (c == '\n') {
            while ((S_UART0_LSR & (1UL << 5)) == 0) {}
            S_UART0_THR = '\r';
        }
    }
}

void Trace::puts(const char *str) noexcept {
    if (!str) return;
    while (*str) {
        putc(*str++);
    }
}

void Trace::write(const void *data, size_t len) noexcept {
    if (!data) return;
    const char *p = static_cast<const char *>(data);
    for (size_t i = 0; i < len; ++i) {
        putc(p[i]);
    }
}

void Trace::print_uint(uint32_t val) noexcept {
    char buf[12];
    int idx = 0;
    if (val == 0) {
        putc('0');
        return;
    }
    while (val > 0) {
        buf[idx++] = static_cast<char>('0' + (val % 10));
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; --i) {
        putc(buf[i]);
    }
}

void Trace::print_int(int32_t val) noexcept {
    if (val < 0) {
        putc('-');
        print_uint(static_cast<uint32_t>(-val));
    } else {
        print_uint(static_cast<uint32_t>(val));
    }
}

void Trace::print_hex(uint32_t val, bool prefix) noexcept {
    const char hex_chars[] = "0123456789ABCDEF";
    if (prefix) {
        puts("0x");
    }
    for (int i = 28; i >= 0; i -= 4) {
        putc(hex_chars[(val >> i) & 0xF]);
    }
}

void Trace::print_float(float val, int decimals) noexcept {
    if (val < 0.0f) {
        putc('-');
        val = -val;
    }
    uint32_t int_part = static_cast<uint32_t>(val);
    print_uint(int_part);
    putc('.');
    float frac = val - static_cast<float>(int_part);
    for (int i = 0; i < decimals; ++i) {
        frac *= 10.0f;
        uint32_t d = static_cast<uint32_t>(frac);
        putc(static_cast<char>('0' + (d % 10)));
        frac -= static_cast<float>(d);
    }
}

void Trace::vprintf(const char *fmt, va_list args) noexcept {
    if (!fmt) return;
    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
            continue;
        }
        fmt++; // skip '%'
        if (!*fmt) break;

        switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                puts(s ? s : "(null)");
                break;
            }
            case 'd':
            case 'i': {
                int32_t v = va_arg(args, int32_t);
                print_int(v);
                break;
            }
            case 'u': {
                uint32_t v = va_arg(args, uint32_t);
                print_uint(v);
                break;
            }
            case 'x':
            case 'X':
            case 'p': {
                uint32_t v = va_arg(args, uint32_t);
                print_hex(v, (*fmt == 'p'));
                break;
            }
            case 'f': {
                // In C variadics, float is promoted to double
                double v = va_arg(args, double);
                print_float(static_cast<float>(v), 3);
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
            default: {
                putc('%');
                putc(*fmt);
                break;
            }
        }
        fmt++;
    }
}

void Trace::printf(const char *fmt, ...) noexcept {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

void Trace::dump_hex(const void *addr, size_t len, uint32_t base_addr) noexcept {
    const uint8_t *bytes = static_cast<const uint8_t *>(addr);
    const char hex_chars[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; i += 16) {
        print_hex(base_addr + static_cast<uint32_t>(i), true);
        puts(": ");

        // Hex bytes
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) {
                uint8_t b = bytes[i + j];
                putc(hex_chars[(b >> 4) & 0xF]);
                putc(hex_chars[b & 0xF]);
                putc(' ');
            } else {
                puts("   ");
            }
        }
        puts(" |");

        // ASCII representation
        for (size_t j = 0; j < 16; ++j) {
            if (i + j < len) {
                char c = static_cast<char>(bytes[i + j]);
                putc((c >= 32 && c <= 126) ? c : '.');
            }
        }
        puts("|\n");
    }
}

uint32_t Trace::get_pos() noexcept {
    return s_pos;
}

} // namespace hal

/*
 * C Linkage Implementations
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

}
