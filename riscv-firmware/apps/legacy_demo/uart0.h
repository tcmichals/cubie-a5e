#ifndef UART0_H
#define UART0_H

#ifdef __cplusplus
extern "C" {
#endif

void uart0_putc(char c);
void uart0_puts(const char *str);

#ifdef __cplusplus
}
#endif

#endif /* UART0_H */
