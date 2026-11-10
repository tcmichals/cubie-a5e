// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * a7a-cam-capture - Radxa Cubie A7A (Allwinner A733) OV5647 CSI-2 Capture & Hardware H.264 / MJPEG Recorder
 *
 * Captures 1920x1080 SGBRG10 frames from /dev/video0 at 30.00 FPS, performs
 * fast SGBRG10 -> NV12 / RGB24 conversion with auto white-balance & gamma LUT, and writes:
 *   - .h264 : Hardware-compressed H.264 Annex-B video via Allwinner VE (sunxi-cedrus)
 *   - .avi  : Hardware H.264 (or MJPEG fallback) 1080p @ 30fps AVI video file
 *   - .jpg  : Compressed JPEG still image (via libjpeg-turbo)
 *   - .ppm  : Uncompressed binary PPM still image
 *   - .raw  : Raw SGBRG10_1X10 Bayer frame
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <jpeglib.h>

#define MAX_V4L2_BUFS 8
#define ENC_BUFS 2
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

struct mmap_buf {
	void *start;
	size_t length;
};

struct avi_idx_entry {
	uint32_t offset;
	uint32_t size;
	uint32_t is_keyframe;
};

struct cedrus_encoder {
	int fd;
	uint32_t width;
	uint32_t height;
	uint32_t aligned_w;
	uint32_t aligned_h;
	uint32_t stride;
	uint32_t nv12_size;
	struct mmap_buf out_bufs[ENC_BUFS];
	struct mmap_buf cap_bufs[ENC_BUFS];
	unsigned int out_count;
	unsigned int cap_count;
};

static void write_le16(uint8_t *p, uint16_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
}

