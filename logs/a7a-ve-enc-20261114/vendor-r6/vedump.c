/* LD_PRELOAD shim: on the encoder's IOCTL_WAIT_VE_EN (0x103) snapshot VE_ENC and IOMMU
 * registers from /dev/mem before waiting. Raw syscalls only (no libc dependency). */
typedef unsigned long u64; typedef unsigned int u32;
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
	register long x8 __asm__("x8") = n; register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b; register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d; register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;
	__asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory");
	return x0;
}
#define SYS_openat 56
#define SYS_close 57
#define SYS_write 64
#define SYS_ioctl 29
#define SYS_mmap 222
#define SYS_munmap 215
static int count;
static u32 buf[0x2000 / 4];
static void snap(u64 phys, u32 from, u32 to, u32 *dst) {
	int fd = sc6(SYS_openat, -100, (long)"/dev/mem", 0 | 04010000, 0, 0, 0);
	if (fd < 0) return;
	volatile u32 *p = (volatile u32 *)sc6(SYS_mmap, 0, 0x1000, 1, 1, fd, phys);
	if ((long)p > 0 || (long)p < -4096) {
		for (u32 o = from; o < to; o += 4) dst[o / 4] = p[o / 4];
		sc6(SYS_munmap, (long)p, 0x1000, 0, 0, 0, 0);
	}
	sc6(SYS_close, fd, 0, 0, 0, 0, 0);
}
int ioctl(int fd, unsigned long req, void *arg) {
	if (req == 0x103 && count < 4) {
		for (int i = 0; i < 0x2000 / 4; i++) buf[i] = 0xdeadbeef;
		snap(0x01c10000, 0x800, 0x900, buf);          /* ENC_TOP */
		snap(0x01c10000, 0xa00, 0xc00, buf);          /* ENCPP/ISP + AVC */
		snap(0x03900000, 0x000, 0x100, buf + 0x400);  /* IOMMU global */
		snap(0x03900000, 0xa00, 0xc00, buf + 0x400);  /* IOMMU master 3 */
		char path[] = "/root/venc-dump/regs_0.bin";
		path[21] = '0' + count;
		int o = sc6(SYS_openat, -100, (long)path, 01 | 0100 | 01000, 0644, 0, 0);
		if (o >= 0) { sc6(SYS_write, o, (long)buf, sizeof(buf), 0, 0, 0); sc6(SYS_close, o, 0, 0, 0, 0, 0); }
		count++;
	}
	return sc6(SYS_ioctl, fd, req, (long)arg, 0, 0, 0);
}
