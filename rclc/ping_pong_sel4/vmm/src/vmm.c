#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <microkit.h>
#include <libvmm/libvmm.h>
#include <sddf/network/queue.h>
#include <sddf/network/constants.h>
#include "vmm.h"
#include "../../util/spsc_queue.h"
#include "../../util/networking.h"

extern char _guest_kernel_image[], _guest_kernel_image_end[];
extern char _guest_dtb_image[], _guest_dtb_image_end[];
extern char _guest_initrd_image[], _guest_initrd_image_end[];

#define PKT_SIZE 2048
#define PP2VMM_SIZE 0x100000
#define VMM2PP_SIZE 0x100000

char *pp2vmm, *vmm2pp;
spsc_queue_t *spsc_pp2vmm, *spsc_vmm2pp;
uintptr_t guestRam;
static struct network_ctx_t *networkCtx;
static struct virtio_net_device virtio_net;
static net_queue_handle_t net_rx, net_tx;

typedef struct network_ctx_t {
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
} network_ctx_t;

static bool ready_signal_sent = false;

static bool ready_signal_handler(size_t vcpu_id, size_t offset, size_t fsr, seL4_UserContext *regs, void *data)
{
    if (fault_is_write(fsr) && !ready_signal_sent) {
        ready_signal_sent = true;
        LOG_VMM("Guest signaled readiness, notifying ping_pong PD\n");
        microkit_notify(CHAN_READY);
    }
    return true;
}

static void process_tx_pending(void)
{
    net_buff_desc_t buf;
    while (net_dequeue_active(&net_tx, &buf) != -1) {
        uint8_t *pkt = networkCtx->tx_data + buf.io_or_offset;
        uint32_t len = buf.len;

        char *newBlock = spsc_new_block(spsc_vmm2pp);
        memcpy(newBlock, pkt, len);

        spsc_push(spsc_vmm2pp);
        net_enqueue_free(&net_tx, buf);
        microkit_notify(CHAN_PINGPONG);
    }
}

static void send_pkt_to_guest()
{
    char *pkt = spsc_front_block(spsc_pp2vmm);
    net_buff_desc_t buf;
    net_dequeue_free(&net_rx, &buf);

    memcpy(networkCtx->rx_data + buf.io_or_offset, pkt, PKT_SIZE);
    buf.len = PKT_SIZE;
    
    net_enqueue_active(&net_rx, buf);
    virtio_net_handle_rx(&virtio_net);
    spsc_pop(spsc_pp2vmm);
}

void init(void)
{
    spsc_pp2vmm = (spsc_queue_t *)pp2vmm;
    assert(spsc_init(spsc_pp2vmm, pp2vmm + sizeof(spsc_queue_t), pp2vmm + PP2VMM_SIZE, 11));
    spsc_vmm2pp = (spsc_queue_t *)vmm2pp;
    assert(spsc_init(spsc_vmm2pp, vmm2pp + sizeof(spsc_queue_t), vmm2pp + VMM2PP_SIZE, 11));
    microkit_dbg_puts("spsc_init done\n");

    uint8_t vm_mac[6] = VM_MAC_ADDR;
    LOG_VMM("starting \"%s\"\n", microkit_name);
    memset((void *)networkCtx, 0, sizeof(struct network_ctx_t));

    net_queue_init(&net_tx, &networkCtx->tx_free.q, &networkCtx->tx_active.q, NET_NUM_BUFFERS);
    net_cancel_signal_active(&net_tx);
    net_queue_init(&net_rx, &networkCtx->rx_free.q, &networkCtx->rx_active.q, NET_NUM_BUFFERS);

    for (uint32_t i = 0; i < NET_NUM_BUFFERS; i++) {
        net_buff_desc_t b = { .io_or_offset = i * NET_BUF_SIZE, .len = 0 };
        net_enqueue_free(&net_tx, b);
    }
    for (uint32_t i = 0; i < NET_NUM_BUFFERS; i++) {
        net_buff_desc_t b = { .io_or_offset = i * NET_BUF_SIZE, .len = 0 };
        net_enqueue_free(&net_rx, b);
    }

    size_t kernel_size = _guest_kernel_image_end - _guest_kernel_image;
    size_t dtb_size = _guest_dtb_image_end - _guest_dtb_image;
    size_t initrd_size = _guest_initrd_image_end - _guest_initrd_image;
    uintptr_t kernel_pc = linux_setup_images(guestRam,
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
                                   (uintptr_t)networkCtx->rx_data,
                                   (uintptr_t)networkCtx->tx_data,
                                   CHAN_PINGPONG,
                                   CHAN_PINGPONG,
                                   vm_mac);
    if (!success) {
        LOG_VMM_ERR("Failed to initialise virtIO-networkCtx device\n");
        return;
    }

    success = fault_register_vm_exception_handler(READY_SIGNAL_MMIO_BASE,
                                                  READY_SIGNAL_MMIO_SIZE,
                                                  ready_signal_handler,
                                                  NULL);
    if (!success) {
        LOG_VMM_ERR("Failed to register readiness signal handler\n");
        return;
    }

    guest_start(kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch)
{
    switch (ch) {
    case CHAN_PINGPONG:
        while (!spsc_empty(spsc_pp2vmm)) {
            send_pkt_to_guest();
        }
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
        process_tx_pending();
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    LOG_VMM_ERR("Failed to handle fault, stopping guest\n");
    microkit_vcpu_stop(child);
    return seL4_False;
}