static void write_le32(uint8_t *p, uint32_t v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

static void fput_le32(FILE *fp, uint32_t v)
{
	uint8_t b[4];
	write_le32(b, v);
	fwrite(b, 1, 4, fp);
}

/*
 * Standard 224-byte RIFF AVI header for H264 or MJPG stream:
 *   RIFF ('AVI '
 *     LIST ('hdrl'
 *       'avih' (56 bytes)
 *       LIST ('strl'
 *         'strh' (56 bytes)
 *         'strf' (40 bytes BITMAPINFOHEADER)
 *       )
 *     )
 *     LIST ('movi' ...
 */
static void avi_write_header(FILE *fp, uint32_t width, uint32_t height,
			     uint32_t fps, uint32_t num_frames,
			     uint32_t movi_bytes, uint32_t riff_bytes,
			     const char fourcc[4])
{
	uint8_t hdr[224];
	uint32_t usec_per_frame = (fps > 0) ? (1000000U / fps) : 33333U;
	uint32_t max_bytes_per_sec = width * height * 2;

	memset(hdr, 0, sizeof(hdr));

	/* 0..11: RIFF header */
	memcpy(hdr + 0, "RIFF", 4);
	write_le32(hdr + 4, riff_bytes);
	memcpy(hdr + 8, "AVI ", 4);

	/* 12..23: LIST hdrl (size = 192 bytes: 4 + 8+56 + 12+8+56+8+40) */
	memcpy(hdr + 12, "LIST", 4);
	write_le32(hdr + 16, 192);
	memcpy(hdr + 20, "hdrl", 4);

	/* 24..87: avih chunk (56 bytes payload) */
	memcpy(hdr + 24, "avih", 4);
	write_le32(hdr + 28, 56);
	write_le32(hdr + 32, usec_per_frame);       /* dwMicroSecPerFrame */
	write_le32(hdr + 36, max_bytes_per_sec);    /* dwMaxBytesPerSec */
	write_le32(hdr + 40, 0);                    /* dwPaddingGranularity */
	write_le32(hdr + 44, 0x10 | 0x100);         /* dwFlags: AVIF_HASINDEX | AVIF_ISINTERLEAVED */
	write_le32(hdr + 48, num_frames);           /* dwTotalFrames */
	write_le32(hdr + 52, 0);                    /* dwInitialFrames */
	write_le32(hdr + 56, 1);                    /* dwStreams */
	write_le32(hdr + 60, width * height);       /* dwSuggestedBufferSize */
	write_le32(hdr + 64, width);                /* dwWidth */
	write_le32(hdr + 68, height);               /* dwHeight */

	/* 88..99: LIST strl (size = 116 bytes: 4 + 8+56 + 8+40) */
	memcpy(hdr + 88, "LIST", 4);
	write_le32(hdr + 92, 116);
	memcpy(hdr + 96, "strl", 4);

	/* 100..163: strh chunk (56 bytes payload) */
	memcpy(hdr + 100, "strh", 4);
	write_le32(hdr + 104, 56);
	memcpy(hdr + 108, "vids", 4);               /* fccType */
	memcpy(hdr + 112, fourcc, 4);               /* fccHandler */
	write_le32(hdr + 116, 0);                   /* dwFlags */
	write_le16(hdr + 120, 0);                   /* wPriority */
	write_le16(hdr + 122, 0);                   /* wLanguage */
	write_le32(hdr + 124, 0);                   /* dwInitialFrames */
	write_le32(hdr + 128, 1);                   /* dwScale */
	write_le32(hdr + 132, fps);                 /* dwRate */
	write_le32(hdr + 136, 0);                   /* dwStart */
	write_le32(hdr + 140, num_frames);          /* dwLength */
	write_le32(hdr + 144, width * height);      /* dwSuggestedBufferSize */
	write_le32(hdr + 148, 10000);               /* dwQuality */
	write_le32(hdr + 152, 0);                   /* dwSampleSize */
	write_le16(hdr + 156, 0);                   /* rcFrame.left */
	write_le16(hdr + 158, 0);                   /* rcFrame.top */
	write_le16(hdr + 160, (uint16_t)width);     /* rcFrame.right */
	write_le16(hdr + 162, (uint16_t)height);    /* rcFrame.bottom */

	/* 164..211: strf chunk (40 bytes BITMAPINFOHEADER) */
	memcpy(hdr + 164, "strf", 4);
	write_le32(hdr + 168, 40);
	write_le32(hdr + 172, 40);                  /* biSize */
	write_le32(hdr + 176, width);               /* biWidth */
	write_le32(hdr + 180, height);              /* biHeight */
	write_le16(hdr + 184, 1);                   /* biPlanes */
	write_le16(hdr + 186, 24);                  /* biBitCount */
	memcpy(hdr + 188, fourcc, 4);               /* biCompression */
	write_le32(hdr + 192, width * height * 3);  /* biSizeImage */

	/* 212..223: LIST movi */
	memcpy(hdr + 212, "LIST", 4);
	write_le32(hdr + 216, movi_bytes);
	memcpy(hdr + 220, "movi", 4);

	fseek(fp, 0, SEEK_SET);
	fwrite(hdr, 1, sizeof(hdr), fp);
}

/*
 * Build per-channel 10-bit (0..1023) -> 8-bit (0..255) gamma + AWB LUTs
 * from a reference frame.
 */
static void build_color_luts(const uint16_t *src, int w, int h,
			     uint8_t lut_r[1024], uint8_t lut_g[1024], uint8_t lut_b[1024],
			     uint16_t *out_min, uint16_t *out_max, double *out_mean)
{
	uint64_t sum_r = 0, sum_g = 0, sum_b = 0;
	uint64_t cnt_r = 0, cnt_g = 0, cnt_b = 0;
	uint16_t vmin = 1023, vmax = 0;

	/* Sample every 4th row/col for fast statistics */
	for (int y = 0; y < h; y += 2) {
		const uint16_t *row0 = src + y * w;
		const uint16_t *row1 = src + (y + 1) * w;
		for (int x = 0; x < w; x += 2) {
			uint16_t g0 = row0[x] & 0x3ff;
			uint16_t b0 = row0[x + 1] & 0x3ff;
			uint16_t r0 = row1[x] & 0x3ff;
			uint16_t g1 = row1[x + 1] & 0x3ff;

			if (g0 < vmin) vmin = g0;
			if (g0 > vmax) vmax = g0;

			if (g0 > 64) { sum_g += g0; cnt_g++; }
			if (b0 > 64) { sum_b += b0; cnt_b++; }
			if (r0 > 64) { sum_r += r0; cnt_r++; }
			if (g1 > 64) { sum_g += g1; cnt_g++; }
		}
	}

	double avg_r = cnt_r ? (double)sum_r / cnt_r : 256.0;
	double avg_g = cnt_g ? (double)sum_g / cnt_g : 256.0;
	double avg_b = cnt_b ? (double)sum_b / cnt_b : 256.0;

	double gain_r = (avg_r > 1.0) ? (avg_g / avg_r) : 1.0;
	double gain_b = (avg_b > 1.0) ? (avg_g / avg_b) : 1.0;
	if (gain_r < 0.5) gain_r = 0.5;
	if (gain_r > 2.5) gain_r = 2.5;
	if (gain_b < 0.5) gain_b = 0.5;
	if (gain_b > 2.5) gain_b = 2.5;

	double black = 16.0;
	double white = (vmax > 200) ? (double)vmax * 0.95 : 512.0;
	if (white <= black + 64.0)
		white = black + 64.0;

	for (int i = 0; i < 1024; i++) {
		double v = (i > black) ? (i - black) / (white - black) : 0.0;
		double vr = v * gain_r;
		double vg = v;
		double vb = v * gain_b;

		if (vr > 1.0) vr = 1.0;
		if (vg > 1.0) vg = 1.0;
		if (vb > 1.0) vb = 1.0;

		lut_r[i] = (uint8_t)(pow(vr, 1.0 / 2.2) * 255.0 + 0.5);
		lut_g[i] = (uint8_t)(pow(vg, 1.0 / 2.2) * 255.0 + 0.5);
		lut_b[i] = (uint8_t)(pow(vb, 1.0 / 2.2) * 255.0 + 0.5);
	}

	if (out_min) *out_min = vmin;
	if (out_max) *out_max = vmax;
	if (out_mean) *out_mean = avg_g;
}

/*
 * Fast SGBRG10 -> RGB24 bilinear demosaicing:
 *   Row 2y   (even): G B G B ...
 *   Row 2y+1 (odd):  R G R G ...
 */
static void demosaic_sgbrg10_fast(const uint16_t * restrict src, uint8_t * restrict dst,
				  int w, int h,
				  const uint8_t lut_r[1024],
				  const uint8_t lut_g[1024],
				  const uint8_t lut_b[1024])
{
	/* Interior 2x2 blocks */
	#pragma omp parallel for schedule(static)
	for (int y = 0; y < h; y += 2) {
		const uint16_t *r_prev = src + ((y > 0) ? (y - 1) : 1) * w;
		const uint16_t *r0     = src + y * w;
		const uint16_t *r1     = src + (y + 1) * w;
		const uint16_t *r_next = src + ((y + 2 < h) ? (y + 2) : (y)) * w;

		uint8_t *d0 = dst + (size_t)y * w * 3;
		uint8_t *d1 = dst + (size_t)(y + 1) * w * 3;

		for (int x = 0; x < w; x += 2) {
			int xm1 = (x > 0) ? (x - 1) : 1;
			int xp1 = x + 1;
			int xp2 = (x + 2 < w) ? (x + 2) : x;

			/* (x, y): G site on even row */
			uint16_t g00 = r0[x] & 0x3ff;
			uint16_t b00 = ((r0[xm1] + r0[xp1]) >> 1) & 0x3ff;
			uint16_t r00 = ((r_prev[x] + r1[x]) >> 1) & 0x3ff;

			/* (x+1, y): B site on even row */
			uint16_t b01 = r0[xp1] & 0x3ff;
			uint16_t g01 = ((r0[x] + r0[xp2] + r_prev[xp1] + r1[xp1]) >> 2) & 0x3ff;
			uint16_t r01 = ((r_prev[x] + r_prev[xp2] + r1[x] + r1[xp2]) >> 2) & 0x3ff;

			/* (x, y+1): R site on odd row */
			uint16_t r10 = r1[x] & 0x3ff;
			uint16_t g10 = ((r0[x] + r_next[x] + r1[xm1] + r1[xp1]) >> 2) & 0x3ff;
			uint16_t b10 = ((r0[xm1] + r0[xp1] + r_next[xm1] + r_next[xp1]) >> 2) & 0x3ff;

			/* (x+1, y+1): G site on odd row */
			uint16_t g11 = r1[xp1] & 0x3ff;
			uint16_t r11 = ((r1[x] + r1[xp2]) >> 1) & 0x3ff;
			uint16_t b11 = ((r0[xp1] + r_next[xp1]) >> 1) & 0x3ff;

			d0[0] = lut_r[r00]; d0[1] = lut_g[g00]; d0[2] = lut_b[b00];
			d0[3] = lut_r[r01]; d0[4] = lut_g[g01]; d0[5] = lut_b[b01];

			d1[0] = lut_r[r10]; d1[1] = lut_g[g10]; d1[2] = lut_b[b10];
			d1[3] = lut_r[r11]; d1[4] = lut_g[g11]; d1[5] = lut_b[b11];

			d0 += 6;
			d1 += 6;
		}
	}
}

/*
 * Fast SGBRG10 -> NV12 (BT.601 limited range) direct demosaic + colorspace
 * conversion for feeding Allwinner VE (sunxi-cedrus) H.264 hardware encoder:
 *   Row 2y   (even): G B G B ...
 *   Row 2y+1 (odd):  R G R G ...
 *
 * Writes `w x h` luma into `dst_y` (stride `stride`, padded to `aligned_h`)
 * and `w x (h/2)` interleaved U,V chroma into `dst_uv` (`dst_y + stride * aligned_h`).
 */
static void sgbrg10_to_nv12_fast(const uint16_t * restrict src, uint8_t * restrict dst_nv12,
				 int w, int h, int stride, int aligned_h,
				 const uint8_t lut_r[1024],
				 const uint8_t lut_g[1024],
				 const uint8_t lut_b[1024])
{
	uint8_t *dst_y = dst_nv12;
	uint8_t *dst_uv = dst_nv12 + (size_t)stride * aligned_h;

	#pragma omp parallel for schedule(static)
	for (int y = 0; y < h; y += 2) {
		const uint16_t *r0 = src + (size_t)y * w;
		const uint16_t *r1 = src + (size_t)(y + 1) * w;

		uint8_t *y0 = dst_y + (size_t)y * stride;
		uint8_t *y1 = dst_y + (size_t)(y + 1) * stride;
		uint8_t *uv = dst_uv + (size_t)(y >> 1) * stride;

		for (int x = 0; x < w; x += 2) {
			uint16_t g00_raw = r0[x] & 0x3ff;
			uint16_t b01_raw = r0[x + 1] & 0x3ff;
			uint16_t r10_raw = r1[x] & 0x3ff;
			uint16_t g11_raw = r1[x + 1] & 0x3ff;

			int R = lut_r[r10_raw];
			int B = lut_b[b01_raw];
			int G00 = lut_g[g00_raw];
			int G11 = lut_g[g11_raw];
			int Gavg = (G00 + G11) >> 1;

			int rb_y = 66 * R + 25 * B + 128;
			int y00 = ((rb_y + 129 * G00) >> 8) + 16;
			int y01 = ((rb_y + 129 * Gavg) >> 8) + 16;
			int y10 = y01;
			int y11 = ((rb_y + 129 * G11) >> 8) + 16;

			int u = ((-38 * R - 74 * Gavg + 112 * B + 128) >> 8) + 128;
			int v = ((112 * R - 94 * Gavg - 18 * B + 128) >> 8) + 128;

			y0[x]     = (uint8_t)(y00 < 16 ? 16 : (y00 > 235 ? 235 : y00));
			y0[x + 1] = (uint8_t)(y01 < 16 ? 16 : (y01 > 235 ? 235 : y01));
			y1[x]     = (uint8_t)(y10 < 16 ? 16 : (y10 > 235 ? 235 : y10));
			y1[x + 1] = (uint8_t)(y11 < 16 ? 16 : (y11 > 235 ? 235 : y11));

			uv[x]     = (uint8_t)(u < 16 ? 16 : (u > 240 ? 240 : u));
			uv[x + 1] = (uint8_t)(v < 16 ? 16 : (v > 240 ? 240 : v));
		}
	}

	/* Replicate bottom border into macroblock padding rows (e.g. 1080..1087) */
	for (int y = h; y < aligned_h; y++) {
		memcpy(dst_y + (size_t)y * stride,
		       dst_y + (size_t)(h - 1) * stride, w);
	}
	for (int cy = h / 2; cy < aligned_h / 2; cy++) {
		memcpy(dst_uv + (size_t)cy * stride,
		       dst_uv + (size_t)(h / 2 - 1) * stride, w);
	}
}

static int set_v4l2_ctrl(int fd, uint32_t id, int32_t value)
{
	struct v4l2_control ctrl = {
		.id = id,
		.value = value,
	};
	return ioctl(fd, VIDIOC_S_CTRL, &ctrl);
}

static int find_cedrus_encoder_node(char *path_out, size_t path_len)
{
	for (int i = 0; i < 8; i++) {
		char candidate[32];
		snprintf(candidate, sizeof(candidate), "/dev/video%d", i);
		int fd = open(candidate, O_RDWR);
		if (fd < 0)
			continue;
		struct v4l2_capability cap;
		memset(&cap, 0, sizeof(cap));
		if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
			if (strstr((const char *)cap.card, "cedrus-enc") ||
			    (strcmp((const char *)cap.driver, "cedrus") == 0 &&
			     i > 0)) {
				/* Check if H264 is enumerated on CAPTURE queue */
				struct v4l2_fmtdesc fdesc;
				memset(&fdesc, 0, sizeof(fdesc));
				fdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
				fdesc.index = 0;
				if (ioctl(fd, VIDIOC_ENUM_FMT, &fdesc) == 0 &&
				    fdesc.pixelformat == V4L2_PIX_FMT_H264) {
					close(fd);
					snprintf(path_out, path_len, "%s", candidate);
					return 0;
				}
			}
		}
		close(fd);
	}
	return -1;
}

