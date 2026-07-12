/* spi.c - Core SPI peripheral implementation */
#include "spi.h"

#define REG(addr) (*(volatile uint32_t *)(addr))

void spi_init(uint32_t base_addr) {
    /* 1. Software reset the SPI block */
    REG(base_addr + SPI_GCR) = 0x80000000;
    while (REG(base_addr + SPI_GCR) & 0x80000000); /* Wait for reset to finish */

    /* 2. Configure master mode, enable SPI block */
    REG(base_addr + SPI_GCR) = 0x00000003;

    /* 3. CPOL=0, CPHA=0, SS control active */
    REG(base_addr + SPI_TCR) = 0x00000003;

    /* 4. Configure clock division rate (divide by 16 as standard baseline) */
    REG(base_addr + SPI_CCR) = 0x00001004;

    /* 5. Clear and reset FIFOs */
    REG(base_addr + SPI_FCR) = 0x80008000;
}

void spi_transfer(uint32_t base_addr, const uint8_t *tx_buf, uint8_t *rx_buf, uint32_t len) {
    REG(base_addr + SPI_BCR) = len;   /* Total burst bytes */
    REG(base_addr + SPI_TBCR) = len;  /* TX transfer bytes */
    
    /* Trigger the transfer by setting the Start (XCH) bit */
    REG(base_addr + SPI_TCR) |= (1U << 31);

    uint32_t tx_idx = 0;
    uint32_t rx_idx = 0;

    while (tx_idx < len || rx_idx < len) {
        uint32_t fsr = REG(base_addr + SPI_FSR);
        
        /* Check TX FIFO space */
        uint32_t tx_cnt = (fsr >> 16) & 0xFF;
        uint32_t tx_free = (64 > tx_cnt) ? (64 - tx_cnt) : 0;

        while (tx_free > 0 && tx_idx < len) {
            REG(base_addr + SPI_TXD) = tx_buf ? tx_buf[tx_idx] : 0xFF;
            tx_idx++;
            tx_free--;
        }

        /* Check RX FIFO available data */
        uint32_t rx_cnt = fsr & 0xFF;
        while (rx_cnt > 0 && rx_idx < len) {
            uint32_t data = REG(base_addr + SPI_RXD);
            if (rx_buf) {
                rx_buf[rx_idx] = (uint8_t)data;
            }
            rx_idx++;
            rx_cnt--;
        }
    }
}
