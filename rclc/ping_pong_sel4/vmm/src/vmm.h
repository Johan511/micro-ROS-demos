#pragma once

#include "../../common.h"

#if defined(BOARD_qemu_virt_aarch64)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#define GUEST_RAM_SIZE 0x10000000
#elif defined(BOARD_rpi4b_hyp)
#define GUEST_DTB_VADDR 0x2e000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define GUEST_RAM_SIZE 0x10000000
#elif defined(BOARD_odroidc2_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define GUEST_RAM_SIZE 0x10000000
#elif defined(BOARD_odroidc4_hyp)
#define GUEST_DTB_VADDR 0x2f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x2d700000
#define GUEST_RAM_SIZE 0x10000000
#elif defined(BOARD_imx8mm_evk_hyp)
#define GUEST_DTB_VADDR 0x4f000000
#define GUEST_INIT_RAM_DISK_VADDR 0x4d700000
#define GUEST_RAM_SIZE 0x10000000
#else
#error Need to define VM image address and DTB address
#endif

/* virtIO-net MMIO device: guest-physical base inside platform bus at 0xC100000 */
#define VIRTIO_NET_MMIO_BASE    0xC100000
#define VIRTIO_NET_MMIO_SIZE    0x200
#define VIRTIO_NET_VIRQ         80

/* Network queue configuration */
#define NET_NUM_BUFFERS         16
#define NET_BUF_SIZE            2048

#define VM_MAC_ADDR            { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 }
#define CHAN_PINGPONG           1
