/*
 * host_coprocessor_example.c - ARM Linux Host Co-processor Control Example
 *
 * This example runs on the ARM Cortex-A55 Linux host. It demonstrates:
 *  1. Mapping CCU clock/reset and SRAM blocks using /dev/mem and mmap.
 *  2. Asserting reset, loading the firmware payload, and booting the RISC-V core.
 *  3. Bidirectional mailbox doorbell exchange via memory-mapped msgbox registers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define CCU_PHYS_BASE     0x07010000
#define CCU_MAP_SIZE      0x1000

#define CCU_DSP_CLK_REG   0x0020
#define CCU_DSP_RST_REG   0x0100

#define ITCM_PHYS_BASE    0x07110000
#define ITCM_MAP_SIZE     0x10000  /* 64 KB */

#define MBOX_PHYS_BASE    0x03003000
#define MBOX_MAP_SIZE     0x1000

#define MBOX_LOCAL_IRQ_EN    0x0060
#define MBOX_LOCAL_IRQ_STAT  0x0070
#define MBOX_MSG_STAT(n)     (0x0140 + 0x4 * (n))
#define MBOX_MSG_DATA(n)     (0x0180 + 0x4 * (n))

/* Global virtual memory pointers */
static volatile uint8_t *ccu_virt = NULL;
static volatile uint8_t *itcm_virt = NULL;
static volatile uint8_t *mbox_virt = NULL;

int init_memory_mappings(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Failed to open /dev/mem (requires root/sudo)");
        return -1;
    }

    ccu_virt = mmap(NULL, CCU_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CCU_PHYS_BASE);
    if (ccu_virt == MAP_FAILED) {
        perror("Failed to map CCU");
        close(fd);
        return -1;
    }

    itcm_virt = mmap(NULL, ITCM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ITCM_PHYS_BASE);
    if (itcm_virt == MAP_FAILED) {
        perror("Failed to map ITCM");
        close(fd);
        return -1;
    }

    mbox_virt = mmap(NULL, MBOX_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, MBOX_PHYS_BASE);
    if (mbox_virt == MAP_FAILED) {
        perror("Failed to map Mailbox");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

void coprocessor_boot_sequence(const char *fw_path) {
    FILE *fw = fopen(fw_path, "rb");
    if (!fw) {
        perror("Failed to open firmware file");
        exit(1);
    }

    printf("[Host] Step 1: Asserting reset on XuanTie RISC-V core...\n");
    /* Write 0 to Reset Register (assert reset) */
    *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) &= ~(1 << 17);

    printf("[Host] Step 2: Enabling MCU/DSP subsystem clocks...\n");
    *(volatile uint32_t *)(ccu_virt + CCU_DSP_CLK_REG) |= 0x00000003;

    printf("[Host] Step 3: Copying firmware binary payload into ITCM...\n");
    memset((void *)itcm_virt, 0, ITCM_MAP_SIZE);
    size_t bytes_read = fread((void *)itcm_virt, 1, ITCM_MAP_SIZE, fw);
    printf("[Host] Copied %zu bytes into co-processor memory.\n", bytes_read);
    fclose(fw);

    printf("[Host] Step 4: Releasing core reset (Booting XuanTie!)...\n");
    /* Set Bit 17 of MCU reset register to 1 (release reset) */
    *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) |= (1 << 17) | (1 << 16);
    printf("[Host] Core is now executing.\n");
}

void mailbox_send_cmd(uint32_t cmd) {
    printf("[Host] Sending command 0x%08X to RISC-V on Channel 0...\n", cmd);
    *(volatile uint32_t *)(mbox_virt + MBOX_MSG_DATA(0)) = cmd;
}

void mailbox_poll_ack(void) {
    printf("[Host] Polling for acknowledgement from RISC-V on Channel 1...\n");
    while (1) {
        uint32_t status = *(volatile uint32_t *)(mbox_virt + MBOX_MSG_STAT(1));
        if ((status & 0x7) > 0) { /* FIFO has elements */
            uint32_t ack = *(volatile uint32_t *)(mbox_virt + MBOX_MSG_DATA(1));
            printf("[Host] Received ack payload: 0x%08X\n", ack);
            break;
        }
        usleep(1000); /* 1 ms poll delay */
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path_to_firmware.bin>\n", argv[0]);
        return 1;
    }

    if (init_memory_mappings() < 0) {
        return 1;
    }

    /* Boot the co-processor */
    coprocessor_boot_sequence(argv[1]);

    /* Send simple handshake command 0x100 and wait for incremented echo ack (0x101) */
    sleep(1);
    mailbox_send_cmd(0x00000100);
    mailbox_poll_ack();

    return 0;
}
