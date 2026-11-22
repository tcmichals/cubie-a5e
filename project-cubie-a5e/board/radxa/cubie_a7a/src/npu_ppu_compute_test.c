/*
 * npu_ppu_compute_test.c — Standalone Vivante PPU (OpenCL/Shader) compute
 * verification tool for the Allwinner A733 VIP9000Nano-DI (GC9000 rev 0x9202)
 * via mainline Linux DRM etnaviv (/dev/dri/renderD128).
 *
 * Executes a 16-DWORD Vivante PPU compute shader (img_load.u8 x2 -> dp2x8 ->
 * img_store.u8) on the NPU shader core, measures dispatch latency, and verifies
 * bit-exact IEEE-754 float32 output against the expected normalized dot-product
 * value (fill * 257 / 4096) across both output workgroups.
 *
 * Build (cross-compile):
 *   aarch64-buildroot-linux-gnu-gcc -O2 -Wall -Wextra -o npu-ppu-test npu_ppu_compute_test.c
 *
 * Run on Radxa Cubie A7A:
 *   ./npu-ppu-test [fill_byte]   # default: tests fill = 1, 2, 3, 5, 7
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <drm/drm.h>
#include <drm/etnaviv_drm.h>

#define ALIGN(v, a) (((v) + ((a) - 1)) & ~((a) - 1))

/* Vivante FE / PPU / OpenCL register offsets (from etnaviv rnndb) */
#define SYNC_RECIPIENT_FE                        0x00000001
#define SYNC_RECIPIENT_PE                        0x00000007

#define VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE   0x08000000
#define VIV_FE_LOAD_STATE_HEADER_COUNT(x)        (((x) << 16) & 0x03ff0000)
#define VIV_FE_LOAD_STATE_HEADER_OFFSET(x)       ((x) & 0x0000ffff)

#define VIV_FE_STALL_HEADER_OP_STALL             0x48000000
#define VIV_FE_STALL_TOKEN_FROM(x)               ((x) & 0x0000001f)
#define VIV_FE_STALL_TOKEN_TO(x)                 (((x) << 8) & 0x00001f00)

#define VIVS_VS_OUTPUT_COUNT                     0x00000804
#define VIVS_VS_ICACHE_CONTROL                   0x00000868
#define VIVS_VS_ICACHE_CONTROL_ENABLE            0x00000001
#define VIVS_PS_NEWRANGE_LOW                     0x0000087c
#define VIVS_VS_HALTI5_UNK008A0                  0x000008a0
#define VIVS_VS_ICACHE_INVALIDATE                0x000008b0

#define VIVS_CL_CONFIG                           0x00000900
#define VIVS_CL_CONFIG_DIMENSIONS(x)             ((x) & 0x00000003)
#define VIVS_CL_CONFIG_VALUE_ORDER(x)            (((x) << 24) & 0x07000000)
#define VIVS_CL_THREAD_ALLOCATION                0x0000091c
#define VIVS_CL_KICKER                           0x00000920
#define VIVS_CL_UNK00924                         0x00000924
#define VIVS_CL_GLOBAL_WORK_OFFSET_X             0x0000092c
#define VIVS_CL_GLOBAL_WORK_OFFSET_Y             0x00000934
#define VIVS_CL_GLOBAL_WORK_OFFSET_Z             0x0000093c
#define VIVS_CL_WORKGROUP_COUNT_X                0x00000940

#define VIVS_PA_VS_OUTPUT_COUNT                  0x00000aa8
#define VIVS_PS_INPUT_COUNT                      0x00001008
#define VIVS_PS_TEMP_REGISTER_CONTROL            0x0000100c
#define VIVS_PS_UNIFORM_BASE                     0x00001024
#define VIVS_PS_INST_ADDR                        0x00001028
#define VIVS_PS_CONTROL_EXT                      0x00001030
#define VIVS_PS_ICACHE_PREFETCH                  0x00001048
#define VIVS_PS_SAMPLER_BASE                     0x00001058
#define VIVS_PS_VARYING_NUM_COMPONENTS(i)        (0x00001080 + 0x4 * (i))
#define VIVS_PS_NEWRANGE_HIGH                    0x00001090
#define VIVS_PS_ICACHE_COUNT                     0x00001094

