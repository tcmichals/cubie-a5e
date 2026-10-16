/*
 * main.cpp - testMsgboxStep: message box FIFO popped one step at a time
 *
 * Lets Linux observe the write-IRQ status of the E906's message box bank at
 * every fill level. The E906 never touches the FIFO on its own (no
 * MsgBox::init(), which would drain it): it pops exactly one message from the
 * requested channel when Linux asks, and records the FIFO count before and
 * after plus the value in a command block in SRAM.
 *
 * Command block (SRAM, host 0x072B0000 / core 0x3FFF0000), all 32-bit:
 *   +0x00 magic     E906: 0x53544550 ("STEP") once ready
 *   +0x04 cmd_seq   Linux: increment to issue a command
 *   +0x08 done_seq  E906: set to cmd_seq when the command is done
 *   +0x0c channel   Linux: E906 channel p (0..3) of the ARM link (port 2)
 *   +0x10 before    E906: message count before the pop
 *   +0x14 after     E906: message count after the pop
 *   +0x18 value     E906: the value popped (0 if the FIFO was empty)
 */

#include <stdint.h>
#include "hal/trace.hpp"

namespace {

constexpr uintptr_t kBlock = 0x3FFF0000u;
constexpr uintptr_t kPort = 0x07136000u + 0x200u;   // E906 bank, port 2 = ARM link
constexpr uint32_t kMagic = 0x53544550u;            // "STEP"

volatile uint32_t &word(uintptr_t a) { return *reinterpret_cast<volatile uint32_t *>(a); }
volatile uint32_t &msg_status(uint32_t p) { return word(kPort + 0x60 + 4 * p); }
volatile uint32_t &msg_fifo(uint32_t p) { return word(kPort + 0x70 + 4 * p); }

} // namespace

int main(void)
{
    hal::Trace::init();
    hal::Trace::puts("[testMsgboxStep] ready: pops one message per command\n");

    word(kBlock + 0x08) = word(kBlock + 0x04);   // nothing pending at start
    asm volatile("fence rw, rw" ::: "memory");
    word(kBlock + 0x00) = kMagic;

    uint32_t done = word(kBlock + 0x08);
    while (1) {
        uint32_t seq = word(kBlock + 0x04);
        if (seq == done) {
            continue;
        }
        asm volatile("fence rw, rw" ::: "memory");
        uint32_t p = word(kBlock + 0x0c) & 3;
        uint32_t before = msg_status(p) & 0xf;
        uint32_t value = before ? msg_fifo(p) : 0;
        uint32_t after = msg_status(p) & 0xf;
        word(kBlock + 0x10) = before;
        word(kBlock + 0x14) = after;
        word(kBlock + 0x18) = value;
        asm volatile("fence rw, rw" ::: "memory");
        word(kBlock + 0x08) = seq;
        done = seq;
    }
    return 0;
}
