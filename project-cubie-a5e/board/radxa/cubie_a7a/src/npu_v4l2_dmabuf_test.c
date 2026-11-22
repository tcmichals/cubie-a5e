// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * npu_v4l2_dmabuf_test.c — Zero-copy V4L2 dma-buf -> etnaviv NPU PPU shader test
 *
 * Demonstrates zero-copy PRIME dma-buf sharing between V4L2 (/dev/video0,
 * sun6i-csi-capture) or CMA dma-heap (/dev/dma_heap/default_cma_region) and
 * the Allwinner A733 VIP9000Nano-DI NPU (/dev/dri/renderD128) via mainline
 * Linux DRM etnaviv.
 *
 * Build (cross-compile):
 *   aarch64-buildroot-linux-gnu-gcc -O2 -Wall -Wextra -o npu-v4l2-dmabuf-test npu_v4l2_dmabuf_test.c
 *
 * Run on Radxa Cubie A7A:
 *   ./npu-v4l2-dmabuf-test          # V4L2 VIDIOC_EXPBUF -> PRIME -> PPU shader
 *   ./npu-v4l2-dmabuf-test --cma    # CMA dma-heap -> PRIME -> PPU shader
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
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

static int alloc_v4l2_dmabuf(const char *dev, uint32_t w, uint32_t h,
			     int *v4l2_fd_out, int *dmabuf_fd_out, size_t *size_out)
{
	int fd = open(dev, O_RDWR);
	if (fd < 0) {
		perror(dev);
		return -1;
	}

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		perror("VIDIOC_S_FMT");
		close(fd);
		return -1;
	}

	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count = 1;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("VIDIOC_REQBUFS");
		close(fd);
		return -1;
	}

	struct v4l2_buffer buf;
	memset(&buf, 0, sizeof(buf));
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;
	buf.index = 0;
	if (ioctl(fd, VIDIOC_QUERYBUF, &buf) < 0) {
		perror("VIDIOC_QUERYBUF");
		close(fd);
		return -1;
	}

	struct v4l2_exportbuffer exp;
	memset(&exp, 0, sizeof(exp));
	exp.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	exp.index = 0;
	exp.plane = 0;
	exp.flags = O_RDWR | O_CLOEXEC;
	if (ioctl(fd, VIDIOC_EXPBUF, &exp) < 0) {
		perror("VIDIOC_EXPBUF");
		close(fd);
		return -1;
	}

	*v4l2_fd_out = fd;
	*dmabuf_fd_out = exp.fd;
	*size_out = buf.length;
	return 0;
}

static int alloc_cma_dmabuf(size_t size, int *dmabuf_fd_out)
{
	int heap_fd = open("/dev/dma_heap/default_cma_region", O_RDWR);
	if (heap_fd < 0) {
		perror("open /dev/dma_heap/default_cma_region");
		return -1;
	}
	struct dma_heap_allocation_data alloc = {
		.len = size,
		.fd_flags = O_RDWR | O_CLOEXEC,
	};
	if (ioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
		perror("DMA_HEAP_IOCTL_ALLOC");
		close(heap_fd);
		return -1;
	}
	close(heap_fd);
	*dmabuf_fd_out = alloc.fd;
	return 0;
}

