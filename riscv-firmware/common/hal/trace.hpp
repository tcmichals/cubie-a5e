#ifndef HAL_TRACE_HPP
#define HAL_TRACE_HPP

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "include/resource_table.h"

namespace hal {

class Trace {
public:
    // Initialize trace buffer in memory and optional serial console mirror
    static void init(bool enable_serial_mirror = false) noexcept;

    // Direct character and string outputs
    static void putc(char c) noexcept;
    static void puts(const char *str) noexcept;
    static void write(const void *data, size_t len) noexcept;

    // Formatted numeric outputs
    static void print_uint(uint32_t val) noexcept;
    static void print_int(int32_t val) noexcept;
    static void print_hex(uint32_t val, bool prefix = true) noexcept;
    static void print_float(float val, int decimals = 3) noexcept;

    // Zero-allocation lightweight printf engine
    static void printf(const char *fmt, ...) noexcept;
    static void vprintf(const char *fmt, va_list args) noexcept;

    // Formatted Hex/ASCII Memory Dump
    static void dump_hex(const void *addr, size_t len, uint32_t base_addr = 0) noexcept;

    // Buffer position query
    static uint32_t get_pos() noexcept;

private:
    static volatile uint32_t s_pos;
    static bool s_serial_mirror;
};

} // namespace hal

/*
 * C Linkage Wrappers for C Source Files
 */
#ifdef __cplusplus
extern "C" {
#endif

void trace_init(void);
void trace_putc(char c);
void trace_puts(const char *s);
void trace_printf(const char *fmt, ...);
void trace_put_uint(uint32_t val);
void trace_put_hex(uint32_t val);
void trace_put_float(float val, int decimals);

#ifdef __cplusplus
}
#endif

#endif // HAL_TRACE_HPP