static int cedrus_enc_open(struct cedrus_encoder *enc, const char *enc_dev,
			   uint32_t width, uint32_t height, uint32_t fps,
			   int quality)
{
	char auto_dev[32];
	memset(enc, 0, sizeof(*enc));
	enc->fd = -1;

	if (!enc_dev || !enc_dev[0]) {
		if (find_cedrus_encoder_node(auto_dev, sizeof(auto_dev)) < 0)
			return -1;
		enc_dev = auto_dev;
	}

	int fd = open(enc_dev, O_RDWR);
	if (fd < 0)
		return -1;

	enc->fd = fd;
	enc->width = width;
	enc->height = height;
	enc->aligned_w = ALIGN_UP(width, 16);
	enc->aligned_h = ALIGN_UP(height, 16);

	/* 1. Configure OUTPUT (Picture: NV12) format */
	struct v4l2_format fmt_out;
	memset(&fmt_out, 0, sizeof(fmt_out));
	fmt_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	fmt_out.fmt.pix.width = enc->aligned_w;
	fmt_out.fmt.pix.height = enc->aligned_h;
	fmt_out.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12;
	fmt_out.fmt.pix.field = V4L2_FIELD_NONE;
	fmt_out.fmt.pix.colorspace = V4L2_COLORSPACE_REC709;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt_out) < 0) {
		perror("cedrus VIDIOC_S_FMT(VIDEO_OUTPUT NV12)");
		close(fd);
		enc->fd = -1;
		return -1;
	}
	enc->aligned_w = fmt_out.fmt.pix.width;
	enc->aligned_h = fmt_out.fmt.pix.height;
	enc->stride = fmt_out.fmt.pix.bytesperline ? fmt_out.fmt.pix.bytesperline : enc->aligned_w;
	enc->nv12_size = fmt_out.fmt.pix.sizeimage;

	/* 2. Configure SPS crop selection if height != aligned_h (e.g. 1080 inside 1088) */
	if (width != enc->aligned_w || height != enc->aligned_h) {
		struct v4l2_selection sel;
		memset(&sel, 0, sizeof(sel));
		sel.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		sel.target = V4L2_SEL_TGT_CROP;
		sel.r.left = 0;
		sel.r.top = 0;
		sel.r.width = width;
		sel.r.height = height;
		ioctl(fd, VIDIOC_S_SELECTION, &sel);
	}

	/* 3. Set framerate on OUTPUT queue */
	struct v4l2_streamparm parm;
	memset(&parm, 0, sizeof(parm));
	parm.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	parm.parm.output.timeperframe.numerator = 1;
	parm.parm.output.timeperframe.denominator = fps ? fps : 30;
	ioctl(fd, VIDIOC_S_PARM, &parm);

	/* 4. Configure CAPTURE (Coded: H264) format */
	struct v4l2_format fmt_cap;
	memset(&fmt_cap, 0, sizeof(fmt_cap));
	fmt_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt_cap.fmt.pix.width = enc->aligned_w;
	fmt_cap.fmt.pix.height = enc->aligned_h;
	fmt_cap.fmt.pix.pixelformat = V4L2_PIX_FMT_H264;
	fmt_cap.fmt.pix.sizeimage = 1024 * 1024;
	fmt_cap.fmt.pix.field = V4L2_FIELD_NONE;
	if (ioctl(fd, VIDIOC_S_FMT, &fmt_cap) < 0) {
		perror("cedrus VIDIOC_S_FMT(VIDEO_CAPTURE H264)");
		close(fd);
		enc->fd = -1;
		return -1;
	}

	/* 5. Configure H.264 encoder controls */
	int qp_i = 42 - (quality * 22) / 100; /* q=85 -> QP 23 */
	if (qp_i < 16) qp_i = 16;
	if (qp_i > 42) qp_i = 42;
	int qp_p = qp_i + 2;

	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		      V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		      V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
		      V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR, 1);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_CLOSURE, 1);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_GOP_SIZE, fps ? fps : 30);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD, fps ? fps : 30);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, qp_i);
	set_v4l2_ctrl(fd, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, qp_p);

	/* 6. Request & mmap OUTPUT buffers */
	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count = ENC_BUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("cedrus VIDIOC_REQBUFS(OUTPUT)");
		close(fd);
		enc->fd = -1;
		return -1;
	}
	enc->out_count = req.count;
	for (unsigned int i = 0; i < enc->out_count; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0)
			return -1;
		enc->out_bufs[i].length = b.length;
		enc->out_bufs[i].start = mmap(NULL, b.length,
					      PROT_READ | PROT_WRITE,
					      MAP_SHARED, fd, b.m.offset);
		if (enc->out_bufs[i].start == MAP_FAILED)
			return -1;
	}

	/* 7. Request, mmap, and queue CAPTURE buffers */
	memset(&req, 0, sizeof(req));
	req.count = ENC_BUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;
	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("cedrus VIDIOC_REQBUFS(CAPTURE)");
		return -1;
	}
	enc->cap_count = req.count;
	for (unsigned int i = 0; i < enc->cap_count; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0)
			return -1;
		enc->cap_bufs[i].length = b.length;
		enc->cap_bufs[i].start = mmap(NULL, b.length,
					      PROT_READ | PROT_WRITE,
					      MAP_SHARED, fd, b.m.offset);
		if (enc->cap_bufs[i].start == MAP_FAILED)
			return -1;
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0)
			return -1;
	}

	/* 8. Start streaming on CAPTURE and OUTPUT queues */
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("cedrus VIDIOC_STREAMON(CAPTURE)");
		return -1;
	}
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("cedrus VIDIOC_STREAMON(OUTPUT)");
		return -1;
	}

	printf("Opened Allwinner VE H.264 hardware encoder (%s: %ux%u -> %ux%u NV12->H264 CABAC QP=%d/%d)\n",
	       enc_dev, width, height, enc->aligned_w, enc->aligned_h, qp_i, qp_p);
	return 0;
}

