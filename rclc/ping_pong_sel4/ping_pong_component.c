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
    /* Fixed-size frame_id for freestanding environment */
    char frame_id[100];
};

/*
 * Shared memory layout for cross-VM communication
 * The shared_mem region is mapped into both the VM and this PD.
 */
struct shared_microros_mem {
    /* Synchronization flags */
    volatile uint32_t native_to_vm_seq;
    volatile uint32_t vm_to_native_seq;
    volatile uint32_t native_ready;
    volatile uint32_t vm_ready;

    /* Messages */
    struct microros_header native_ping;    /* Ping sent by native PD */
    struct microros_header vm_ping;        /* Ping received from VM */
    struct microros_header native_pong;    /* Pong sent by native PD */
    struct microros_header vm_pong;        /* Pong received from VM */

    /* Counters for diagnostics */
    volatile uint32_t ping_sent;
    volatile uint32_t pong_received;
    volatile uint32_t ping_received;
    volatile uint32_t pong_sent;
};

/* Shared memory address set via setvar_vaddr in system file */
uintptr_t shared_mem_vaddr;
static struct shared_microros_mem *shared_mem;
#define CHAN_VMM                1

static uint32_t seq_no = 0;
static uint32_t device_id = 0xABCD1234;
static uint32_t pong_count = 0;
static bool initialized = false;

static void build_frame_id(char *buf, uint32_t seq, uint32_t dev)
{
    snprintf(buf, 100, "%u_%u", seq, dev);
}

static void send_ping(void)
{
    seq_no = 0;

    build_frame_id(shared_mem->native_ping.frame_id, seq_no, device_id);
    shared_mem->native_ping.stamp.sec = 0;
    shared_mem->native_ping.stamp.nanosec = 0;

    shared_mem->ping_sent++;
    shared_mem->native_to_vm_seq++;
    shared_mem->native_ready = 1;

    microkit_dbg_puts("Ping send seq ");
    microkit_dbg_puts(shared_mem->native_ping.frame_id);
    microkit_dbg_puts("\n");

    pong_count = 0;
}

static void send_pong(const char *frame_id)
{
    strcpy(shared_mem->native_pong.frame_id, frame_id);
    shared_mem->native_pong.stamp.sec = 0;
    shared_mem->native_pong.stamp.nanosec = 0;

    shared_mem->pong_sent++;
    shared_mem->native_to_vm_seq++;
    shared_mem->native_ready = 1;

    microkit_dbg_puts("Pong sent for seq ");
    microkit_dbg_puts(frame_id);
    microkit_dbg_puts("\n");
}

static void handle_vm_ping(void)
{
    const char *frame_id = shared_mem->vm_ping.frame_id;

    /* Don't pong our own pings */
    if (strcmp(shared_mem->native_ping.frame_id, frame_id) != 0) {
        microkit_dbg_puts("Ping received with seq ");
        microkit_dbg_puts(frame_id);
        microkit_dbg_puts(". Answering.\n");

        shared_mem->ping_received++;
        send_pong(frame_id);
    }
}

static void handle_vm_pong(void)
{
    const char *frame_id = shared_mem->vm_pong.frame_id;

    if (strcmp(shared_mem->native_ping.frame_id, frame_id) == 0) {
        pong_count++;
        shared_mem->pong_received++;

        char buf[32];
        microkit_dbg_puts("Pong for seq ");
        microkit_dbg_puts(frame_id);
        microkit_dbg_puts(" (");
        snprintf(buf, sizeof(buf), "%u", pong_count);
        microkit_dbg_puts(buf);
        microkit_dbg_puts(")\n");
    }
}

void init(void)
{
    char buf[32];

    microkit_dbg_puts("ping_pong: Initializing micro-ROS compatible ping-pong...\n");

    /* Set up shared memory pointer from the address provided by the system file */
    shared_mem = (struct shared_microros_mem *)shared_mem_vaddr;

    /* Clear shared memory */
    volatile uint8_t *p = (volatile uint8_t *)shared_mem;
    for (uint32_t i = 0; i < sizeof(struct shared_microros_mem); i++) {
        p[i] = 0;
    }

    initialized = true;

    microkit_dbg_puts("ping_pong: Initialized, shared memory at ");
    snprintf(buf, sizeof(buf), "0x%016lx", (uint64_t)shared_mem);
    microkit_dbg_puts(buf);
    microkit_dbg_puts("\n");
    microkit_dbg_puts("ping_pong: Message format: std_msgs/msg/Header compatible\n");

    send_ping();
}

void notified(microkit_channel ch)
{
    char buf[32];

    if (!initialized) {
        microkit_dbg_puts("ping_pong: Error - not initialized!\n");
        return;
    }

    switch (ch) {
        case CHAN_VMM:
            if (shared_mem->vm_ready) {
                if (shared_mem->vm_to_native_seq > shared_mem->ping_received)
                    handle_vm_ping();
                if (shared_mem->vm_to_native_seq > shared_mem->pong_received)
                    handle_vm_pong();
                shared_mem->vm_ready = 0;
                send_ping();
            }
            break;

        default:
            microkit_dbg_puts("ping_pong: Unknown channel ");
            snprintf(buf, sizeof(buf), "0x%lx", ch);
            microkit_dbg_puts(buf);
            microkit_dbg_puts("\n");
            break;
    }
}
