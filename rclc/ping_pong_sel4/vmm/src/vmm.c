#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <microkit.h>
#include <libvmm/libvmm.h>
#include "vmm.h"

extern char _guest_kernel_image[], _guest_kernel_image_end[];
extern char _guest_dtb_image[], _guest_dtb_image_end[];
extern char _guest_initrd_image[], _guest_initrd_image_end[];

uintptr_t guest_ram_vaddr;
uintptr_t shared_mem_vaddr;

void init(void)
{
    LOG_VMM("starting \"%s\"\n", microkit_name);

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

    guest_start(kernel_pc, GUEST_DTB_VADDR, GUEST_INIT_RAM_DISK_VADDR);
}

void notified(microkit_channel ch)
{
    printf("Unexpected notification on channel: 0x%lx\n", ch);
}

seL4_Bool fault(microkit_child child, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    bool success = fault_handle(child, msginfo);
    if (success) {
        *reply_msginfo = microkit_msginfo_new(0, 0);
        return seL4_True;
    }

    LOG_VMM_ERR("Failed to handle fault, stopping guest\n");
    microkit_vcpu_stop(child);
    return seL4_False;
}
