#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <microkit.h>

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
};

uintptr_t shared_mem_vaddr;
static struct shared_microros_mem *shm;
#define CHAN_VMM                1

static uint32_t seq_no = 0;
static uint32_t device_id = 0xABCD1234;
static uint32_t pong_received_count = 0;
static bool initialized = false;

static void build_frame_id(char *buf, uint32_t seq, uint32_t dev)
{
    snprintf(buf, 100, "%u_%u", seq, dev);
}

static void send_ping(void)
{
    seq_no++;

    microkit_dbg_puts("PING: seq=");
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", seq_no);
    microkit_dbg_puts(buf);
    microkit_dbg_puts("\n");

    shm->native_to_vm_seq++;
    shm->native_ready = 1;

    microkit_notify(CHAN_VMM);
}

static void send_pong(void)
{
    microkit_dbg_puts("PONG: responding to VM ping\n");

    shm->native_to_vm_seq++;
    shm->native_ready = 1;

    microkit_notify(CHAN_VMM);
}

static void handle_vm_ping(void)
{
    microkit_dbg_puts("RX: ping from VM received, sending pong\n");
    send_pong();
}

static void handle_vm_pong(void)
{
    pong_received_count++;

    char buf[32];
    microkit_dbg_puts("RX: pong from VM (");
    snprintf(buf, sizeof(buf), "%u", pong_received_count);
    microkit_dbg_puts(buf);
    microkit_dbg_puts(" total)\n");
}

void init(void)
{
    microkit_dbg_puts("ping_pong: Starting micro-ROS ping-pong over virtIO-net...\n");

    shm = (struct shared_microros_mem *)shared_mem_vaddr;
    memset((void *)shm, 0, sizeof(struct shared_microros_mem));

    initialized = true;

    microkit_dbg_puts("ping_pong: Initialized, shared memory at 0x");
    char buf[32];
    snprintf(buf, sizeof(buf), "%016lx", (uint64_t)shm);
    microkit_dbg_puts(buf);
    microkit_dbg_puts("\n");

    send_ping();
}

void notified(microkit_channel ch)
{
    if (!initialized) {
        microkit_dbg_puts("ping_pong: Error - not initialized!\n");
        return;
    }

    switch (ch) {
    case CHAN_VMM:
        if (shm->vm_ready) {
            if (shm->vm_to_native_seq > shm->native_to_vm_seq) {
                handle_vm_ping();
            } else {
                handle_vm_pong();
            }
            shm->vm_ready = 0;
            send_ping();
        }
        break;

    default: {
        char buf[32];
        microkit_dbg_puts("ping_pong: Unknown channel ");
        snprintf(buf, sizeof(buf), "0x%lx", ch);
        microkit_dbg_puts(buf);
        microkit_dbg_puts("\n");
        break;
    }
    }
}
