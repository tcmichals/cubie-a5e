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

static void usage(const char *prog) {
    printf("Usage: %s {start|stop|status|restart} [path_to_firmware.bin]\n", prog);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    const char *action = argv[1];
    const char *fw_path = (argc >= 3) ? argv[2] : "/lib/firmware/riscv-firmware.bin";

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Failed to open /dev/mem (requires root privileges)");
        return 1;
    }

    volatile uint8_t *ccu_virt = (volatile uint8_t *)mmap(NULL, CCU_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, CCU_PHYS_BASE);
    if (ccu_virt == MAP_FAILED) {
        perror("Failed to mmap CCU registers");
        close(fd);
        return 1;
    }

    if (strcmp(action, "status") == 0) {
        uint32_t clk = *(volatile uint32_t *)(ccu_virt + CCU_DSP_CLK_REG);
        uint32_t rst = *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG);
        printf("MCU Bus Clock Reg (0x07010020): 0x%08X\n", clk);
        printf("MCU Reset Reg     (0x07010100): 0x%08X\n", rst);
        if (rst & (1 << 17)) {
            printf("Status: RUNNING (Core active)\n");
        } else {
            printf("Status: HALTED (In reset)\n");
        }
        munmap((void *)ccu_virt, CCU_MAP_SIZE);
        close(fd);
        return 0;
    }

    if (strcmp(action, "stop") == 0) {
        printf("Halting XuanTie E907 RISC-V co-processor...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) &= ~(1 << 17);
        printf("RISC-V core is held in reset.\n");
        munmap((void *)ccu_virt, CCU_MAP_SIZE);
        close(fd);
        return 0;
    }

    if (strcmp(action, "start") == 0 || strcmp(action, "restart") == 0) {
        FILE *fw = fopen(fw_path, "rb");
        if (!fw) {
            fprintf(stderr, "Error: Cannot open firmware file '%s'\n", fw_path);
            munmap((void *)ccu_virt, CCU_MAP_SIZE);
            close(fd);
            return 1;
        }

        printf("=== Loading XuanTie E907 RISC-V Firmware ===\n");

        /* 1. Enable MCU/DSP Clocks */
        printf("Enabling MCU subsystem clocks (CCU 0x07010020 -> 0x03)...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_CLK_REG) |= 0x00000003;

        /* 2. Assert Core Reset */
        printf("Asserting RISC-V core reset...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) &= ~(1 << 17);

        /* 3. Map ITCM and Copy Firmware via mmap */
        volatile uint8_t *itcm_virt = (volatile uint8_t *)mmap(NULL, ITCM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ITCM_PHYS_BASE);
        if (itcm_virt == MAP_FAILED) {
            perror("Failed to mmap ITCM memory");
            fclose(fw);
            munmap((void *)ccu_virt, CCU_MAP_SIZE);
            close(fd);
            return 1;
        }

        printf("Copying %s to ITCM (0x07110000)...\n", fw_path);
        memset((void *)itcm_virt, 0, ITCM_MAP_SIZE);
        size_t n = fread((void *)itcm_virt, 1, ITCM_MAP_SIZE, fw);
        printf("Copied %zu bytes into ITCM.\n", n);
        fclose(fw);
        munmap((void *)itcm_virt, ITCM_MAP_SIZE);

        /* 4. Release Reset */
        printf("Releasing reset (Booting XuanTie E907 at 0x00000000)...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) |= (1 << 17) | (1 << 16);
        printf("XuanTie E907 RISC-V co-processor is running.\n");

        munmap((void *)ccu_virt, CCU_MAP_SIZE);
        close(fd);
        return 0;
    }

    usage(argv[0]);
    munmap((void *)ccu_virt, CCU_MAP_SIZE);
    close(fd);
    return 1;
}
