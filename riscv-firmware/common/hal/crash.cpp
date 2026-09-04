#include "crash.hpp"
#include "trace.hpp"

#define SRAM_CRASH_DUMP_LOC ((volatile uint32_t *)0x07130000UL)

namespace hal {

const char *CrashHandler::get_cause_name(uint32_t mcause) noexcept {
    if (mcause & (1UL << 31)) {
        return "Interrupt (Asynchronous)";
    }
    switch (mcause & 0x1F) {
        case 0:  return "Instruction address misaligned";
        case 1:  return "Instruction access fault";
        case 2:  return "Illegal instruction";
        case 3:  return "Breakpoint (EBREAK)";
        case 4:  return "Load address misaligned";
        case 5:  return "Load access fault";
        case 6:  return "Store/AMO address misaligned";
        case 7:  return "Store/AMO access fault";
        case 8:  return "Environment call from U-mode";
        case 11: return "Environment call from M-mode";
        case 12: return "Instruction page fault";
        case 13: return "Load page fault";
        case 15: return "Store/AMO page fault";
        default: return "Reserved / Unknown Trap";
    }
}

void CrashHandler::handle(const CrashFrame &frame) noexcept {
    // 1. Write fatal signature to Dedicated MCU SRAM C (0x07130000)
    SRAM_CRASH_DUMP_LOC[0] = 0xDEADF00D; // Fatal crash magic
    SRAM_CRASH_DUMP_LOC[1] = frame.mepc;
    SRAM_CRASH_DUMP_LOC[2] = frame.mcause;
    SRAM_CRASH_DUMP_LOC[3] = frame.mtval;

    // 2. Output rich autopsy report to hal::Trace (trace0 buffer + S_UART0)
    Trace::puts("\n\n");
    Trace::puts("################################################################\n");
    Trace::puts("  FATAL HARDWARE EXCEPTION DETECTED ON XUANTIE E907 RISC-V CORE \n");
    Trace::puts("################################################################\n");
    
    Trace::printf("  Cause Name : %s\n", get_cause_name(frame.mcause));
    Trace::printf("  mcause     : 0x%08x\n", frame.mcause);
    Trace::printf("  mepc (PC)  : 0x%08x\n", frame.mepc);
    Trace::printf("  mtval      : 0x%08x\n", frame.mtval);
    Trace::printf("  mstatus    : 0x%08x\n\n", frame.mstatus);

    Trace::puts("--- General Purpose Register (GPR) Dump ---\n");
    Trace::printf("  ra (x1) = 0x%08x  sp (x2) = 0x%08x  gp (x3) = 0x%08x\n", frame.ra, frame.sp, frame.gp);
    Trace::printf("  tp (x4) = 0x%08x  t0 (x5) = 0x%08x  t1 (x6) = 0x%08x\n", frame.tp, frame.t0, frame.t1);
    Trace::printf("  t2 (x7) = 0x%08x  s0 (x8) = 0x%08x  s1 (x9) = 0x%08x\n", frame.t2, frame.s0, frame.s1);
    Trace::printf("  a0 (x10)= 0x%08x  a1 (x11)= 0x%08x  a2 (x12)= 0x%08x\n", frame.a0, frame.a1, frame.a2);
    Trace::printf("  a3 (x13)= 0x%08x  a4 (x14)= 0x%08x  a5 (x15)= 0x%08x\n", frame.a3, frame.a4, frame.a5);
    Trace::printf("  a6 (x16)= 0x%08x  a7 (x17)= 0x%08x  s2 (x18)= 0x%08x\n", frame.a6, frame.a7, frame.s2);
    Trace::printf("  s3 (x19)= 0x%08x  s4 (x20)= 0x%08x  s5 (x21)= 0x%08x\n", frame.s3, frame.s4, frame.s5);
    Trace::printf("  s6 (x22)= 0x%08x  s7 (x23)= 0x%08x  s8 (x24)= 0x%08x\n", frame.s6, frame.s7, frame.s8);
    Trace::printf("  s9 (x25)= 0x%08x  s10(x26)= 0x%08x  s11(x27)= 0x%08x\n", frame.s9, frame.s10, frame.s11);
    Trace::printf("  t3 (x28)= 0x%08x  t4 (x29)= 0x%08x  t5 (x30)= 0x%08x\n", frame.t3, frame.t4, frame.t5);
    Trace::printf("  t6 (x31)= 0x%08x\n", frame.t6);

    Trace::puts("################################################################\n");
    Trace::puts("  Core halted safely. Inspect /sys/.../trace0 or SRAM 0x07130000 \n");
    Trace::puts("################################################################\n\n");
}

} // namespace hal

extern "C" void hal_crash_dispatcher(const hal::CrashFrame *frame) {
    if (frame) {
        hal::CrashHandler::handle(*frame);
    }
}
