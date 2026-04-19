/*
 * Microkit System Description for ping_pong_sel4
 * 
 * This system has two partitions:
 *   1. linux_vm - Linux VM running the micro-ROS agent
 *   2. ping_pong - Native Microkit partition running the ping-pong application
 * 
 * Communication between partitions is done via shared memory and notifications.
 */

#include <microkit.h>
#include <stdint.h>
#include <stdbool.h>

/* Memory regions */
#define LINUX_VM_RAM_BASE       0x40000000
#define LINUX_VM_RAM_SIZE       0x10000000  /* 256 MB */
#define SHARED_MEM_BASE         0x50000000
#define SHARED_MEM_SIZE         0x00100000  /* 1 MB shared memory */
#define DEVICE_TREE_BASE        0x60000000
#define DEVICE_TREE_SIZE        0x00010000  /* 64 KB */

/* Channel IDs for inter-partition communication */
#define CHAN_LINUX_TO_PING      1
#define CHAN_PING_TO_LINUX      2
#define CHAN_LINUX_IRQ          3

/* UART device for debug output */
#define UART_BASE               0x9000000

/* VM configuration */
struct vm_config {
    uintptr_t ram_base;
    size_t ram_size;
    uintptr_t dtb_base;
    size_t dtb_size;
    uintptr_t initrd_base;
    size_t initrd_size;
};

/* Shared memory structure for micro-ROS communication */
struct shared_microros_mem {
    volatile uint32_t magic;           /* Magic number to verify initialization */
    volatile uint32_t version;         /* Protocol version */
    volatile uint32_t tx_head;         /* Transmit buffer head */
    volatile uint32_t tx_tail;         /* Transmit buffer tail */
    volatile uint32_t rx_head;         /* Receive buffer head */
    volatile uint32_t rx_tail;         /* Receive buffer tail */
    volatile uint32_t agent_ready;     /* Flag indicating agent is ready */
    volatile uint32_t client_ready;    /* Flag indicating client is ready */
    uint8_t tx_buffer[512 * 1024];     /* 512KB TX buffer (client -> agent) */
    uint8_t rx_buffer[512 * 1024];     /* 512KB RX buffer (agent -> client) */
};

#define MICROROS_SHM_MAGIC      0x4D49524F  /* "MIRO" */
#define MICROROS_VERSION        1

/* Global system state */
static struct shared_microros_mem *shared_mem = (struct shared_microros_mem *)SHARED_MEM_BASE;

/*
 * Initialization notification handler
 * Called when the system is initialized
 */
void init(void) {
    /* Initialize shared memory structure */
    shared_mem->magic = MICROROS_SHM_MAGIC;
    shared_mem->version = MICROROS_VERSION;
    shared_mem->tx_head = 0;
    shared_mem->tx_tail = 0;
    shared_mem->rx_head = 0;
    shared_mem->rx_tail = 0;
    shared_mem->agent_ready = 0;
    shared_mem->client_ready = 0;
    
    microkit_dbg_puts("ping_pong_sel4: System initialized\n");
}

/*
 * Notification handler for Linux VM
 * Called when Linux sends a notification
 */
void notified(microkit_channel channel) {
    switch (channel) {
        case CHAN_LINUX_TO_PING:
            /* Data available from Linux agent - ping_pong component will handle */
            break;
            
        case CHAN_PING_TO_LINUX:
            /* Ack from ping_pong component - forward to Linux */
            microkit_notify(CHAN_LINUX_TO_PING);
            break;
            
        case CHAN_LINUX_IRQ:
            /* IRQ from Linux VM */
            break;
            
        default:
            microkit_dbg_puts("ping_pong_sel4: Unknown channel\n");
            break;
    }
}

/*
 * Fault handler
 * Called when a partition faults
 */
sel4cp_msginfo protected(sel4cp_msginfo msginfo, sel4cp_msginfo msginfo_unused) {
    microkit_dbg_puts("ping_pong_sel4: Protected procedure called\n");
    return msginfo;
}

/*
 * Memory region definitions for the system
 * These are used by the Microkit tool to generate the final system
 */

/* Memory region: Linux VM RAM */
__attribute__((section(".memory_regions")))
const struct memory_region linux_vm_ram = {
    .name = "linux_vm_ram",
    .base = LINUX_VM_RAM_BASE,
    .size = LINUX_VM_RAM_SIZE,
};

/* Memory region: Shared memory for micro-ROS communication */
__attribute__((section(".memory_regions")))
const struct memory_region shared_microros = {
    .name = "shared_microros",
    .base = SHARED_MEM_BASE,
    .size = SHARED_MEM_SIZE,
};

/* Memory region: Device tree */
__attribute__((section(".memory_regions")))
const struct memory_region device_tree = {
    .name = "device_tree",
    .base = DEVICE_TREE_BASE,
    .size = DEVICE_TREE_SIZE,
};
