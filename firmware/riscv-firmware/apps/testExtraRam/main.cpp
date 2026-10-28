/*
 * main.cpp - testExtraRam: the RAM the remap register gives the E906
 *
 * Target: Allwinner T527 / A523 XuanTie E906
 *
 * REMAP_CTRL_REG (R-PRCM 0x07010364, T527 manual V0.94 p.418):
 *   bit 0 MCU_RAM_REMAP      (reset 1): HiFi4 DSP local memory shared for the system
 *   bit 1 SRAMA3_2_RAM_REMAP (reset 0): SRAMA3_2 (512 KB, 0x07300000) shared for MCU_SYS;
 *                                       sunxi_rproc prepare() sets it
 *
 * 1. DSP local RAM, 0x00020000, 128 KB: the AbstractX e906_repl_app MicroPython GC
 *    heap. Every word, then byte and halfword stores read back as words.
 * 2. DRAM, the E906 carveout dram@48100000 (1 MiB, no-map in Linux, 1:1 for the
 *    core): the same checks. The firmware runs from SRAM, so all of it is free.
 * 3. SRAMA3_2, 0x07300000, 512 KB: the same checks. Probed last: an address the
 *    E906 cannot reach traps into the crash handler, whose autopsy (mcause, mtval)
 *    goes to trace0 after the "probing" line.
 *
 * Each region prints "EXTRA-RAM <name> PASS" or "EXTRA-RAM <name> FAIL ..." to trace0,
 * then the result is repeated with every heartbeat. A tested region is left zeroed
 * with a signature at its start (EXTRA_RAM_MAGIC, base, errors), which Linux reads
 * back through its own mapping to check that both sides see the same memory.
 */

#include <stdint.h>
#include "hal/trace.hpp"
#include "hal/timer.hpp"

namespace {

constexpr uintptr_t REMAP_CTRL_REG = 0x07010364;
constexpr uint32_t EXTRA_RAM_MAGIC = 0x45585452;	/* "EXTR" */

struct Region {
    const char *name;
    uintptr_t base;
    uint32_t size;
    int result;		/* -1 not run, 0 pass, >0 errors */
    uintptr_t first_bad;
    uint32_t first_got, first_want;
};

Region regions[] = {
    { "dsp-ram", 0x00020000, 128 * 1024, -1, 0, 0, 0 },
    { "dram", 0x48100000, 1024 * 1024, -1, 0, 0, 0 },
    { "sram-a3-2", 0x07300000, 512 * 1024, -1, 0, 0, 0 },
};

uint32_t pattern(uintptr_t addr, uint32_t seed)
{
    /* Address dependent, so an aliased or stuck address line reads back wrong */
    return (uint32_t)addr * 0x9E3779B1u ^ seed;
}

uint32_t errors;
uintptr_t first_bad;
uint32_t first_got, first_want;

void check(uintptr_t addr, uint32_t got, uint32_t want)
{
    if (got == want)
        return;
    if (!errors++) {
        first_bad = addr;
        first_got = got;
        first_want = want;
    }
}

/* Write the whole region first, then read it back: catches aliasing */
void word_pass(const Region &r, uint32_t seed)
{
    volatile uint32_t *p = (volatile uint32_t *)r.base;
    uint32_t n = r.size / 4;

    for (uint32_t i = 0; i < n; i++)
        p[i] = pattern(r.base + i * 4, seed);
    for (uint32_t i = 0; i < n; i++)
        check(r.base + i * 4, p[i], pattern(r.base + i * 4, seed));
}

/* Byte and halfword stores: the GC heap and MicroPython objects use every width */
void narrow_pass(const Region &r)
{
    volatile uint8_t *b = (volatile uint8_t *)r.base;
    volatile uint16_t *h = (volatile uint16_t *)r.base;
    volatile uint32_t *w = (volatile uint32_t *)r.base;
    uint32_t n = r.size / 4;

    for (uint32_t i = 0; i < r.size; i++)
        b[i] = (uint8_t)(i * 7 + 3);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = i * 4;
        uint32_t want = (uint32_t)(uint8_t)(j * 7 + 3) |
                        (uint32_t)(uint8_t)((j + 1) * 7 + 3) << 8 |
                        (uint32_t)(uint8_t)((j + 2) * 7 + 3) << 16 |
                        (uint32_t)(uint8_t)((j + 3) * 7 + 3) << 24;
        check(r.base + j, w[i], want);
    }

    for (uint32_t i = 0; i < r.size / 2; i++)
        h[i] = (uint16_t)(i ^ 0x5AA5);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t want = (uint32_t)(uint16_t)((2 * i) ^ 0x5AA5) |
                        (uint32_t)(uint16_t)((2 * i + 1) ^ 0x5AA5) << 16;
        check(r.base + i * 4, w[i], want);
    }
}

void report(const Region &r)
{
    if (r.result == 0)
        hal::Trace::printf("EXTRA-RAM %s PASS (0x%08x, %u bytes)\n",
                           r.name, (unsigned)r.base, (unsigned)r.size);
    else if (r.result > 0)
        hal::Trace::printf("EXTRA-RAM %s FAIL errors=%u first=0x%08x got=0x%08x want=0x%08x\n",
                           r.name, (unsigned)r.result, (unsigned)r.first_bad,
                           (unsigned)r.first_got, (unsigned)r.first_want);
}

void test_region(Region &r)
{
    hal::Trace::printf("EXTRA-RAM %s probing 0x%08x, %u bytes\n",
                       r.name, (unsigned)r.base, (unsigned)r.size);
    errors = 0;
    word_pass(r, 0x00000000u);
    word_pass(r, 0xFFFFFFFFu);
    narrow_pass(r);
    r.result = (int)errors;
    r.first_bad = first_bad;
    r.first_got = first_got;
    r.first_want = first_want;
    /* Leave it zeroed, as a fresh heap, with the signature for Linux */
    volatile uint32_t *w = (volatile uint32_t *)r.base;
    for (uint32_t i = 0; i < r.size / 4; i++)
        w[i] = 0;
    w[0] = EXTRA_RAM_MAGIC;
    w[1] = (uint32_t)r.base;
    w[2] = errors;
    report(r);
}

}  // namespace

int main(void)
{
    hal::Trace::init();
    hal::Timer::init();

    uint32_t remap = *(volatile uint32_t *)REMAP_CTRL_REG;
    hal::Trace::printf("\n[testExtraRam] REMAP_CTRL_REG = 0x%08x (bit 0 DSP RAM %s, bit 1 SRAMA3_2 %s)\n",
                       (unsigned)remap, (remap & 1) ? "shared" : "MCU only",
                       (remap & 2) ? "shared with MCU" : "not shared");

    for (Region &r : regions)
        test_region(r);

    for (uint32_t beat = 1;; beat++) {
        hal::Trace::printf("[testExtraRam] Heartbeat #%u\n", (unsigned)beat);
        for (const Region &r : regions)
            report(r);
        hal::Timer::delay_ms(2000);
    }
    return 0;
}
