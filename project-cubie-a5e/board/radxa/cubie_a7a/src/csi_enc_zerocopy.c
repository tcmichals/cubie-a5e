// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * csi_enc_zerocopy.c - Zero-copy CSI to H.264 Cedrus encoder test
 *
 * Imports CMA dma-heap buffers into CSI capture (/dev/video0) and
 * Cedrus encoder (/dev/video1) without CPU mapping or copying.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/dma-heap.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#define NUM_BUFS 4
#define WIDTH 1920
#define HEIGHT 1080

// NV12 size for 1080p (aligned to 1088 for guard): 1920 * 1088 * 3 / 2 = 3,133,440 (or 1920*1080*3/2 = 3,110,400)
// CSI GB10 requires 4,147,200 bytes.
// We allocate 4,194,304 bytes (4 MiB) from CMA heap so both CSI and Cedrus are satisfied.
#define BUFFER_SIZE (4 * 1024 * 1024)

static int xioctl(int fh, int request, void *arg)
{
	int r;
	do {
		r = ioctl(fh, request, arg);
	} while (-1 == r && EINTR == errno);
	return r;
}

int main(int argc, char **argv)
{
	int num_frames = 300;
	if (argc > 1)
		num_frames = atoi(argv[1]);

	printf("=== CSI -> Cedrus Zero-Copy Test (%d frames) ===\n", num_frames);

	int heap_fd = open("/dev/dma_heap/default_cma_region", O_RDWR);
	if (heap_fd < 0) {
		perror("open /dev/dma_heap/default_cma_region");
		return 1;
	}

	int buf_fds[NUM_BUFS];
	for (int i = 0; i < NUM_BUFS; i++) {
		struct dma_heap_allocation_data alloc = {
			.len = BUFFER_SIZE,
			.fd_flags = O_RDWR | O_CLOEXEC,
		};
		if (xioctl(heap_fd, DMA_HEAP_IOCTL_ALLOC, &alloc) < 0) {
			perror("DMA_HEAP_IOCTL_ALLOC");
			return 1;
		}
		buf_fds[i] = alloc.fd;

		// Initialize the chroma plane to 0x80 once at setup as specified in guide §3.3
		void *p = mmap(NULL, BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, buf_fds[i], 0);
		if (p != MAP_FAILED) {
			memset((uint8_t *)p + (WIDTH * HEIGHT), 0x80, (WIDTH * HEIGHT) / 2);
			munmap(p, BUFFER_SIZE);
		}
	}
	close(heap_fd);
	printf("Allocated %d CMA dma-bufs of %d bytes each\n", NUM_BUFS, BUFFER_SIZE);

	// Open CSI (/dev/video0)
	int csi_fd = open("/dev/video0", O_RDWR);
	if (csi_fd < 0) {
		perror("open /dev/video0");
		return 1;
	}

	struct v4l2_format csi_fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix.width = WIDTH,
		.fmt.pix.height = HEIGHT,
		.fmt.pix.pixelformat = V4L2_PIX_FMT_SGBRG10,
		.fmt.pix.field = V4L2_FIELD_NONE,
	};
	if (xioctl(csi_fd, VIDIOC_S_FMT, &csi_fmt) < 0) {
		perror("CSI VIDIOC_S_FMT");
		return 1;
	}

	struct v4l2_requestbuffers csi_req = {
		.count = NUM_BUFS,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_DMABUF,
	};
	if (xioctl(csi_fd, VIDIOC_REQBUFS, &csi_req) < 0) {
		perror("CSI VIDIOC_REQBUFS");
		return 1;
	}

	// Open Cedrus (/dev/video1)
	int enc_fd = open("/dev/video1", O_RDWR);
	if (enc_fd < 0) {
		perror("open /dev/video1");
		return 1;
	}

	struct v4l2_format enc_out_fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
		.fmt.pix.width = WIDTH,
		.fmt.pix.height = HEIGHT,
		.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12,
		.fmt.pix.field = V4L2_FIELD_NONE,
	};
	if (xioctl(enc_fd, VIDIOC_S_FMT, &enc_out_fmt) < 0) {
		perror("Cedrus out VIDIOC_S_FMT");
		return 1;
	}

	struct v4l2_format enc_cap_fmt = {
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.fmt.pix.pixelformat = V4L2_PIX_FMT_H264,
	};
	if (xioctl(enc_fd, VIDIOC_S_FMT, &enc_cap_fmt) < 0) {
		perror("Cedrus cap VIDIOC_S_FMT");
		return 1;
	}

	// Set controls: High Profile, CABAC, GOP 30
	struct v4l2_control ctrl;
	ctrl.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE;
	ctrl.value = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH;
	xioctl(enc_fd, VIDIOC_S_CTRL, &ctrl);

	ctrl.id = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE;
	ctrl.value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC;
	xioctl(enc_fd, VIDIOC_S_CTRL, &ctrl);

	ctrl.id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD;
	ctrl.value = 30;
	xioctl(enc_fd, VIDIOC_S_CTRL, &ctrl);

	struct v4l2_requestbuffers enc_out_req = {
		.count = NUM_BUFS,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
		.memory = V4L2_MEMORY_DMABUF,
	};
	if (xioctl(enc_fd, VIDIOC_REQBUFS, &enc_out_req) < 0) {
		perror("Cedrus out VIDIOC_REQBUFS");
		return 1;
	}

	struct v4l2_requestbuffers enc_cap_req = {
		.count = NUM_BUFS,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
		.memory = V4L2_MEMORY_MMAP,
	};
	if (xioctl(enc_fd, VIDIOC_REQBUFS, &enc_cap_req) < 0) {
		perror("Cedrus cap VIDIOC_REQBUFS");
		return 1;
	}

	struct {
		void *start;
		size_t length;
	} enc_cap_bufs[NUM_BUFS];

	for (int i = 0; i < NUM_BUFS; i++) {
		struct v4l2_buffer b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
			.index = i,
		};
		if (xioctl(enc_fd, VIDIOC_QUERYBUF, &b) < 0) {
			perror("Cedrus VIDIOC_QUERYBUF");
			return 1;
		}
		enc_cap_bufs[i].length = b.length;
		enc_cap_bufs[i].start = mmap(NULL, b.length, PROT_READ, MAP_SHARED, enc_fd, b.m.offset);
		if (enc_cap_bufs[i].start == MAP_FAILED) {
			perror("mmap Cedrus cap");
			return 1;
		}
		if (xioctl(enc_fd, VIDIOC_QBUF, &b) < 0) {
			perror("Cedrus cap VIDIOC_QBUF");
			return 1;
		}
	}

	// Queue all DMABUFs to CSI initially
	for (int i = 0; i < NUM_BUFS; i++) {
		struct v4l2_buffer b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_DMABUF,
			.index = i,
			.m.fd = buf_fds[i],
			.length = BUFFER_SIZE,
		};
		if (xioctl(csi_fd, VIDIOC_QBUF, &b) < 0) {
			perror("CSI VIDIOC_QBUF init");
			return 1;
		}
	}

	int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(csi_fd, VIDIOC_STREAMON, &type) < 0) {
		perror("CSI VIDIOC_STREAMON");
		return 1;
	}

	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	if (xioctl(enc_fd, VIDIOC_STREAMON, &type) < 0) {
		perror("Cedrus out VIDIOC_STREAMON");
		return 1;
	}

	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (xioctl(enc_fd, VIDIOC_STREAMON, &type) < 0) {
		perror("Cedrus cap VIDIOC_STREAMON");
		return 1;
	}

	printf("Started zero-copy pipeline: CSI capture -> Cedrus encoder\n");

	int out_file = open("/tmp/csi_enc_raw.h264", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out_file < 0) {
		perror("open /tmp/csi_enc_raw.h264");
		return 1;
	}

	struct timeval t_start, t_end;
	gettimeofday(&t_start, NULL);

	for (int frame = 0; frame < num_frames; frame++) {
		// 1. DQBUF from CSI
		struct v4l2_buffer csi_b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_DMABUF,
		};
		if (xioctl(csi_fd, VIDIOC_DQBUF, &csi_b) < 0) {
			perror("CSI VIDIOC_DQBUF");
			break;
		}

		int idx = csi_b.index;

		// 2. QBUF the exact same DMABUF to Cedrus encoder OUTPUT
		struct v4l2_buffer enc_out_b = {
			.type = V4L2_BUF_TYPE_VIDEO_OUTPUT,
			.memory = V4L2_MEMORY_DMABUF,
			.index = idx,
			.m.fd = buf_fds[idx],
			.length = BUFFER_SIZE,
			.bytesused = WIDTH * HEIGHT * 3 / 2,
			.timestamp = csi_b.timestamp,
		};
		if (xioctl(enc_fd, VIDIOC_QBUF, &enc_out_b) < 0) {
			perror("Cedrus out VIDIOC_QBUF");
			break;
		}

		// 3. DQBUF the encoded H.264 slice from Cedrus CAPTURE
		struct v4l2_buffer enc_cap_b = {
			.type = V4L2_BUF_TYPE_VIDEO_CAPTURE,
			.memory = V4L2_MEMORY_MMAP,
		};
		if (xioctl(enc_fd, VIDIOC_DQBUF, &enc_cap_b) < 0) {
			perror("Cedrus cap VIDIOC_DQBUF");
			break;
		}

		// Write bitstream to file
		if (write(out_file, enc_cap_bufs[enc_cap_b.index].start, enc_cap_b.bytesused) < 0) {
			perror("write bitstream");
		}

		// Re-QBUF Cedrus CAPTURE buffer
		if (xioctl(enc_fd, VIDIOC_QBUF, &enc_cap_b) < 0) {
			perror("Cedrus cap re-QBUF");
			break;
		}

		// 4. DQBUF from Cedrus OUTPUT (encoder is done with input)
		if (xioctl(enc_fd, VIDIOC_DQBUF, &enc_out_b) < 0) {
			perror("Cedrus out VIDIOC_DQBUF");
			break;
		}

		// 5. QBUF back to CSI
		csi_b.m.fd = buf_fds[idx];
		csi_b.length = BUFFER_SIZE;
		if (xioctl(csi_fd, VIDIOC_QBUF, &csi_b) < 0) {
			perror("CSI re-QBUF");
			break;
		}

		if ((frame + 1) % 30 == 0) {
			printf("Frame %d / %d processed\n", frame + 1, num_frames);
		}
	}

	gettimeofday(&t_end, NULL);
	double elapsed = (t_end.tv_sec - t_start.tv_sec) + (t_end.tv_usec - t_start.tv_usec) / 1000000.0;
	printf("Encoded %d frames in %.2f s (%.2f fps)\n", num_frames, elapsed, num_frames / elapsed);

	close(out_file);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(csi_fd, VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	xioctl(enc_fd, VIDIOC_STREAMOFF, &type);
	type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	xioctl(enc_fd, VIDIOC_STREAMOFF, &type);

	close(csi_fd);
	close(enc_fd);
	for (int i = 0; i < NUM_BUFS; i++)
		close(buf_fds[i]);

	printf("Done! Output saved to /tmp/csi_enc_raw.h264\n");
	return 0;
}
