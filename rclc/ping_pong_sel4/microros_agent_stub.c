/*******************************************************************************
 * micro-ROS Agent Stub for Linux VM on seL4
 *
 * This minimal agent runs inside the Linux VM and communicates with the
 * native ping_pong PD via shared memory at physical address 0x5000000.
 *
 * The shared memory uses a std_msgs/msg/Header-compatible format.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>

#define SHARED_MEM_PADDR 0x50000000
#define SHARED_MEM_SIZE  0x1000

/* Match the native PD's message format */
struct microros_time {
    int32_t sec;
    uint32_t nanosec;
};

struct microros_header {
    struct microros_time stamp;
    char frame_id[100];
};

struct shared_microros_mem {
    volatile uint32_t native_to_vm_seq;
    volatile uint32_t vm_to_native_seq;
    volatile uint32_t native_ready;
    volatile uint32_t vm_ready;
    struct microros_header native_ping;
    struct microros_header vm_ping;
    struct microros_header native_pong;
    struct microros_header vm_pong;
    volatile uint32_t ping_sent;
    volatile uint32_t pong_received;
    volatile uint32_t ping_received;
    volatile uint32_t pong_sent;
};

static uint32_t device_id = 0xDEADBEEF;
static uint32_t seq_no = 0;
static uint32_t pong_count = 0;

static void build_frame_id(char *buf, uint32_t seq, uint32_t dev)
{
    snprintf(buf, 100, "%u_%u", seq, dev);
}

int main(void)
{
    printf("micro-ROS agent stub: starting...\n");

    /* Open /dev/mem for physical memory access */
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("open /dev/mem");
        printf("micro-ROS agent stub: trying to create /dev/mem node...\n");
        /* Create the device node if it doesn't exist */
        system("mknod /dev/mem c 1 1");
        fd = open("/dev/mem", O_RDWR | O_SYNC);
        if (fd < 0) {
            perror("open /dev/mem (retry)");
            return 1;
        }
    }

    /* Map the shared memory region */
    void *mem = mmap(NULL, SHARED_MEM_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, SHARED_MEM_PADDR);
    if (mem == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    struct shared_microros_mem *shm = (struct shared_microros_mem *)mem;

    printf("micro-ROS agent stub: shared memory mapped at %p (phys 0x%x)\n",
           mem, SHARED_MEM_PADDR);
    printf("micro-ROS agent stub: waiting for native PD to be ready...\n");

    /* Wait for native PD to signal readiness */
    int wait_count = 0;
    while (!shm->native_ready) {
        usleep(100000); /* 100ms */
        wait_count++;
        if (wait_count > 100) {
            printf("micro-ROS agent stub: timeout waiting for native PD\n");
            printf("micro-ROS agent stub: starting anyway...\n");
            break;
        }
    }

    printf("micro-ROS agent stub: native PD ready (seq=%u), starting ping-pong\n",
           shm->native_to_vm_seq);

    /* Seed random number generator */
    srand((unsigned)time(NULL));

    /* Main ping-pong loop */
    while (1) {
        /* Check if native PD sent a new ping */
        if (shm->native_ready && shm->native_to_vm_seq > shm->pong_received) {
            printf("Agent: Received ping seq %s\n", shm->native_ping.frame_id);

            /* Send pong response */
            seq_no = (uint32_t)rand();
            build_frame_id((char *)shm->vm_pong.frame_id, seq_no, device_id);
            shm->vm_pong.stamp.sec = 0;
            shm->vm_pong.stamp.nanosec = 0;

            shm->pong_sent++;
            shm->vm_to_native_seq++;
            shm->vm_ready = 1;

            printf("Agent: Sent pong seq %s (pong #%u)\n",
                   shm->vm_pong.frame_id, shm->pong_sent);

            /* Clear native_ready to acknowledge we processed it */
            /* Note: in a real implementation, use proper atomic operations */
        }

        /* Periodically send our own ping as well */
        if ((shm->ping_sent == 0) || (shm->vm_to_native_seq <= shm->native_to_vm_seq + 2)) {
            seq_no = (uint32_t)rand();
            build_frame_id((char *)shm->vm_ping.frame_id, seq_no, device_id);
            shm->vm_ping.stamp.sec = 0;
            shm->vm_ping.stamp.nanosec = 0;

            shm->ping_sent++;
            shm->vm_to_native_seq++;
            shm->vm_ready = 1;

            printf("Agent: Sent ping seq %s (ping #%u)\n",
                   shm->vm_ping.frame_id, shm->ping_sent);
        }

        usleep(500000); /* 500ms */
    }

    munmap(mem, SHARED_MEM_SIZE);
    close(fd);
    return 0;
}
