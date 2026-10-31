/*
 * atomics.c - Software atomic primitives for RISC-V cores without hardware 'A' extension
 * (e.g. XuanTie E902 RV32EMC)
 */

#include <stdint.h>
#include <stdbool.h>

#if defined(__riscv_32e) || !defined(__riscv_atomic)

unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder)
{
    (void)memorder;
    unsigned int mstatus, old;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));
    old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old + val;
    if (mstatus & 8)
        __asm__ volatile ("csrs mstatus, 8");
    return old;
}

unsigned int __atomic_fetch_sub_4(volatile void *ptr, unsigned int val, int memorder)
{
    (void)memorder;
    unsigned int mstatus, old;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));
    old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old - val;
    if (mstatus & 8)
        __asm__ volatile ("csrs mstatus, 8");
    return old;
}

unsigned int __atomic_exchange_4(volatile void *ptr, unsigned int val, int memorder)
{
    (void)memorder;
    unsigned int mstatus, old;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));
    old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = val;
    if (mstatus & 8)
        __asm__ volatile ("csrs mstatus, 8");
    return old;
}

bool __atomic_compare_exchange_4(volatile void *ptr, void *expected, unsigned int desired,
                                bool weak, int success_memorder, int failure_memorder)
{
    (void)weak; (void)success_memorder; (void)failure_memorder;
    unsigned int mstatus, current, exp;
    bool match;

    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));
    current = *(volatile unsigned int *)ptr;
    exp = *(unsigned int *)expected;
    if (current == exp) {
        *(volatile unsigned int *)ptr = desired;
        match = true;
    } else {
        *(unsigned int *)expected = current;
        match = false;
    }
    if (mstatus & 8)
        __asm__ volatile ("csrs mstatus, 8");
    return match;
}

#endif