int main(int argc, char **argv)
{
	int use_cma = (argc > 1 && strcmp(argv[1], "--cma") == 0);
	int v4l2_fd = -1;
	int dmabuf_fd = -1;
	size_t dmabuf_size = 640 * 480 * 3 / 2;

	if (!use_cma) {
		if (alloc_v4l2_dmabuf("/dev/video0", 640, 480,
				      &v4l2_fd, &dmabuf_fd, &dmabuf_size) == 0) {
			printf("[npu-v4l2-dmabuf] Exported V4L2 dma-buf from /dev/video0 "
			       "(fd=%d, size=%zu bytes)\n", dmabuf_fd, dmabuf_size);
		} else {
			printf("[npu-v4l2-dmabuf] /dev/video0 EXPBUF unavailable, falling back to CMA dma-heap\n");
			use_cma = 1;
		}
	}
	if (use_cma) {
		if (alloc_cma_dmabuf(dmabuf_size, &dmabuf_fd) < 0)
			return 1;
		printf("[npu-v4l2-dmabuf] Allocated CMA dma-buf from /dev/dma_heap/default_cma_region "
		       "(fd=%d, size=%zu bytes)\n", dmabuf_fd, dmabuf_size);
	}

	/* Write test frame luma pattern (fill=5) into the dma-buf */
	const uint8_t fill_val = 5;
	struct dma_buf_sync sync = { .flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
	ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);
	uint8_t *in_map = mmap(NULL, dmabuf_size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
	if (in_map == MAP_FAILED) {
		perror("mmap dmabuf");
		return 1;
	}
	memset(in_map, fill_val, 64 * 6);
	munmap(in_map, dmabuf_size);
	sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
	ioctl(dmabuf_fd, DMA_BUF_IOCTL_SYNC, &sync);

	int drm_fd = open("/dev/dri/renderD128", O_RDWR);
	if (drm_fd < 0) {
		perror("open /dev/dri/renderD128");
		return 1;
	}

	/* Zero-copy PRIME import: V4L2/CMA dma-buf fd -> etnaviv GEM handle (bo_idx = 0) */
	struct drm_prime_handle prime = { .fd = dmabuf_fd, .flags = 0 };
	if (ioctl(drm_fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &prime) < 0) {
		perror("DRM_IOCTL_PRIME_FD_TO_HANDLE");
		return 1;
	}
	printf("[npu-v4l2-dmabuf] PRIME imported dma-buf fd=%d -> etnaviv GEM handle=%u\n",
	       dmabuf_fd, prime.handle);

	/* Output + shader BO (bo_idx = 1) */
	struct drm_etnaviv_gem_new gem_new = { .size = 4096, .flags = ETNA_BO_WC };
	if (ioctl(drm_fd, DRM_IOCTL_ETNAVIV_GEM_NEW, &gem_new) < 0) {
		perror("DRM_IOCTL_ETNAVIV_GEM_NEW");
		return 1;
	}
	struct drm_etnaviv_gem_info gem_info = { .handle = gem_new.handle };
	if (ioctl(drm_fd, DRM_IOCTL_ETNAVIV_GEM_INFO, &gem_info) < 0) {
		perror("DRM_IOCTL_ETNAVIV_GEM_INFO");
		return 1;
	}
	uint8_t *out_map = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, gem_info.offset);
	if (out_map == MAP_FAILED) {
		perror("mmap out BO");
		return 1;
	}
	memset(out_map, 0, 4096);

	const uint32_t img_w = 64, img_h = 6, img_stride = 64;
	const uint32_t out_off = 0;
	const uint32_t sh_off = 512;
	const uint32_t sh_size = 28 * 4;

	static const uint32_t shader_inst[28] = {
		0x78011779, 0x39000804, 0x00A90050, 0x00000000,
		0x00811019, 0x00001804, 0x40010000, 0x78000188,
		0x0081101a, 0x00001804, 0x40010000, 0x78000188,
		0x0781102d, 0x00001804, 0x40000000, 0x00000000,
		0x07821009, 0x00000004, 0x00000000, 0x20390028,
		0x07811003, 0x00001804, 0x00000140, 0x00000000,
		0x380007BA, 0x39001804, 0x00A90050, 0x00390018,
	};
	memcpy(out_map + sh_off, shader_inst, sizeof(shader_inst));

	struct cmdbuf cb;
	memset(&cb, 0, sizeof(cb));

	load_state(&cb, VIVS_GL_API_MODE, VIVS_GL_API_MODE_OPENCL);
	emit_sem(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);
	emit_stall(&cb, SYNC_RECIPIENT_FE, SYNC_RECIPIENT_PE);

	/* Input image descriptor in u0 -> reads directly from PRIME-imported V4L2 dma-buf (bo_idx 0) */
	load_states_start(&cb, VIVS_SH_HALTI5_UNIFORMS(0), 4);
	emit_reloc(&cb, 0, 0);
	emit(&cb, img_stride);
	emit(&cb, img_w | (img_h << 16));
	emit(&cb, 0x444051f0);
	emit(&cb, 0xffffffff);

	/* Output image descriptor in u1 -> writes to output GEM BO (bo_idx 1) */
	load_states_start(&cb, VIVS_SH_HALTI5_UNIFORMS(4), 4);
	emit_reloc(&cb, 1, out_off);
	emit(&cb, img_stride);
	emit(&cb, img_w | (img_h << 16));
	emit(&cb, 0x444051f0);
	emit(&cb, 0xffffffff);

	/* Scale factor in u2: 257.0f / 4096.0f (0x3d808000) */
	load_states_start(&cb, VIVS_SH_HALTI5_UNIFORMS(8), 4);
	emit(&cb, 0x3d808000);
	emit(&cb, 0x3d808000);
	emit(&cb, 0x3d808000);
	emit(&cb, 0x3d808000);
	emit(&cb, 0x00000000);

	load_state(&cb, VIVS_CL_CONFIG,
		   VIVS_CL_CONFIG_DIMENSIONS(2) | VIVS_CL_CONFIG_VALUE_ORDER(3));
	load_state(&cb, VIVS_VS_ICACHE_INVALIDATE, 0x1f);
	load_state(&cb, VIVS_PS_VARYING_NUM_COMPONENTS(0), 0);
	load_state(&cb, VIVS_PS_TEMP_REGISTER_CONTROL, 3);
	load_state(&cb, VIVS_PS_SAMPLER_BASE, 0);
	load_state(&cb, VIVS_PS_UNIFORM_BASE, 0);
	load_state(&cb, VIVS_PS_NEWRANGE_LOW, 0);
	load_state(&cb, VIVS_PS_NEWRANGE_HIGH, sh_size / 16);
	load_state_reloc(&cb, VIVS_PS_INST_ADDR, 1, sh_off);
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
	emit(&cb, 0x0);
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

	struct drm_etnaviv_gem_submit_bo bos[2] = {
		{ .flags = ETNA_SUBMIT_BO_READ, .handle = prime.handle },
		{ .flags = ETNA_SUBMIT_BO_READ | ETNA_SUBMIT_BO_WRITE, .handle = gem_new.handle },
	};
	struct drm_etnaviv_gem_submit submit;
	memset(&submit, 0, sizeof(submit));
	submit.pipe = 0;
	submit.exec_state = ETNA_PIPE_3D;
	submit.nr_bos = 2;
	submit.bos = (uintptr_t)bos;
	submit.nr_relocs = cb.nr_relocs;
	submit.relocs = (uintptr_t)cb.relocs;
	submit.stream_size = cb.count * 4;
	submit.stream = (uintptr_t)cb.words;

	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	if (ioctl(drm_fd, DRM_IOCTL_ETNAVIV_GEM_SUBMIT, &submit) < 0) {
		perror("DRM_IOCTL_ETNAVIV_GEM_SUBMIT");
		return 1;
	}
	struct drm_etnaviv_wait_fence wait = {
		.pipe = 0,
		.fence = submit.fence,
		.timeout = { .tv_sec = t0.tv_sec + 5, .tv_nsec = t0.tv_nsec },
	};
	if (ioctl(drm_fd, DRM_IOCTL_ETNAVIV_WAIT_FENCE, &wait) < 0) {
		perror("DRM_IOCTL_ETNAVIV_WAIT_FENCE");
		return 1;
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

	struct drm_etnaviv_gem_cpu_prep prep = {
		.handle = gem_new.handle,
		.op = ETNA_PREP_READ,
		.timeout = { .tv_sec = t1.tv_sec + 2, .tv_nsec = t1.tv_nsec },
	};
	ioctl(drm_fd, DRM_IOCTL_ETNAVIV_GEM_CPU_PREP, &prep);

	float expected = (float)fill_val * (257.0f / 4096.0f);
	uint32_t expected_bits = 0;
	memcpy(&expected_bits, &expected, sizeof(expected_bits));

	static const uint32_t offsets[2] = { 0, 256 };
	int pass = 1;
	int exact = 0;
	for (int i = 0; i < 2; i++) {
		uint32_t got_bits = 0;
		memcpy(&got_bits, out_map + out_off + offsets[i], sizeof(got_bits));
		if (got_bits == expected_bits) {
			exact++;
		} else {
			fprintf(stderr, "  MISMATCH off=%u: got=0x%08x expected=0x%08x\n",
				offsets[i], got_bits, expected_bits);
			pass = 0;
		}
	}

	uint32_t sample_bits = 0;
	float sample = 0.0f;
	memcpy(&sample_bits, out_map + out_off + offsets[0], sizeof(sample_bits));
	memcpy(&sample, &sample_bits, sizeof(sample));
	printf("[npu-v4l2-dmabuf] fill=%u -> %.6f (0x%08x) | %d/2 workgroups exact | %.3f ms | %s\n",
	       fill_val, sample, sample_bits, exact, ms, pass ? "PASS" : "FAIL");

	struct drm_etnaviv_gem_cpu_fini fini = { .handle = gem_new.handle };
	ioctl(drm_fd, DRM_IOCTL_ETNAVIV_GEM_CPU_FINI, &fini);
	munmap(out_map, 4096);

	struct drm_gem_close cl0 = { .handle = prime.handle };
	struct drm_gem_close cl1 = { .handle = gem_new.handle };
	ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &cl0);
	ioctl(drm_fd, DRM_IOCTL_GEM_CLOSE, &cl1);
	close(drm_fd);
	close(dmabuf_fd);
	if (v4l2_fd >= 0)
		close(v4l2_fd);

	return pass ? 0 : 1;
}