static int cedrus_enc_encode_frame(struct cedrus_encoder *enc,
				   const uint16_t *raw_sgbrg10,
				   uint8_t *nv12_cached,
				   const uint8_t lut_r[1024],
				   const uint8_t lut_g[1024],
				   const uint8_t lut_b[1024],
				   const void **bitstream_out,
				   uint32_t *bytes_out,
				   uint32_t *is_keyframe_out,
				   int *cap_buf_idx_out)
{
	/*
	 * Convert SGBRG10 into cached L1/L2 NV12 buffer first, then burst-copy
	 * into the uncached DMA-coherent V4L2 OUTPUT buffer using 128-bit NEON stp.
	 */
	sgbrg10_to_nv12_fast(raw_sgbrg10, nv12_cached,
			     enc->width, enc->height,
			     enc->stride, enc->aligned_h,
			     lut_r, lut_g, lut_b);
	memcpy(enc->out_bufs[0].start, nv12_cached, enc->nv12_size);

	struct v4l2_buffer b_out;
	memset(&b_out, 0, sizeof(b_out));
	b_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	b_out.memory = V4L2_MEMORY_MMAP;
	b_out.index = 0;
	b_out.bytesused = enc->nv12_size;
	b_out.field = V4L2_FIELD_NONE;

	if (ioctl(enc->fd, VIDIOC_QBUF, &b_out) < 0) {
		perror("cedrus VIDIOC_QBUF(OUTPUT)");
		return -1;
	}

	struct v4l2_buffer b_cap;
	memset(&b_cap, 0, sizeof(b_cap));
	b_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b_cap.memory = V4L2_MEMORY_MMAP;
	if (ioctl(enc->fd, VIDIOC_DQBUF, &b_cap) < 0) {
		perror("cedrus VIDIOC_DQBUF(CAPTURE)");
		return -1;
	}

	if (ioctl(enc->fd, VIDIOC_DQBUF, &b_out) < 0) {
		perror("cedrus VIDIOC_DQBUF(OUTPUT)");
		return -1;
	}

	*bitstream_out = enc->cap_bufs[b_cap.index].start;
	*bytes_out = b_cap.bytesused;
	*is_keyframe_out = (b_cap.flags & V4L2_BUF_FLAG_KEYFRAME) ? 1 : 0;
	*cap_buf_idx_out = b_cap.index;
	return 0;
}

