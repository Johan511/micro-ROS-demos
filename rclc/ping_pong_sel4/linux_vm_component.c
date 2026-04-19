/*
 * Linux VM Component for ping_pong_sel4
 * 
 * This component runs a Linux VM that hosts the micro-ROS agent.
 * The VM communicates with the native Microkit ping-pong component
 * via shared memory.
 */

#include <microkit.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

/* Memory regions */
#define LINUX_VM_RAM_BASE       0x40000000
#define LINUX_VM_RAM_SIZE       0x10000000
#define SHARED_MEM_BASE         0x50000000
#define DEVICE_TREE_BASE        0x60000000
#define UART_BASE               0x09000000

/* Shared memory structure */
struct shared_microros_mem {
    volatile uint32_t magic;
    volatile uint32_t version;
    volatile uint32_t tx_head;
    volatile uint32_t tx_tail;
    volatile uint32_t rx_head;
    volatile uint32_t rx_tail;
    volatile uint32_t agent_ready;
    volatile uint32_t client_ready;
    uint8_t tx_buffer[512 * 1024];
    uint8_t rx_buffer[512 * 1024];
};

#define MICROROS_SHM_MAGIC      0x4D49524F
#define MICROROS_VERSION        1

static struct shared_microros_mem *shared_mem = (struct shared_microros_mem *)SHARED_MEM_BASE;

/* VM configuration */
#define VM_ID           0
#define VM_VCPU_ID      0

/*
 * Initialize the Linux VM
 */
void init(void) {
    microkit_dbg_puts("linux_vm: Initializing Linux VM for micro-ROS agent...\n");
    
    /* Initialize shared memory */
    shared_mem->magic = MICROROS_SHM_MAGIC;
    shared_mem->version = MICROROS_VERSION;
    shared_mem->tx_head = 0;
    shared_mem->tx_tail = 0;
    shared_mem->rx_head = 0;
    shared_mem->rx_tail = 0;
    shared_mem->agent_ready = 0;
    shared_mem->client_ready = 0;
    
    /* Load Linux kernel and initrd into VM memory */
    /* This would typically be done by the bootloader or loader component */
    
    /* Set up virtual UART for console output */
    microkit_vm_console_init(VM_ID);
    
    microkit_dbg_puts("linux_vm: VM initialized, starting...\n");
    
    /* Start the VM - this will boot Linux */
    microkit_vm_init(VM_ID, VM_VCPU_ID, LINUX_VM_RAM_BASE, DEVICE_TREE_BASE);
    
    /* Signal that agent (VM) is ready */
    shared_mem->agent_ready = 1;
}

/*
 * Handle VM faults and syscalls
 */
sel4cp_msginfo protected(sel4cp_msginfo msginfo, sel4cp_msginfo msginfo_unused) {
    uint64_t label = seL4_MessageInfo_get_label(msginfo);
    
    switch (label) {
        case seL4_VMFault: {
            /* Handle VM fault */
            microkit_dbg_puts("linux_vm: VM fault occurred\n");
            break;
        }
        
        case seL4_VCPUFault: {
            /* Handle VCPU fault */
            microkit_dbg_puts("linux_vm: VCPU fault occurred\n");
            break;
        }
        
        default: {
            /* Handle other faults */
            break;
        }
    }
    
    return msginfo;
}

/*
 * Notification handler
 */
void notified(microkit_channel channel) {
    switch (channel) {
        case 1:  /* CHAN_PING_TO_LINUX - data from ping_pong component */
            /* Data available from ping_pong component */
            /* Forward to Linux via virtio or other mechanism */
            break;
            
        case 2:  /* IRQ notification */
            /* Handle IRQ from device */
            break;
            
        default:
            break;
    }
}

/*
 * VM memory fault handler
 * Called when the VM accesses unmapped memory or triggers a fault
 */
void vm_fault(microkit_channel channel, microkit_msginfo msginfo) {
    (void)channel;
    
    uint64_t fault_ip = microkit_msginfo_get_label(msginfo);
    uint64_t fault_addr = microkit_mr_get(0);
    uint64_t fsr = microkit_mr_get(1);
    
    (void)fault_ip;
    (void)fault_addr;
    (void)fsr;
    
    /* Handle the fault - typically by mapping memory or injecting exception */
    microkit_dbg_puts("linux_vm: Memory fault handled\n");
    
    /* Restart the VM */
    microkit_vm_restart(VM_ID, fault_ip);
}
