/*
 * e907_ddr.ld - SRAM A3 & DDR Linker Script for Allwinner T527 / A523 XuanTie E907
 *
 * Target: Allwinner XuanTie E907 / E906 RISC-V Co-Processor
 * Memory: Fast Dedicated SRAM A3 Pool 0 (256 KB @ 0x40000000),
 *         Dedicated SRAM A3 Pool 1 (256 KB @ 0x40040000),
 *         Reserved Non-Cacheable DDR DRAM Carveout (1 MB @ 0x48100000).
 *
 * HARDWARE TRUTHS:
 * - 0x00020000 (128 KB) is HiFi4 DSP Memory! DO NOT USE for E907!
 * - 0x00044000 (160 KB) is OP-TEE / TrustZone Memory (SRAM A2)! DO NOT USE!
 * - E907 firmware lives exclusively in SRAM A3 (0x40000000 / 0x40040000).
 */

OUTPUT_ARCH("riscv")
ENTRY(_start)

MEMORY
{
    /* Dedicated Fast SRAM A3 (Pool 0) */
    SRAM_FAST (rwx) : ORIGIN = 0x40000000, LENGTH = 256K
    /* Switchable SRAM A3 (Pool 1 / SRAMA3_2 via REMAP_CTRL_REG[1]=1) */
    SRAM_A3_2 (rwx) : ORIGIN = 0x40040000, LENGTH = 256K
    /* Non-cacheable DDR DMA Carveout */
    DRAM      (rwx) : ORIGIN = 0x48100000, LENGTH = 1M
}

SECTIONS
{
    /* 1. Fast Vectors, Startup & Time-Critical Code in Zero-Wait-State SRAM A3 (Pool 0) */
    .vectors : ALIGN(64)
    {
        KEEP(*(.vectors))
        KEEP(*(.text.startup))
        *(.text.fast .text.fast.*)
        *(.text.trap)
    } > SRAM_FAST

    /* Standard application code in fast SRAM */
    .text : ALIGN(4)
    {
        *(.text)
        *(.text.*)
        *(.rodata)
        *(.rodata.*)
        *(.srodata)
        *(.srodata.*)
    } > SRAM_FAST

    /* 2. Fast Data & Critical Data */
    .data : ALIGN(4)
    {
        _sdata = .;
        __data_start = .;
        PROVIDE(__global_pointer$ = . + 0x800);
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        _edata = .;
        __data_end = .;
    } > SRAM_FAST
    _sidata = LOADADDR(.data);
    __data_load = LOADADDR(.data);

    .bss : ALIGN(8)
    {
        _sbss = .;
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(COMMON)
        _ebss = .;
        __bss_end = .;
    } > SRAM_FAST

    /* Execution Stack allocated in fast SRAM */
    .stack (NOLOAD) : ALIGN(16)
    {
        _stack_bottom = .;
        __stack_bottom = .;
        . += 0x00004000; /* 16 KB Stack */
        _stack_top = .;
        __stack_top = .;
        _sp = .;
    } > SRAM_FAST

    /* 3. SRAM A3 Pool 1: VirtIO vrings, SPSC Descriptors, Head/Tail Counters & trace0 */
    .resource_table : ALIGN(1024)
    {
        __resource_table_start = .;
        KEEP(*(.resource_table))
        KEEP(*(.resource_table*))
        __resource_table_end = .;
    } > SRAM_A3_2

    /* RemoteProc Trace Buffer in SRAM A3 Pool 1 */
    .trace_buffer (NOLOAD) : ALIGN(4)
    {
        __trace_start = .;
        KEEP(*(.trace_buffer))
        KEEP(*(.trace_buffer.*))
        . = ALIGN(4);
        __trace_end = .;
    } > SRAM_A3_2

    /* Shared Application / IPC Memory in SRAM A3 Pool 1 */
    .sram_c (NOLOAD) : ALIGN(4)
    {
        __sram_c_start = .;
        *(.sram_c)
        *(.sram_c.*)
        *(.sram_c_loc1)
        *(.sram_c_loc2)
        *(.sram_ipc .sram_ipc.*)
        *(.vring_ctrl)
        . = ALIGN(4);
        __sram_c_end = .;
    } > SRAM_A3_2

    /* 4. DDR DRAM: Large DMA Packet Buffers */
    .dram_payload (NOLOAD) : ALIGN(4096)
    {
        *(.dram_payload .dram_payload.*)
        *(.rpmsg_bufs)
    } > DRAM

    .dtcm_scratch (NOLOAD) : ALIGN(4)
    {
        __dtcm_scratch_start = .;
        *(.dtcm_scratch)
        *(.dtcm_scratch.*)
        . = ALIGN(4);
        __dtcm_scratch_end = .;
    } > SRAM_FAST

    /DISCARD/ :
    {
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.comment)
        *(.note.gnu.build-id)
    }
}