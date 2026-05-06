#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <microkit.h>
#include <libvmm/libvmm.h>
#include <sddf/network/queue.h>
#include <sddf/network/constants.h>
#include "vmm.h"

extern char _guest_kernel_image[], _guest_kernel_image_end[];
extern char _guest_dtb_image[], _guest_dtb_image_end[];
extern char _guest_initrd_image[], _guest_initrd_image_end[];

uintptr_t guest_ram_vaddr;
uintptr_t net_bufs_vaddr;
static volatile shm_buffer_t *pp_comm_buffer;
static struct net_layout *net;

/* virtIO-net device */
static struct virtio_net_device virtio_net;
static net_queue_handle_t net_rx;
static net_queue_handle_t net_tx;

/* Network queue layout in net_bufs.
 * Each net_queue_t has a flexible array member buffers[] which must immediately
 * follow the struct for the queue code to access buffers correctly.
 */
struct net_layout {
    struct {
        net_queue_t q;
        net_buff_desc_t bufs[NET_NUM_BUFFERS];
    } tx_free;
    struct {
        net_queue_t q;
        net_buff_desc_t bufs[NET_NUM_BUFFERS];
    } tx_active;
    struct {
        net_queue_t q;
        net_buff_desc_t bufs[NET_NUM_BUFFERS];
    } rx_free;
    struct {
        net_queue_t q;
        net_buff_desc_t bufs[NET_NUM_BUFFERS];
    } rx_active;
    uint8_t tx_data[NET_NUM_BUFFERS * NET_BUF_SIZE] __attribute__((aligned(64)));
    uint8_t rx_data[NET_NUM_BUFFERS * NET_BUF_SIZE] __attribute__((aligned(64)));
};

static void process_tx_pending(void)
{
    net_buff_desc_t buf;
    while (net_dequeue_active(&net_tx, &buf) != -1) {
        uint8_t *pkt = net->tx_data + buf.io_or_offset;
        uint32_t len = buf.len;

        memcpy((void *)(pp_comm_buffer + 1), pkt, len);
        pp_comm_buffer->size = len;
        LOG_VMM("vmm -> ping_pong, payload='%s'\n", pkt + hdrs_len);

        net_enqueue_free(&net_tx, buf);
        microkit_notify(CHAN_PINGPONG);
    }
}

static void send_pkt_to_guest(char *pkt, uint32_t len)
{
    LOG_VMM("vmm -> vm, payload='%s'\n  ", pkt + hdrs_len);
    if (net_queue_empty_free(&net_rx)) {
        LOG_VMM_ERR("No free RX buffers, dropping packet\n");
        return;
    }

    net_buff_desc_t buf;
    net_dequeue_free(&net_rx, &buf);
    memcpy(net->rx_data + buf.io_or_offset, pkt, len);
    buf.len = len;
    net_enqueue_active(&net_rx, buf);
    virtio_net_handle_rx(&virtio_net);
}

void init(void)
{
    uint8_t vm_mac[6] = VM_MAC_ADDR;
    LOG_VMM("starting \"%s\"\n", microkit_name);

    memset((void *)pp_comm_buffer, 0, 0x1000);
    memset((void *)net, 0, sizeof(struct net_layout));

    net_queue_init(&net_tx, &net->tx_free.q, &net->tx_active.q, NET_NUM_BUFFERS);
    net_cancel_signal_active(&net_tx);
    net_queue_init(&net_rx, &net->rx_free.q, &net->rx_active.q, NET_NUM_BUFFERS);

    for (uint32_t i = 0; i < NET_NUM_BUFFERS; i++) {
        net_buff_desc_t b = { .io_or_offset = i * NET_BUF_SIZE, .len = 0 };
        net_enqueue_free(&net_tx, b);
    }
    for (uint32_t i = 0; i < NET_NUM_BUFFERS; i++) {
        net_buff_desc_t b = { .io_or_offset = i * NET_BUF_SIZE, .len = 0 };
        net_enqueue_free(&net_rx, b);
    }

    /* Set up the Linux guest images */
    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    uintptr_t kernel_pc = linux_setup_images(guest_ram_vaddr,
                                             (uintptr_t)_guest_kernel_image, kernel_size,
                                             (uintptr_t)_guest_dtb_image, GUEST_DTB_VADDR, dtb_size,
                                             (uintptr_t)_guest_initrd_image, GUEST_INIT_RAM_DISK_VADDR, initrd_size);
    if (!kernel_pc) {
        LOG_VMM_ERR("Failed to initialise guest images\n");
        return;
    }
    bool success = virq_controller_init();
    if (!success) {
        LOG_VMM_ERR("Failed to initialise emulated interrupt controller\n");
        return;
    }
    success = virtio_mmio_net_init(&virtio_net,
                                   VIRTIO_NET_MMIO_BASE,
                                   VIRTIO_NET_MMIO_SIZE,
                                   VIRTIO_NET_VIRQ,
                                   &net_rx,
                                   &net_tx,
                                   (uintptr_t)net->rx_data,
                                   (uintptr_t)net->tx_data,
                                   CHAN_PINGPONG,
                                   CHAN_PINGPONG,
                                   vm_mac);
    if (!success) {
        LOG_VMM_ERR("Failed to initialise virtIO-net device\n");
        return;
    }

    LOG_VMM("virtIO-net initialized at MMIO 0x%x, vIRQ %u\n",
            VIRTIO_NET_MMIO_BASE, VIRTIO_NET_VIRQ);
    LOG_VMM("VM MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            vm_mac[0], vm_mac[1], vm_mac[2], vm_mac[3], vm_mac[4], vm_mac[5]);
    guest_start(kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_PINGPONG:
        send_pkt_to_guest((char *)(pp_comm_buffer + 1), pp_comm_buffer->size);
        break;
    default:
        LOG_VMM_ERR("Unexpected notification on channel: 0x%lx\n", ch);
        break;
    }
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    bool success = fault_handle(child, msginfo);
    if (success) {
        /* Process any pending TX data after handling the fault */
        process_tx_pending();
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    LOG_VMM_ERR("Failed to handle fault, stopping guest\n");
    microkit_vcpu_stop(child);
    return seL4_False;
}