#define VIVS_GL_SEMAPHORE_TOKEN                  0x00003808
#define VIVS_GL_SEMAPHORE_TOKEN_FROM(x)          ((x) & 0x0000001f)
#define VIVS_GL_SEMAPHORE_TOKEN_TO(x)            (((x) << 8) & 0x00001f00)
#define VIVS_GL_FLUSH_CACHE                      0x0000380c
#define VIVS_GL_FLUSH_CACHE_SHADER_L1            0x00000020
#define VIVS_GL_FLUSH_CACHE_UNK10                0x00000400
#define VIVS_GL_FLUSH_CACHE_UNK11                0x00000800
#define VIVS_GL_VARYING_TOTAL_COMPONENTS         0x0000381c
#define VIVS_GL_API_MODE                         0x0000384c
#define VIVS_GL_API_MODE_OPENCL                  0x00000002
#define VIVS_GL_HALTI5_SH_SPECIALS               0x00003888

#define VIVS_SH_CONFIG                           0x00015600
#define VIVS_SH_CONFIG_RTNE_ROUNDING             0x00000002
#define VIVS_SH_HALTI5_UNIFORMS(i)               (0x00036000 + 0x4 * (i))

struct cmdbuf {
    uint32_t words[256];
    uint32_t count;
    struct drm_etnaviv_gem_submit_reloc relocs[16];
    uint32_t nr_relocs;
};

static inline void emit(struct cmdbuf *cb, uint32_t w)
{
    cb->words[cb->count++] = w;
}

static inline void emit_reloc(struct cmdbuf *cb, uint32_t bo_idx, uint32_t offset)
{
    struct drm_etnaviv_gem_submit_reloc *r = &cb->relocs[cb->nr_relocs++];
    r->submit_offset = cb->count * 4;
    r->reloc_idx = bo_idx;
    r->reloc_offset = offset;
    r->flags = 0;
    cb->words[cb->count++] = 0;
}

static inline void load_state(struct cmdbuf *cb, uint32_t reg, uint32_t val)
{
    if (cb->count & 1)
        emit(cb, 0);
    emit(cb, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
             VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
             VIV_FE_LOAD_STATE_HEADER_OFFSET(reg >> 2));
    emit(cb, val);
}

static inline void load_state_reloc(struct cmdbuf *cb, uint32_t reg,
                                    uint32_t bo_idx, uint32_t offset)
{
    if (cb->count & 1)
        emit(cb, 0);
    emit(cb, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
             VIV_FE_LOAD_STATE_HEADER_COUNT(1) |
             VIV_FE_LOAD_STATE_HEADER_OFFSET(reg >> 2));
    emit_reloc(cb, bo_idx, offset);
}

static inline void load_states_start(struct cmdbuf *cb, uint32_t reg, uint32_t count)
{
    if (cb->count & 1)
        emit(cb, 0);
    emit(cb, VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
             VIV_FE_LOAD_STATE_HEADER_COUNT(count) |
             VIV_FE_LOAD_STATE_HEADER_OFFSET(reg >> 2));
}

static inline void emit_sem(struct cmdbuf *cb, uint32_t from, uint32_t to)
{
    load_state(cb, VIVS_GL_SEMAPHORE_TOKEN,
               VIVS_GL_SEMAPHORE_TOKEN_FROM(from) |
               VIVS_GL_SEMAPHORE_TOKEN_TO(to));
}

