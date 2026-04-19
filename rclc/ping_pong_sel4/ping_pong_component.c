/*
 * Copyright 2024, micro-ROS
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

/*
 * Microkit Native Ping-Pong Component
 * 
 * This component demonstrates a ping-pong application running in a native
 * Microkit partition, communicating with another partition via shared memory.
 * 
 * This is a simplified version that demonstrates the Microkit structure.
 * The full micro-ROS integration would require building micro-ROS libraries
 * for the seL4/Microkit environment.
 */

#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>

/* Simple string copy implementation */
static void simple_strcpy(char *dest, const char *src)
{
    while ((*dest++ = *src++))
        ;
}

/* Shared memory with other partition - address set via setvar_vaddr in system file */
uintptr_t shared_mem_vaddr;

/* Shared memory structure */
struct shared_microros_mem {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t counter;
    volatile uint32_t ping_count;
    volatile uint32_t pong_count;
    char message[128];
};

#define MICROROS_SHM_MAGIC      0x4D49524F  /* "MIRO" */
#define MICROROS_VERSION        1

static struct shared_microros_mem *shared_mem;

/* Channel definitions */
#define CHAN_PING               0
#define CHAN_PONG               1

/* State */
static uint32_t local_counter = 0;
static bool initialized = false;

/*
 * Output a number as hex for debugging
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

/*
 * Initialize the ping-pong component
 */
void init(void)
{
    microkit_dbg_puts("ping_pong: Initializing...\n");
    
    /* Set up shared memory pointer from the address provided by the system file */
    shared_mem = (struct shared_microros_mem *)shared_mem_vaddr;
    
    /* Initialize shared memory */
    shared_mem->magic = MICROROS_SHM_MAGIC;
    shared_mem->version = MICROROS_VERSION;
    shared_mem->counter = 0;
    shared_mem->ping_count = 0;
    shared_mem->pong_count = 0;
    simple_strcpy(shared_mem->message, "Hello from ping_pong!");
    
    initialized = true;
    
    microkit_dbg_puts("ping_pong: Initialized, shared memory at ");
    puthex64((uint64_t)shared_mem);
    microkit_dbg_puts("\n");
    microkit_dbg_puts("ping_pong: Message: ");
    microkit_dbg_puts(shared_mem->message);
    microkit_dbg_puts("\n");
}

/*
 * Notification handler
 */
void notified(microkit_channel ch)
{
    if (!initialized) {
        microkit_dbg_puts("ping_pong: Error - not initialized!\n");
        return;
    }
    
    switch (ch) {
        case CHAN_PING:
            /* Received ping - update counter and respond */
            local_counter++;
            shared_mem->counter = local_counter;
            shared_mem->ping_count++;
            
            microkit_dbg_puts("ping_pong: Received ping #");
            puthex64(local_counter);
            microkit_dbg_puts(" (total pings: ");
            puthex64(shared_mem->ping_count);
            microkit_dbg_puts(")\n");
            
            /* Send pong response */
            microkit_notify(CHAN_PONG);
            break;
            
        case CHAN_PONG:
            /* Received pong acknowledgment */
            shared_mem->pong_count++;
            microkit_dbg_puts("ping_pong: Received pong acknowledgment (total pongs: ");
            puthex64(shared_mem->pong_count);
            microkit_dbg_puts(")\n");
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
