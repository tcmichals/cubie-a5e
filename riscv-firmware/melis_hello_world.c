/*
 * melis_hello_world.c - Bare-Metal UART "Hello World" Reference Example
 *
 * This example shows how to configure the standard Allwinner 8250-compatible
 * UART0 peripheral to print boot telemetry strings from the XuanTie RISC-V core.
 */

#include <stdint.h>

#define UART0_BASE  0x02500000  /* UART0 Physical register base */

/* UART 8250 Register Offsets */
#define UART_THR    0x00        /* Transmit Holding Register (Write Only) */
#define UART_LSR    0x14        /* Line Status Register (Read Only) */
#define UART_LSR_THRE (1 << 5)  /* Transmit Holding Register Empty Bit */

/* Send a single character over the UART0 port */
void uart0_putc(char c) {
    /* Wait until the Transmit FIFO/Holding Register is empty and ready */
    while (!(*(volatile uint32_t *)(UART0_BASE + UART_LSR) & UART_LSR_THRE));
    
    /* Write character to register */
    *(volatile uint32_t *)(UART0_BASE + UART_THR) = c;
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

/* Hello World entry point */
void hello_world_main(void) {
    /* Initialize UART0 (Baud rate is pre-configured to 115200 by U-Boot) */
    
    uart0_puts("========================================\n");
    uart0_puts(" Hello World from XuanTie RISC-V Core!  \n");
    uart0_puts(" Running bare-metal on Radxa Cubie A5E  \n");
    uart0_puts("========================================\n");
    
    while (1) {
        /* Hello world execution loop */
    }
}
