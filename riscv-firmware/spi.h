/* spi.h - Allwinner Sunxi SPI register mapping and drivers */
#ifndef SPI_H
#define SPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPI0_BASE 0x04025000
#define SPI1_BASE 0x04026000

/* Register offsets */
#define SPI_GCR       0x04   /* Global Control Register */
#define SPI_TCR       0x08   /* Transfer Control Register */
#define SPI_IER       0x0C   /* Interrupt Enable Register */
#define SPI_ISR       0x10   /* Interrupt Status Register */
#define SPI_FCR       0x14   /* FIFO Control Register */
#define SPI_FSR       0x18   /* FIFO Status Register */
#define SPI_CCR       0x1C   /* Clock Configuration Register */
#define SPI_BCR       0x30   /* Burst Counter Register */
#define SPI_TBCR      0x34   /* Transmit Burst Counter Register */
#define SPI_TXD       0x200  /* TX Data FIFO (varies by IP, often at 0x200 on modern sunxi) */
#define SPI_RXD       0x300  /* RX Data FIFO (varies by IP, often at 0x300 on modern sunxi) */

void spi_init(uint32_t base_addr);
void spi_transfer(uint32_t base_addr, const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // SPI_H
