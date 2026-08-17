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
#define DTCM_PHYS_BASE    0x07120000
#define DTCM_MAP_SIZE     0x1000

static const char *LOADER_VERSION = "1.1.0";

static void usage(const char *prog) {
    printf("Usage: %s {start|stop|status|monitor|restart|version} [path_to_firmware.bin]\n", prog);
    printf("%s loader version %s\n", prog, LOADER_VERSION);
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
        if (rst & ((1 << 17) | (1 << 16))) {
            printf("Status: RUNNING (Core active)\n");
            volatile uint32_t *dtcm = (volatile uint32_t *)mmap(NULL, DTCM_MAP_SIZE, PROT_READ, MAP_SHARED, fd, DTCM_PHYS_BASE);
            if (dtcm != MAP_FAILED) {
                printf("--- Live Telemetry Block (Host 0x07120000 / E907 DTCM 0x00080000) ---\n");
                printf("  Magic Header (0x07120000):  0x%08X (%s)\n", dtcm[0], (dtcm[0] == 0x52495343) ? "RISC" : "Unset");
                printf("  Booted Flag  (0x07120004):  %u\n", dtcm[1]);
                printf("  Heartbeat    (0x07120008):  %u\n", dtcm[2]);
                printf("  Loop Counter (0x0712000C):  %u (0x%08X)\n", dtcm[3], dtcm[3]);
                munmap((void *)dtcm, DTCM_MAP_SIZE);
            }
        } else {
            printf("Status: HALTED (In reset)\n");
        }
        munmap((void *)ccu_virt, CCU_MAP_SIZE);
        close(fd);
        return 0;
    } else if (strcmp(action, "monitor") == 0) {
        volatile uint32_t *dtcm = (volatile uint32_t *)mmap(NULL, DTCM_MAP_SIZE, PROT_READ, MAP_SHARED, fd, DTCM_PHYS_BASE);
        if (dtcm == MAP_FAILED) {
            perror("Failed to mmap DTCM telemetry space (0x07120000)");
            munmap((void *)ccu_virt, CCU_MAP_SIZE);
            close(fd);
            return 1;
        }
        printf("=== Live XuanTie E907 Telemetry Monitor (Press Ctrl+C to stop) ===\n");
        printf("Reading DTCM at physical 0x07120000...\n\n");
        for (int i = 0; i < 30; i++) {
            uint32_t magic = dtcm[0];
            uint32_t boot = dtcm[1];
            uint32_t hb = dtcm[2];
            uint32_t loops = dtcm[3];
            printf("\r[E907 Live] Magic: 0x%08X | Boot: %u | Heartbeat: %-6u | Loop Counter: %-10u", magic, boot, hb, loops);
            fflush(stdout);
            usleep(200000); /* 200 ms */
        }
        printf("\n\nMonitor finished 30 samples.\n");
        munmap((void *)dtcm, DTCM_MAP_SIZE);
        munmap((void *)ccu_virt, CCU_MAP_SIZE);
        close(fd);
        return 0;
    } else if (strcmp(action, "version") == 0) {
        printf("riscv-load version %s\n", LOADER_VERSION);
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

        /* 2. Assert Core Reset & Enable Subsystem Bus (bit 16=1, bit 17=0) */
        printf("Asserting RISC-V core reset...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) = (1 << 16);

        /* 3. Read firmware binary into local host RAM buffer */
        uint8_t fw_buf[ITCM_MAP_SIZE];
        memset(fw_buf, 0, sizeof(fw_buf));
        size_t n = fread(fw_buf, 1, sizeof(fw_buf), fw);
        fclose(fw);
        if (n == 0) {
            fprintf(stderr, "Error: Firmware file '%s' is empty!\n", fw_path);
            munmap((void *)ccu_virt, CCU_MAP_SIZE);
            close(fd);
            return 1;
        }

        /* 4. Map ITCM and Copy Firmware via 32-bit aligned MMIO writes */
        volatile uint32_t *itcm_virt32 = (volatile uint32_t *)mmap(NULL, ITCM_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ITCM_PHYS_BASE);
        if (itcm_virt32 == MAP_FAILED) {
            perror("Failed to mmap ITCM memory");
            munmap((void *)ccu_virt, CCU_MAP_SIZE);
            close(fd);
            return 1;
        }

        printf("Copying %zu bytes to ITCM (0x07110000)...\n", n);
        uint32_t *src32 = (uint32_t *)fw_buf;
        size_t words = (n + 3) / 4;
        for (size_t i = 0; i < words; i++) {
            itcm_virt32[i] = src32[i];
        }
        printf("Copied %zu bytes into ITCM successfully.\n", n);
        munmap((void *)itcm_virt32, ITCM_MAP_SIZE);

        /* 5. Release Core Reset (Boot XuanTie E907 at 0x00000000) */
        printf("Releasing reset (Booting XuanTie E907 at 0x00000000)...\n");
        *(volatile uint32_t *)(ccu_virt + CCU_DSP_RST_REG) = (1 << 17) | (1 << 16);
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
