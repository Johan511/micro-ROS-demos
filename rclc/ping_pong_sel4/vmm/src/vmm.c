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
uintptr_t shared_mem_vaddr;
uintptr_t net_bufs_vaddr;

static struct shared_microros_mem *shm;

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

static struct net_layout *net;

static void process_tx_pending(void)
{
    net_buff_desc_t buf;
    while (net_dequeue_active(&net_tx, &buf) != -1) {
        uint8_t *pkt = net->tx_data + buf.io_or_offset;
        uint32_t len = buf.len;

        LOG_VMM("TX: packet len=%u ether_type=0x%02x%02x\n",
                len, pkt[12], pkt[13]);

        if (len > 42) {
            char *payload = (char *)pkt + 42;
            uint32_t payload_len = len - 42;
            LOG_VMM("TX: payload %u bytes: %.*s\n", payload_len, (int)payload_len, payload);
        }

        net_enqueue_free(&net_tx, buf);
    }
}

static void send_pkt_to_guest(const uint8_t *payload, uint32_t payload_len,
                              const uint8_t *dst_mac, const uint8_t *src_mac,
                              uint32_t dst_ip, uint32_t src_ip,
                              uint16_t dst_port, uint16_t src_port)
{
    if (net_queue_empty_free(&net_rx)) {
        LOG_VMM_ERR("No free RX buffers, dropping packet\n");
        return;
    }

    net_buff_desc_t buf;
    net_dequeue_free(&net_rx, &buf);
    uint8_t *pkt = net->rx_data + buf.io_or_offset;

    struct {
        uint8_t  dst_mac[6];
        uint8_t  src_mac[6];
        uint16_t eth_type;
        uint8_t  ver_ihl;
        uint8_t  dscp_ecn;
        uint16_t total_len;
        uint16_t ident;
        uint16_t flags_frag;
        uint8_t  ttl;
        uint8_t  proto;
        uint16_t hdr_csum;
        uint32_t src_ip;
        uint32_t dst_ip;
        uint16_t src_port;
        uint16_t dst_port;
        uint16_t udp_len;
        uint16_t udp_csum;
    } __attribute__((packed)) hdr;

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.dst_mac, dst_mac, 6);
    memcpy(hdr.src_mac, src_mac, 6);
    hdr.eth_type = 0x0008; /* IPv4 big-endian */
    hdr.ver_ihl = 0x45;
    hdr.total_len = ((20 + 8 + payload_len) >> 8) | (((20 + 8 + payload_len) & 0xFF) << 8);
    hdr.ident = 0x0000;
    hdr.ttl = 64;
    hdr.proto = 17; /* UDP */
    hdr.src_ip = src_ip;
    hdr.dst_ip = dst_ip;
    hdr.src_port = src_port;
    hdr.dst_port = dst_port;
    hdr.udp_len = ((8 + payload_len) >> 8) | (((8 + payload_len) & 0xFF) << 8);

    uint32_t offset = 0;
    memcpy(pkt + offset, &hdr, sizeof(hdr));
    offset += sizeof(hdr);
    memcpy(pkt + offset, payload, payload_len);
    offset += payload_len;

    buf.len = offset;
    net_enqueue_active(&net_rx, buf);

    virtio_net_handle_rx(&virtio_net);
}

void init(void)
{
    uint8_t mac[6] = VMM_MAC_BYTES;
    uint8_t vm_mac[6] = VM_MAC_BYTES;

    LOG_VMM("starting \"%s\"\n", microkit_name);

    /* Set up shared memory pointers */
    shm = (struct shared_microros_mem *)shared_mem_vaddr;
    net = (struct net_layout *)net_bufs_vaddr;

    /* Clear shared memory */
    memset((void *)shm, 0, sizeof(struct shared_microros_mem));

    /* Clear net_bufs */
    memset((void *)net, 0, sizeof(struct net_layout));

    /* Initialize network queues */
    net_queue_init(&net_tx, &net->tx_free.q, &net->tx_active.q, NET_NUM_BUFFERS);
    net_queue_init(&net_rx, &net->rx_free.q, &net->rx_active.q, NET_NUM_BUFFERS);

    /* Fill TX free queue with buffers */
    for (uint32_t i = 0; i < NET_NUM_BUFFERS; i++) {
        net_buff_desc_t b = { .io_or_offset = i * NET_BUF_SIZE, .len = 0 };
        net_enqueue_free(&net_tx, b);
    }
    /* Fill RX free queue with buffers */
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

    /* Initialize virtual interrupt controller */
    bool success = virq_controller_init();
    if (!success) {
        LOG_VMM_ERR("Failed to initialise emulated interrupt controller\n");
        return;
    }

    /* Initialize virtIO-net device */
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
    LOG_VMM("VMM MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* Start the guest VM */
    guest_start(kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_PINGPONG:
        LOG_VMM("Notification from ping_pong on channel %u\n", ch);
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