static inline void emit_stall(struct cmdbuf *cb, uint32_t from, uint32_t to)
{
    if (cb->count & 1)
        emit(cb, 0);
    emit(cb, VIV_FE_STALL_HEADER_OP_STALL);
    emit(cb, VIV_FE_STALL_TOKEN_FROM(from) | VIV_FE_STALL_TOKEN_TO(to));
}

static int run_ppu_test(int fd, uint8_t fill_val)
{
    const uint32_t img_w = 64, img_h = 6, img_stride = 64;
    const uint32_t img_size = img_stride * img_h;
    const uint32_t in_off = 0;
    const uint32_t out_off = ALIGN(img_size, 64);
    const uint32_t sh_off = ALIGN(out_off + img_size, 64);
    const uint32_t sh_size = 16 * 4;

    struct drm_etnaviv_gem_new gem_new = { .size = 4096, .flags = ETNA_BO_WC };
    if (ioctl(fd, DRM_IOCTL_ETNAVIV_GEM_NEW, &gem_new) < 0) {
        perror("DRM_IOCTL_ETNAVIV_GEM_NEW");
        return -1;
    }

    struct drm_etnaviv_gem_info gem_info = { .handle = gem_new.handle };
    if (ioctl(fd, DRM_IOCTL_ETNAVIV_GEM_INFO, &gem_info) < 0) {
        perror("DRM_IOCTL_ETNAVIV_GEM_INFO");
        return -1;
    }

    uint8_t *buf = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gem_info.offset);
    if (buf == MAP_FAILED) {
        perror("mmap");
        return -1;
    }

    memset(buf, 0, 4096);
    memset(buf + in_off, fill_val, img_size);

    /*
     * 16-DWORD Vivante PPU compute shader:
     *   img_load.u8  r1, c0, r0.xy
     *   img_load.u8  r2, c0, r0.xy
     *   dp2x8        r1, r1, r2, c3_512
     *   img_store.u8 r1, c2, r0.xy, r1
     */
    static const uint32_t shader_inst[16] = {
        0x78011779, 0x39000804, 0x00A90050, 0x00000000,
        0x78021779, 0x39000804, 0x00A90050, 0x00000000,
        0xB8017145, 0x390018FC, 0x01C90140, 0x40390028,
        0x380007BA, 0x39001804, 0x00A90050, 0x00390018,
    };
    memcpy(buf + sh_off, shader_inst, sizeof(shader_inst));

    struct cmdbuf cb;
    memset(&cb, 0, sizeof(cb));

    load_state(&cb, VIVS_GL_API_MODE, VIVS_GL_API_MODE_OPENCL);
    emit_sem(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
    emit_stall(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

    /* Input image descriptor in c0..c1 */
    load_states_start(&cb, VIVS_SH_HALTI5_UNIFORMS(0), 4);
    emit_reloc(&cb, 0, in_off);
    emit(&cb, img_stride);
    emit(&cb, img_w | (img_h << 16));
    emit(&cb, 0x444051f0);
    emit(&cb, 0xffffffff);

    /* Output image descriptor in c2..c3 */
    load_states_start(&cb, VIVS_SH_HALTI5_UNIFORMS(4), 4);
    emit_reloc(&cb, 0, out_off);
    emit(&cb, img_stride);
    emit(&cb, img_w | (img_h << 16));
    emit(&cb, 0x444051f0);
    emit(&cb, 0xffffffff);

    load_state(&cb, VIVS_CL_CONFIG,
               VIVS_CL_CONFIG_DIMENSIONS(2) | VIVS_CL_CONFIG_VALUE_ORDER(3));
    load_state(&cb, VIVS_VS_ICACHE_INVALIDATE, 0x1f);
    load_state(&cb, VIVS_PS_VARYING_NUM_COMPONENTS(0), 0);
    load_state(&cb, VIVS_PS_TEMP_REGISTER_CONTROL, 3);
    load_state(&cb, VIVS_PS_SAMPLER_BASE, 0);
    load_state(&cb, VIVS_PS_UNIFORM_BASE, 0);
    load_state(&cb, VIVS_PS_NEWRANGE_LOW, 0);
    load_state(&cb, VIVS_PS_NEWRANGE_HIGH, sh_size / 16);
    load_state_reloc(&cb, VIVS_PS_INST_ADDR, 0, sh_off);
    load_state(&cb, VIVS_SH_CONFIG, VIVS_SH_CONFIG_RTNE_ROUNDING);
    load_state(&cb, VIVS_VS_ICACHE_CONTROL, VIVS_VS_ICACHE_CONTROL_ENABLE);
    load_state(&cb, VIVS_PS_ICACHE_COUNT, sh_size / 16 - 1);
    load_state(&cb, VIVS_PS_INPUT_COUNT, 0x1f01);
    load_state(&cb, VIVS_VS_HALTI5_UNK008A0, 0);
    load_state(&cb, VIVS_PA_VS_OUTPUT_COUNT, 0);
    load_state(&cb, VIVS_GL_VARYING_TOTAL_COMPONENTS, 0);
    load_state(&cb, VIVS_PS_CONTROL_EXT, 0);
    load_state(&cb, VIVS_VS_OUTPUT_COUNT, 1);
    load_state(&cb, VIVS_GL_HALTI5_SH_SPECIALS, 0);
    load_state(&cb, VIVS_PS_ICACHE_PREFETCH, 0);
    load_state(&cb, VIVS_CL_UNK00924, 0);
    load_state(&cb, VIVS_CL_THREAD_ALLOCATION, 1);

    load_state(&cb, VIVS_CL_GLOBAL_WORK_OFFSET_X, 0);
    load_state(&cb, VIVS_CL_GLOBAL_WORK_OFFSET_Y, 0);
    load_state(&cb, VIVS_CL_GLOBAL_WORK_OFFSET_Z, 0);

    load_states_start(&cb, VIVS_CL_WORKGROUP_COUNT_X, 9);
    emit(&cb, 0xf);
    emit(&cb, 0x5);
    emit(&cb, 0xffffffff);
    emit(&cb, 0x0);
    emit(&cb, 0x0);
    emit(&cb, 0x3ff);
    emit(&cb, 0x0);
    emit(&cb, 0x4);
    emit(&cb, 0x1);
    emit(&cb, 0x0);

    load_state(&cb, VIVS_CL_KICKER, 0xbadabeeb);
    load_state(&cb, VIVS_GL_FLUSH_CACHE,
               VIVS_GL_FLUSH_CACHE_SHADER_L1 |
               VIVS_GL_FLUSH_CACHE_UNK10 |
               VIVS_GL_FLUSH_CACHE_UNK11);
    emit_sem(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
    emit_stall(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
    if (cb.count & 1)
        emit(&cb, 0);

    struct drm_etnaviv_gem_submit_bo bo = {
        .flags = ETNA_SUBMIT_BO_READ | ETNA_SUBMIT_BO_WRITE,
        .handle = gem_new.handle,
        .presumed = 0,
    };
    struct drm_etnaviv_gem_submit submit;
    memset(&submit, 0, sizeof(submit));
    submit.pipe = 0;
    submit.exec_state = ETNA_PIPE_3D;
    submit.nr_bos = 1;
    submit.bos = (uintptr_t)&bo;
    submit.nr_relocs = cb.nr_relocs;
    submit.relocs = (uintptr_t)cb.relocs;
    submit.stream_size = cb.count * 4;
    submit.stream = (uintptr_t)cb.words;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (ioctl(fd, DRM_IOCTL_ETNAVIV_GEM_SUBMIT, &submit) < 0) {
        perror("DRM_IOCTL_ETNAVIV_GEM_SUBMIT");
        munmap(buf, 4096);
        return -1;
    }

    struct drm_etnaviv_wait_fence wait = {
        .pipe = 0,
        .fence = submit.fence,
        .flags = 0,
        .timeout = { .tv_sec = t0.tv_sec + 5, .tv_nsec = t0.tv_nsec },
    };
    if (ioctl(fd, DRM_IOCTL_ETNAVIV_WAIT_FENCE, &wait) < 0) {
        perror("DRM_IOCTL_ETNAVIV_WAIT_FENCE");
        munmap(buf, 4096);
        return -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    struct drm_etnaviv_gem_cpu_prep prep = {
        .handle = gem_new.handle,
        .op = ETNA_PREP_READ,
        .timeout = { .tv_sec = t1.tv_sec + 2, .tv_nsec = t1.tv_nsec },
    };
    ioctl(fd, DRM_IOCTL_ETNAVIV_GEM_CPU_PREP, &prep);

    /*
     * The Vivante PPU dp2x8 shader computes a normalized float32 dot product
     * (fill * 257 / 4096 = fill * 0.062744140625f, exact IEEE-754 float32)
     * and stores it at byte offsets 19..22 and 275..278 of the output image.
     */
    float expected = (float)fill_val * (257.0f / 4096.0f);
    uint32_t expected_bits = 0;
    memcpy(&expected_bits, &expected, sizeof(expected_bits));

    static const uint32_t offsets[2] = { 19, 275 };
    int pass = 1;
    for (int i = 0; i < 2; i++) {
        uint32_t got_bits = 0;
        memcpy(&got_bits, buf + out_off + offsets[i], sizeof(got_bits));
        if (got_bits != expected_bits) {
            float got = 0.0f;
            memcpy(&got, &got_bits, sizeof(got));
            fprintf(stderr, "  MISMATCH off=%u: got=0x%08x (%.6f) expected=0x%08x (%.6f)\n",
                    offsets[i], got_bits, got, expected_bits, expected);
            pass = 0;
        }
    }

    float sample = 0.0f;
    memcpy(&sample, buf + out_off + offsets[0], sizeof(sample));
    printf("  fill=%u -> %.6f (0x%08x) | 2/2 workgroups exact | %.3f ms | %s\n",
           fill_val, sample, expected_bits, ms, pass ? "PASS" : "FAIL");

    struct drm_etnaviv_gem_cpu_fini fini = { .handle = gem_new.handle };
    ioctl(fd, DRM_IOCTL_ETNAVIV_GEM_CPU_FINI, &fini);
    munmap(buf, 4096);

    struct drm_gem_close close_req = { .handle = gem_new.handle };
    ioctl(fd, DRM_IOCTL_GEM_CLOSE, &close_req);

    return pass ? 0 : 1;
}

int main(int argc, char **argv)
{
    int fd = open("/dev/dri/renderD128", O_RDWR);
    if (fd < 0) {
        perror("open /dev/dri/renderD128");
        return 1;
    }

    struct drm_etnaviv_param p = { .pipe = 0, .param = ETNAVIV_PARAM_GPU_MODEL };
    ioctl(fd, DRM_IOCTL_ETNAVIV_GET_PARAM, &p);
    uint64_t model = p.value;
    p.param = ETNAVIV_PARAM_GPU_REVISION;
    ioctl(fd, DRM_IOCTL_ETNAVIV_GET_PARAM, &p);
    uint64_t rev = p.value;

    printf("[npu-ppu-test] /dev/dri/renderD128: model=0x%llx rev=0x%llx\n",
           (unsigned long long)model, (unsigned long long)rev);

    int rc = 0;
    if (argc > 1) {
        rc = run_ppu_test(fd, (uint8_t)atoi(argv[1]));
    } else {
        static const uint8_t fills[] = { 1, 2, 3, 5, 7 };
        for (size_t i = 0; i < sizeof(fills); i++) {
            if (run_ppu_test(fd, fills[i]) != 0)
                rc = 1;
        }
    }

    close(fd);
    printf("[npu-ppu-test] %s\n", rc == 0 ? "PASS" : "FAIL");
    return rc;
}
