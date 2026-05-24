#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>

#define READY_SIGNAL_MMIO_BASE 0xC200000
#define READY_SIGNAL_MMIO_SIZE 0x1000
#define UDP_PORT 12345

static void spin_sleep()
{
    volatile int counter = 0;
    while (counter++ < 500000000);
}

static bool is_udp_port_open(int port)
{
    FILE *f = fopen("/proc/net/udp", "r");
    if (!f) {
        perror("fopen /proc/net/udp");
        return false;
    }

    char port_str[8];
    snprintf(port_str, sizeof(port_str), ":%04X", port);

    char line[256];
    fgets(line, sizeof(line), f);

    while (fgets(line, sizeof(line), f)) {
        char local_addr[32];
        int sl;
        if (sscanf(line, "%d: %31s", &sl, local_addr) == 2) {
            if (strstr(local_addr, port_str)) {
                fclose(f);
                return true;
            }
        }
    }

    fclose(f);
    return false;
}

static void signal_ready(void)
{
    int fd = open("/dev/mem", O_RDWR);
    if (fd < 0) {
        perror("open /dev/mem");
        exit(1);
    }

    volatile uint32_t *reg = mmap(NULL, READY_SIGNAL_MMIO_SIZE,
                                  PROT_READ | PROT_WRITE, MAP_SHARED,
                                  fd, READY_SIGNAL_MMIO_BASE);
    if (reg == MAP_FAILED) {
        perror("mmap");
        close(fd);
        exit(1);
    }

    *reg = 0x52454144;

    munmap((void *)reg, READY_SIGNAL_MMIO_SIZE);
    close(fd);
}

int main(int argc, char **argv)
{
    int port = UDP_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
    }

    printf("is_port_open: waiting for UDP port %d\n", port);

    while (!is_udp_port_open(port)) {
        spin_sleep();
    }

    printf("is_port_open: UDP port %d is open, signaling VMM\n", port);
    signal_ready();

    return 0;
}
