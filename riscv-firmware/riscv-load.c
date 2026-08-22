#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#define MAIN_CCU_PHYS_BASE 0x02001000
#define MAIN_CCU_MAP_SIZE  0x1000

#define SYS_CFG_PHYS_BASE  0x03000000
#define SYS_CFG_MAP_SIZE   0x1000

#define MCU_CCU_PHYS_BASE  0x07102000
#define MCU_CCU_MAP_SIZE   0x1000

#define SRAM_PHYS_BASE     0x00020000
#define SRAM_MAP_SIZE      0x20000   /* 128 KB */
#define TELEM_OFFSET       0x00008000 /* 0x00028000 */

static const char *LOADER_VERSION = "2.0.0";

static void usage(const char *prog) {
    printf("Usage: %s {start|stop|status|monitor|restart|version} [path_to_firmware.bin]\n", prog);
    printf("%s loader version %s (Built %s %s)\n", prog, LOADER_VERSION, __DATE__, __TIME__);
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

    volatile uint8_t *main_ccu = (volatile uint8_t *)mmap(NULL, MAIN_CCU_MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, MAIN_CCU_PHYS_BASE);
    volatile uint8_t *sys_cfg  = (volatile uint8_t *)mmap(NULL, SYS_CFG_MAP_SIZE,  PROT_READ | PROT_WRITE, MAP_SHARED, fd, SYS_CFG_PHYS_BASE);
    volatile uint8_t *mcu_ccu  = (volatile uint8_t *)mmap(NULL, MCU_CCU_MAP_SIZE,  PROT_READ | PROT_WRITE, MAP_SHARED, fd, MCU_CCU_PHYS_BASE);
    volatile uint8_t *sram     = (volatile uint8_t *)mmap(NULL, SRAM_MAP_SIZE,     PROT_READ | PROT_WRITE, MAP_SHARED, fd, SRAM_PHYS_BASE);

    if (main_ccu == MAP_FAILED || sys_cfg == MAP_FAILED || mcu_ccu == MAP_FAILED || sram == MAP_FAILED) {
        perror("Failed to mmap MMIO regions");
        close(fd);
        return 1;
    }

    if (strcmp(action, "status") == 0) {
        uint32_t clk_dsp = *(volatile uint32_t *)(main_ccu + 0xc70);
        uint32_t rst_dsp = *(volatile uint32_t *)(main_ccu + 0xc7c);
        uint32_t sram_mux = *(volatile uint32_t *)(sys_cfg + 0x004);
        uint32_t riscv_clk = *(volatile uint32_t *)(mcu_ccu + 0x120);
        uint32_t riscv_rst = *(volatile uint32_t *)(mcu_ccu + 0x124);

        printf("=== XuanTie RISC-V Hardware Status ===\n");
        printf("Main CCU DSP Clk (0x02001c70): 0x%08X (%s)\n", clk_dsp, (clk_dsp & (1<<31)) ? "ON" : "OFF");
        printf("Main CCU DSP Bus (0x02001c7c): 0x%08X (%s)\n", rst_dsp, (rst_dsp & 1) ? "ON" : "OFF");
        printf("SYS_CFG SRAM MUX (0x03000004): 0x%08X (Mapped to %s)\n", sram_mux, (sram_mux & (1<<24)) ? "ARM Host" : "RISC-V Subsystem");
        printf("MCU CCU Core Clk (0x07102120): 0x%08X (%s)\n", riscv_clk, (riscv_clk & (1<<31)) ? "RUNNING" : "STOPPED");
        printf("MCU CCU Core Rst (0x07102124): 0x%08X (Core %s)\n", riscv_rst, (riscv_rst & (1<<18)) ? "ACTIVE" : "IN RESET");

        volatile uint32_t *telem = (volatile uint32_t *)(sram + TELEM_OFFSET);
        printf("\n--- Shared SRAM C Telemetry (0x00028000) ---\n");
        printf("  Magic:        0x%08X (%s)\n", telem[0], (telem[0] == 0x52495343) ? "RISC" : "Unset");
        printf("  Boot Flag:    %u\n", telem[1]);
        printf("  Heartbeat:    %u\n", telem[2]);
        printf("  Loop Counter: %u (0x%08X)\n", telem[3], telem[3]);
        printf("  Status Magic: 0x%08X (%s)\n", telem[4], (telem[4] == 0x414C4956) ? "ALIV" : "Unset");
        printf("  Trap Count:   %u\n", telem[8]);
        return 0;
    } else if (strcmp(action, "stop") == 0) {
        printf("Halting XuanTie RISC-V co-processor...\n");
        *(volatile uint32_t *)(mcu_ccu + 0x124) = 0x00030001;
        printf("Core held in reset.\n");
        return 0;
    } else if (strcmp(action, "monitor") == 0) {
        volatile uint32_t *telem = (volatile uint32_t *)(sram + TELEM_OFFSET);
        printf("=== Live XuanTie RISC-V Telemetry Monitor (0x00028000) ===\n");
        for (int i = 0; i < 30; i++) {
            printf("\r[E907 Live] Magic: 0x%08X | Boot: %u | Heartbeat: %-6u | Loops: %-10u | Traps: %u",
                   telem[0], telem[1], telem[2], telem[3], telem[8]);
            fflush(stdout);
            usleep(200000);
        }
        printf("\n\nMonitor finished 30 samples.\n");
        return 0;
    } else if (strcmp(action, "start") == 0 || strcmp(action, "restart") == 0) {
        FILE *fw = fopen(fw_path, "rb");
        if (!fw) {
            fprintf(stderr, "Error: Cannot open firmware file '%s'\n", fw_path);
            return 1;
        }

        printf("=== Loading XuanTie RISC-V Firmware ===\n");

        /* 1. Enable Main CCU DSP Root Clock and Bus */
        *(volatile uint32_t *)(main_ccu + 0xc70) = 0x80000000;
        *(volatile uint32_t *)(main_ccu + 0xc7c) = 0x00010001;

        /* 2. Enable MCU CCU and Peripheral Buses */
        *(volatile uint32_t *)(mcu_ccu + 0x108) = 0x00010001; /* TZMA0 */
        *(volatile uint32_t *)(mcu_ccu + 0x10c) = 0x00010001; /* TZMA1 */
        *(volatile uint32_t *)(mcu_ccu + 0x114) = 0x00010001; /* PubSRAM */
        *(volatile uint32_t *)(mcu_ccu + 0x11c) = 0x00000003; /* MBUS */
        *(volatile uint32_t *)(mcu_ccu + 0x120) = 0x80000000; /* RISC-V Clock */
        *(volatile uint32_t *)(mcu_ccu + 0x128) = 0x00010001; /* Mailbox */

        /* 3. Hold RISC-V in reset */
        *(volatile uint32_t *)(mcu_ccu + 0x124) = 0x00030001;

        /* 4. Ensure SRAM C is mapped to ARM CPU for writing */
        *(volatile uint32_t *)(sys_cfg + 0x004) |= (1 << 24);

        /* 5. Load Firmware into SRAM C */
        uint8_t fw_buf[SRAM_MAP_SIZE];
        memset(fw_buf, 0, sizeof(fw_buf));
        size_t n = fread(fw_buf, 1, sizeof(fw_buf), fw);
        fclose(fw);

        if (n == 0) {
            fprintf(stderr, "Error: Firmware file '%s' is empty!\n", fw_path);
            return 1;
        }

        printf("Writing %zu bytes into SRAM C (0x00020000)...\n", n);
        volatile uint32_t *sram32 = (volatile uint32_t *)sram;
        uint32_t *src32 = (uint32_t *)fw_buf;
        size_t words = (n + 3) / 4;
        for (size_t i = 0; i < words; i++) {
            sram32[i] = src32[i];
        }

        uint32_t first = sram32[0];
        uint32_t magic = sram32[1];
        printf("Copied %zu bytes (First: 0x%08X, eGON: 0x%08X).\n", n, first, magic);

        /* 6. Release RISC-V Reset */
        printf("Releasing RISC-V core reset...\n");
        *(volatile uint32_t *)(mcu_ccu + 0x124) = 0x00070001;
        uint32_t rst_read = *(volatile uint32_t *)(mcu_ccu + 0x124);
        printf("Reset status (0x07102124): 0x%08X (Core active)\n", rst_read);
        printf("XuanTie RISC-V co-processor is running.\n");
        return 0;
    }

    usage(argv[0]);
    return 1;
}