static int cedrus_enc_release_cap_buf(struct cedrus_encoder *enc, int cap_idx)
{
	struct v4l2_buffer b_cap;
	memset(&b_cap, 0, sizeof(b_cap));
	b_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	b_cap.memory = V4L2_MEMORY_MMAP;
	b_cap.index = cap_idx;
	return ioctl(enc->fd, VIDIOC_QBUF, &b_cap);
}

static void cedrus_enc_close(struct cedrus_encoder *enc)
{
	if (enc->fd < 0)
		return;
	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	ioctl(enc->fd, VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	ioctl(enc->fd, VIDIOC_STREAMOFF, &type);

	for (unsigned int i = 0; i < enc->out_count; i++) {
		if (enc->out_bufs[i].start && enc->out_bufs[i].start != MAP_FAILED)
			munmap(enc->out_bufs[i].start, enc->out_bufs[i].length);
	}
	for (unsigned int i = 0; i < enc->cap_count; i++) {
		if (enc->cap_bufs[i].start && enc->cap_bufs[i].start != MAP_FAILED)
			munmap(enc->cap_bufs[i].start, enc->cap_bufs[i].length);
	}
	close(enc->fd);
	enc->fd = -1;
}

static int encode_jpeg_to_mem(const uint8_t *rgb, int width, int height,
			      int quality, unsigned char **out_buf,
			      unsigned long *out_size)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	JSAMPROW row_pointer[1];

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);

	*out_buf = NULL;
	*out_size = 0;
	jpeg_mem_dest(&cinfo, out_buf, out_size);

	cinfo.image_width = width;
	cinfo.image_height = height;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;

	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	cinfo.dct_method = JDCT_IFAST;

	jpeg_start_compress(&cinfo, TRUE);
	while (cinfo.next_scanline < cinfo.image_height) {
		row_pointer[0] = (JSAMPROW)(rgb + (size_t)cinfo.next_scanline * width * 3);
		jpeg_write_scanlines(&cinfo, row_pointer, 1);
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	return 0;
}

