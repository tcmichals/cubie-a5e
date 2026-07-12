/*
 * rpmsg_host_example.c - ARM Linux RPMsg Userspace Example
 *
 * After the remoteproc driver loads firmware.elf and the co-processor
 * announces its RPMsg virtio device, the kernel creates /dev/rpmsg_ctrlX
 * and, once an endpoint is opened, /dev/rpmsgX character devices.
 *
 * This program:
 *  1. Opens the rpmsg control device and creates an endpoint.
 *  2. Sends a "hello" string to the RISC-V co-processor.
 *  3. Reads and prints the echo response.
 *
 * Build on the Cubie A5E target:
 *   gcc rpmsg_host_example.c -o rpmsg_host_example
 *
 * Run:
 *   # First ensure the co-processor is running:
 *   echo firmware.elf > /sys/class/remoteproc/remoteproc0/firmware
 *   echo start       > /sys/class/remoteproc/remoteproc0/state
 *   # Then run this tool:
 *   ./rpmsg_host_example /dev/rpmsg0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/rpmsg.h>

#define RPMSG_ADDR_ANY  0xFFFFFFFF

int main(int argc, char **argv) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/rpmsg0";
    char buf[512];
    ssize_t n;

    printf("[Host RPMsg] Opening %s\n", dev);
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror("open rpmsg device");
        fprintf(stderr, "Hint: is the co-processor running?\n"
                "  echo firmware.elf > /sys/class/remoteproc/remoteproc0/firmware\n"
                "  echo start       > /sys/class/remoteproc/remoteproc0/state\n");
        return 1;
    }

    /* Send a greeting to the RISC-V co-processor */
    const char *msg = "hello from ARM Linux";
    printf("[Host RPMsg] Sending: \"%s\"\n", msg);
    if (write(fd, msg, strlen(msg)) < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    /* Read the echo response */
    printf("[Host RPMsg] Waiting for response...\n");
    n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read");
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("[Host RPMsg] Received (%zd bytes): \"%s\"\n", n, buf);

    close(fd);
    return 0;
}
