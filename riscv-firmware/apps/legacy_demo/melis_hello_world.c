/*
 * melis_hello_world.c - Bare-Metal UART "Hello World" Reference Example
 *
 * This example configures the standard Allwinner 8250-compatible
 * UART0 peripheral to print boot telemetry strings from the XuanTie RISC-V core.
 */

#include <stdint.h>
#include "uart0.h"

#define UART0_BASE    0x02500000  /* UART0 Physical register base */

/* UART 8250 Register Offsets */
#define UART_THR      0x00        /* Transmit Holding Register (Write Only) */
#define UART_LSR      0x14        /* Line Status Register (Read Only) */
#define UART_LSR_THRE (1 << 5)    /* Transmit Holding Register Empty Bit */

/* Send a single character over the UART0 port */
void uart0_putc(char c) {
    /* Wait until the Transmit FIFO/Holding Register is empty and ready with timeout safety */
    volatile uint32_t timeout = 100000;
    while (!(*(volatile uint32_t *)(UART0_BASE + UART_LSR) & UART_LSR_THRE) && --timeout);
    
    /* Write character to register */
    *(volatile uint32_t *)(UART0_BASE + UART_THR) = (uint32_t)(uint8_t)c;
}

/* Send a null-terminated string over the UART0 port */
void uart0_puts(const char *str) {
    while (*str) {
        if (*str == '\n') {
            uart0_putc('\r'); /* Format newline carriage return */
        }
        uart0_putc(*str++);
    }
}
