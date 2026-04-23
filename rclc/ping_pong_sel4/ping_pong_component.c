/*
 * Copyright 2024, micro-ROS
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Microkit Native Ping-Pong Component
 *
 * This component implements a ping-pong application using a message format
 * compatible with micro-ROS std_msgs/msg/Header. It communicates with the
 * Linux VM (running the micro-ROS agent) via shared memory.
 *
 * The message format matches std_msgs__msg__Header:
 *   - stamp.sec     : seconds
 *   - stamp.nanosec : nanoseconds
 *   - frame_id      : string (seq_device_id format)
 */

#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>

/*
 * Message format compatible with std_msgs/msg/Header
 * This is a simplified version that matches the memory layout of the
 * micro-ROS Header message type.
 */
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

/* Channel to VMM */
#define CHAN_VMM                1

/* State */
static uint32_t seq_no = 0;
static uint32_t device_id = 0xABCD1234;
static uint32_t pong_count = 0;
static bool initialized = false;

/*
 * Simple string functions (no libc in freestanding)
 */
static void simple_strcpy(char *dest, const char *src)
{
    while ((*dest++ = *src++))
        ;
}

static int simple_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

static void simple_itoa(char *buf, uint32_t val)
{
    int i = 0;
    char tmp[12];
    do {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    } while (val);
    int j = 0;
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void simple_strcat(char *dest, const char *src)
{
    while (*dest)
        dest++;
    while ((*dest++ = *src++))
        ;
}

static uint32_t simple_strlen(const char *s)
{
    uint32_t len = 0;
    while (*s++)
        len++;
    return len;
}

/*
 * Simple pseudo-random number generator (LCG)
 */
static uint32_t prng_state = 12345;
static uint32_t simple_rand(void)
{
    prng_state = prng_state * 1103515245 + 12345;
    return prng_state;
}

/*
 * Output helpers
 */
static void puthex64(uint64_t val)
{
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    buffer[18] = 0;
    for (int i = 17; i > 1; i--) {
        unsigned int v = val & 0xf;
        buffer[i] = v < 10 ? '0' + v : ('a' - 10) + v;
        val >>= 4;
    }
    microkit_dbg_puts(buffer);
}

static void putdec(uint32_t val)
{
    char buf[12];
    simple_itoa(buf, val);
    microkit_dbg_puts(buf);
}

/*
 * Build a frame_id string in the format "seq_device_id"
 * This matches the original Linux ping_pong application format.
 */
static void build_frame_id(char *buf, uint32_t seq, uint32_t dev)
{
    buf[0] = '\0';
    simple_itoa(buf, seq);
    simple_strcat(buf, "_");
    char devbuf[12];
    simple_itoa(devbuf, dev);
    simple_strcat(buf, devbuf);
}

/*
 * Send a ping message via shared memory
 */
static void send_ping(void)
{
    seq_no = simple_rand();

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

/*
 * Send a pong response via shared memory
 */
static void send_pong(const char *frame_id)
{
    simple_strcpy(shared_mem->native_pong.frame_id, frame_id);
    shared_mem->native_pong.stamp.sec = 0;
    shared_mem->native_pong.stamp.nanosec = 0;

    shared_mem->pong_sent++;
    shared_mem->native_to_vm_seq++;
    shared_mem->native_ready = 1;

    microkit_dbg_puts("Pong sent for seq ");
    microkit_dbg_puts(frame_id);
    microkit_dbg_puts("\n");
}

/*
 * Handle an incoming ping from the VM
 */
static void handle_vm_ping(void)
{
    const char *frame_id = shared_mem->vm_ping.frame_id;

    /* Don't pong our own pings */
    if (simple_strcmp(shared_mem->native_ping.frame_id, frame_id) != 0) {
        microkit_dbg_puts("Ping received with seq ");
        microkit_dbg_puts(frame_id);
        microkit_dbg_puts(". Answering.\n");

        shared_mem->ping_received++;
        send_pong(frame_id);
    }
}

/*
 * Handle an incoming pong from the VM
 */
static void handle_vm_pong(void)
{
    const char *frame_id = shared_mem->vm_pong.frame_id;

    if (simple_strcmp(shared_mem->native_ping.frame_id, frame_id) == 0) {
        pong_count++;
        shared_mem->pong_received++;

        microkit_dbg_puts("Pong for seq ");
        microkit_dbg_puts(frame_id);
        microkit_dbg_puts(" (");
        putdec(pong_count);
        microkit_dbg_puts(")\n");
    }
}

/*
 * Initialize the ping-pong component
 */
void init(void)
{
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
    puthex64((uint64_t)shared_mem);
    microkit_dbg_puts("\n");
    microkit_dbg_puts("ping_pong: Message format: std_msgs/msg/Header compatible\n");

    /* Send initial ping */
    send_ping();
}

/*
 * Notification handler
 * Channel 1 is used by the VMM to notify us when the VM has data.
 */
void notified(microkit_channel ch)
{
    if (!initialized) {
        microkit_dbg_puts("ping_pong: Error - not initialized!\n");
        return;
    }

    switch (ch) {
        case CHAN_VMM:
            /* VM has potentially updated shared memory */
            if (shared_mem->vm_ready) {
                /* Check for new ping from VM */
                if (shared_mem->vm_to_native_seq > shared_mem->ping_received) {
                    handle_vm_ping();
                }
                /* Check for new pong from VM */
                if (shared_mem->vm_to_native_seq > shared_mem->pong_received) {
                    handle_vm_pong();
                }
                shared_mem->vm_ready = 0;

                /* Send next ping after processing */
                send_ping();
            }
            break;

        default:
            microkit_dbg_puts("ping_pong: Unknown channel ");
            puthex64(ch);
            microkit_dbg_puts("\n");
            break;
    }
}

/*
 * Protected procedure call handler
 * (not used in this example but required by Microkit API)
 */
microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo)
{
    (void)ch;
    microkit_dbg_puts("ping_pong: Protected call received\n");
    return msginfo;
}

/*
 * Fault handler
 * (required by Microkit API)
 */
seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    (void)child;
    (void)msginfo;
    (void)reply_msginfo;
    microkit_dbg_puts("ping_pong: Fault occurred!\n");
    return seL4_False;
}