static int has_suffix(const char *str, const char *suf)
{
	size_t l1 = strlen(str), l2 = strlen(suf);
	return (l1 >= l2) && (strcasecmp(str + l1 - l2, suf) == 0);
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [-d /dev/video0] [-e /dev/video1] [-o output.(h264|avi|jpg|ppm|raw)] [-n frames] [-s skip] [-q quality] [-m]\n"
		"  -d <dev>     V4L2 video capture device (default: /dev/video0)\n"
		"  -e <enc_dev> V4L2 M2M hardware encoder device (auto-detects cedrus-enc)\n"
		"  -o <file>    Output path (.h264 HW H.264, .avi HW H.264/MJPEG, .jpg, .ppm, .raw)\n"
		"  -n <frames>  Number of frames to record (default: 1 for still, 90 for video)\n"
		"  -s <skip>    Warmup frames to skip for AEC/AGC settle (default: 6)\n"
		"  -q <quality> Quality 1..100 (default: 85)\n"
		"  -m           Force software MJPEG for .avi instead of hardware H.264\n",
		prog);
}

int main(int argc, char **argv)
{
	const char *dev_path = "/dev/video0";
	const char *enc_path = NULL;
	const char *out_path = "/tmp/capture.jpg";
	int skip_frames = 6;
	int num_frames = -1;
	int quality = 85;
	int force_mjpeg = 0;
	uint32_t width = 1920, height = 1080, fps = 30;
	int opt;

	while ((opt = getopt(argc, argv, "d:e:o:n:s:q:mh")) != -1) {
		switch (opt) {
		case 'd':
			dev_path = optarg;
			break;
		case 'e':
			enc_path = optarg;
			break;
		case 'o':
			out_path = optarg;
			break;
		case 'n':
			num_frames = atoi(optarg);
			break;
		case 's':
			skip_frames = atoi(optarg);
			if (skip_frames < 0)
				skip_frames = 0;
			break;
		case 'q':
			quality = atoi(optarg);
			if (quality < 10) quality = 10;
			if (quality > 100) quality = 100;
			break;
		case 'm':
			force_mjpeg = 1;
			break;
		default:
			usage(argv[0]);
			return 1;
		}
	}

	int is_h264 = has_suffix(out_path, ".h264") || has_suffix(out_path, ".264");
	int is_avi = has_suffix(out_path, ".avi");
	int is_jpg = has_suffix(out_path, ".jpg") || has_suffix(out_path, ".jpeg");
	int is_raw = has_suffix(out_path, ".raw");

	if (num_frames <= 0)
		num_frames = (is_avi || is_h264) ? 90 : 1;

	/* Ensure media pipeline and 30fps vblank=79 are configured */
	if (access("/usr/bin/media-ctl", X_OK) == 0) {
		system("media-ctl -d /dev/media0 -V '\"ov5647 3-0036\":0 [fmt:SGBRG10_1X10/1920x1080]' >/dev/null 2>&1");
		system("media-ctl -d /dev/media0 -V '\"sun6i-mipi-csi2\":0 [fmt:SGBRG10_1X10/1920x1080]' >/dev/null 2>&1");
		system("media-ctl -d /dev/media0 -V '\"sun6i-mipi-csi2\":1 [fmt:SGBRG10_1X10/1920x1080]' >/dev/null 2>&1");
		system("media-ctl -d /dev/media0 -V '\"sun6i-csi-bridge\":0 [fmt:SGBRG10_1X10/1920x1080]' >/dev/null 2>&1");
		system("media-ctl -d /dev/media0 -V '\"sun6i-csi-bridge\":1 [fmt:SGBRG10_1X10/1920x1080]' >/dev/null 2>&1");
	}
	if (access("/usr/bin/v4l2-ctl", X_OK) == 0) {
		system("v4l2-ctl -d /dev/v4l-subdev2 --set-ctrl=vertical_blanking=79,exposure_auto=0,autogain=1 >/dev/null 2>&1");
	}

	int fd = open(dev_path, O_RDWR);
	if (fd < 0) {
		perror("open video device");
		return 1;
	}

	struct v4l2_format fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = width;
	fmt.fmt.pix.height = height;
	fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_SGBRG10;
	fmt.fmt.pix.field = V4L2_FIELD_NONE;

	if (ioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
		perror("VIDIOC_S_FMT");
		close(fd);
		return 1;
	}
	width = fmt.fmt.pix.width;
	height = fmt.fmt.pix.height;

	struct v4l2_requestbuffers req;
	memset(&req, 0, sizeof(req));
	req.count = MAX_V4L2_BUFS;
	req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	req.memory = V4L2_MEMORY_MMAP;

	if (ioctl(fd, VIDIOC_REQBUFS, &req) < 0) {
		perror("VIDIOC_REQBUFS");
		close(fd);
		return 1;
	}

	struct mmap_buf bufs[MAX_V4L2_BUFS];
	memset(bufs, 0, sizeof(bufs));

	for (unsigned int i = 0; i < req.count; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		b.index = i;
		if (ioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
			perror("VIDIOC_QUERYBUF");
			return 1;
		}
		bufs[i].length = b.length;
		bufs[i].start = mmap(NULL, b.length, PROT_READ | PROT_WRITE,
				     MAP_SHARED, fd, b.m.offset);
		if (bufs[i].start == MAP_FAILED) {
			perror("mmap");
			return 1;
		}
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0) {
			perror("VIDIOC_QBUF");
			return 1;
		}
	}

	enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
		perror("VIDIOC_STREAMON");
		return 1;
	}

	uint8_t *rgb = malloc((size_t)width * ALIGN_UP(height, 16) * 3);
	uint16_t *raw_cached = malloc((size_t)width * height * sizeof(uint16_t));
	if (!rgb || !raw_cached) {
		perror("malloc buffers");
		return 1;
	}

	uint8_t lut_r[1024], lut_g[1024], lut_b[1024];
	int luts_ready = 0;
	uint16_t vmin = 0, vmax = 0;
	double vmean = 0.0;

	/* Discard warmup frames so sensor AEC/AGC settles */
	for (int i = 0; i < skip_frames; i++) {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
			perror("VIDIOC_DQBUF warmup");
			return 1;
		}
		if (i == skip_frames - 1) {
			build_color_luts((const uint16_t *)bufs[b.index].start,
					 width, height, lut_r, lut_g, lut_b,
					 &vmin, &vmax, &vmean);
			luts_ready = 1;
		}
		if (ioctl(fd, VIDIOC_QBUF, &b) < 0) {
			perror("VIDIOC_QBUF warmup");
			return 1;
		}
	}

	struct timeval tv_start, tv_end;
	gettimeofday(&tv_start, NULL);

	if (is_h264 || is_avi) {
		struct cedrus_encoder enc;
		int use_hw_h264 = 0;

		if (!force_mjpeg) {
			if (cedrus_enc_open(&enc, enc_path, width, height, fps, quality) == 0) {
				use_hw_h264 = 1;
			} else if (is_h264) {
				fprintf(stderr, "Error: hardware H.264 encoder (cedrus-enc) not available for %s\n",
					out_path);
				return 1;
			}
		}

		FILE *fp = fopen(out_path, "wb");
		if (!fp) {
			perror("fopen video output");
			return 1;
		}

		struct avi_idx_entry *idx = NULL;
		if (is_avi) {
			idx = calloc(num_frames, sizeof(*idx));
			if (!idx) {
				perror("calloc idx");
				fclose(fp);
				return 1;
			}
			avi_write_header(fp, width, height, fps, num_frames, 4, 216,
					 use_hw_h264 ? "H264" : "MJPG");
		}

		uint32_t movi_bytes = 4; /* 'movi' fourcc */
		uint64_t total_payload_bytes = 0;

		for (int f = 0; f < num_frames; f++) {
			struct v4l2_buffer b;
			memset(&b, 0, sizeof(b));
			b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			b.memory = V4L2_MEMORY_MMAP;
			if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
				perror("VIDIOC_DQBUF video");
				return 1;
			}

			/*
			 * Copy from uncached DMA-coherent mmap buffer into cached
			 * L1/L2 memory using 128-bit burst memcpy before random-access
			 * 2D stencil conversion, and immediately re-queue the V4L2 buffer.
			 */
			memcpy(raw_cached, bufs[b.index].start, (size_t)width * height * 2);
			if (ioctl(fd, VIDIOC_QBUF, &b) < 0) {
				perror("VIDIOC_QBUF video");
				return 1;
			}

			const uint16_t *src = raw_cached;
			if (!luts_ready || (f % 30 == 0)) {
				build_color_luts(src, width, height, lut_r, lut_g, lut_b,
						 &vmin, &vmax, &vmean);
				luts_ready = 1;
			}

			if (use_hw_h264) {
				const void *bitstream = NULL;
				uint32_t bs_size = 0;
				uint32_t is_key = 0;
				int cap_idx = -1;

				if (cedrus_enc_encode_frame(&enc, src, rgb,
							    lut_r, lut_g, lut_b,
							    &bitstream, &bs_size,
							    &is_key, &cap_idx) < 0) {
					fprintf(stderr, "Hardware H.264 encode failed at frame %d\n", f);
					return 1;
				}

				const uint8_t *bsp = (const uint8_t *)bitstream;
				printf("DEBUG FRAME %d: cap_idx=%d bs_size=%u is_key=%u | %02x %02x %02x %02x %02x %02x %02x %02x\n",
				       f, cap_idx, bs_size, is_key,
				       bsp[0], bsp[1], bsp[2], bsp[3],
				       bsp[4], bsp[5], bsp[6], bsp[7]);

				if (is_h264) {
					fwrite(bitstream, 1, bs_size, fp);
				} else {
					uint32_t padded_size = (bs_size + 1U) & ~1U;
					idx[f].offset = movi_bytes;
					idx[f].size = bs_size;
					idx[f].is_keyframe = is_key;

					fwrite("00dc", 1, 4, fp);
					fput_le32(fp, bs_size);
					fwrite(bitstream, 1, bs_size, fp);
					if (padded_size > bs_size) {
						uint8_t zero = 0;
						fwrite(&zero, 1, 1, fp);
					}
					movi_bytes += 8 + padded_size;
				}
				total_payload_bytes += bs_size;
				cedrus_enc_release_cap_buf(&enc, cap_idx);
			} else {
				demosaic_sgbrg10_fast(src, rgb, width, height, lut_r, lut_g, lut_b);

				unsigned char *jpg_buf = NULL;
				unsigned long jpg_size = 0;
				encode_jpeg_to_mem(rgb, width, height, quality, &jpg_buf, &jpg_size);

				uint32_t padded_size = (uint32_t)((jpg_size + 1) & ~1UL);
				idx[f].offset = movi_bytes;
				idx[f].size = (uint32_t)jpg_size;
				idx[f].is_keyframe = 1;

				fwrite("00dc", 1, 4, fp);
				fput_le32(fp, (uint32_t)jpg_size);
				fwrite(jpg_buf, 1, jpg_size, fp);
				if (padded_size > jpg_size) {
					uint8_t zero = 0;
					fwrite(&zero, 1, 1, fp);
				}
				free(jpg_buf);

				movi_bytes += 8 + padded_size;
				total_payload_bytes += jpg_size;
			}
		}

		if (use_hw_h264)
			cedrus_enc_close(&enc);

		uint32_t total_file_bytes = (uint32_t)total_payload_bytes;
		if (is_avi) {
			/* Write idx1 chunk */
			fwrite("idx1", 1, 4, fp);
			fput_le32(fp, num_frames * 16);
			for (int f = 0; f < num_frames; f++) {
				fwrite("00dc", 1, 4, fp);
				fput_le32(fp, idx[f].is_keyframe ? 0x10 : 0x00);
				fput_le32(fp, idx[f].offset);
				fput_le32(fp, idx[f].size);
			}

			total_file_bytes = 216 + movi_bytes + 8 + num_frames * 16;
			avi_write_header(fp, width, height, fps, num_frames, movi_bytes,
					 total_file_bytes, use_hw_h264 ? "H264" : "MJPG");
			free(idx);
		}

		fflush(fp);
		fsync(fileno(fp));
		fclose(fp);

		gettimeofday(&tv_end, NULL);
		double elapsed = (tv_end.tv_sec - tv_start.tv_sec) +
				 (tv_end.tv_usec - tv_start.tv_usec) / 1e6;
		double actual_fps = (elapsed > 0.0) ? (num_frames / elapsed) : 0.0;
		double mbps = (elapsed > 0.0) ? ((total_payload_bytes * 8.0) / (elapsed * 1e6)) : 0.0;

		printf("Recorded %d frames (%ux%u %s q=%d) -> %s (%.2f MB, %.2f fps, %.2f Mbps)\n",
		       num_frames, width, height,
		       use_hw_h264 ? "HW-H264 (sunxi-cedrus)" : "SW-MJPEG",
		       quality, out_path,
		       (double)total_file_bytes / (1024.0 * 1024.0), actual_fps, mbps);
	} else {
		struct v4l2_buffer b;
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		b.memory = V4L2_MEMORY_MMAP;
		if (ioctl(fd, VIDIOC_DQBUF, &b) < 0) {
			perror("VIDIOC_DQBUF still");
			return 1;
		}

		memcpy(raw_cached, bufs[b.index].start, (size_t)width * height * 2);
		const uint16_t *src = raw_cached;
		if (!luts_ready) {
			build_color_luts(src, width, height, lut_r, lut_g, lut_b,
					 &vmin, &vmax, &vmean);
		}

		FILE *fp = fopen(out_path, "wb");
		if (!fp) {
			perror("fopen output");
			return 1;
		}

		if (is_raw) {
			fwrite(src, 1, b.bytesused, fp);
			fclose(fp);
			printf("Captured %ux%u SGBRG10 RAW -> %s (%u bytes)\n",
			       width, height, out_path, b.bytesused);
		} else if (is_jpg) {
			demosaic_sgbrg10_fast(src, rgb, width, height, lut_r, lut_g, lut_b);
			unsigned char *jpg_buf = NULL;
			unsigned long jpg_size = 0;
			encode_jpeg_to_mem(rgb, width, height, quality, &jpg_buf, &jpg_size);
			fwrite(jpg_buf, 1, jpg_size, fp);
			fflush(fp);
			fsync(fileno(fp));
			fclose(fp);
			free(jpg_buf);
			printf("Captured %ux%u JPEG (q=%d) -> %s (%lu bytes, min=%u max=%u mean=%.1f)\n",
			       width, height, quality, out_path, jpg_size, vmin, vmax, vmean);
		} else {
			demosaic_sgbrg10_fast(src, rgb, width, height, lut_r, lut_g, lut_b);
			fprintf(fp, "P6\n%u %u\n255\n", width, height);
			fwrite(rgb, 1, (size_t)width * height * 3, fp);
			fclose(fp);
			printf("Captured %ux%u SGBRG10 -> %s (min=%u max=%u mean=%.1f)\n",
			       width, height, out_path, vmin, vmax, vmean);
		}

		ioctl(fd, VIDIOC_QBUF, &b);
	}

	free(raw_cached);
	free(rgb);
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	for (unsigned int i = 0; i < req.count; i++)
		munmap(bufs[i].start, bufs[i].length);
	close(fd);
	return 0;
}
