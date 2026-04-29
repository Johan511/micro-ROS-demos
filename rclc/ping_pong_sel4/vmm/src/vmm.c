#include <stddef.h>
#include <microkit.h>
#include "util/util.h"
#include "vgic/vgic.h"
#include "smc.h"
#include "fault.h"
#include "hsr.h"
#include "vmm.h"
#include "arch/aarch64/linux.h"

extern char _guest_kernel_image[], _guest_kernel_image_end[];
extern char _guest_dtb_image[], _guest_dtb_image_end[];
extern char _guest_initrd_image[], _guest_initrd_image_end[];

uintptr_t guest_ram_vaddr, shared_mem_vaddr;

#define MAX_IRQ_CH 32
int passthrough_irq_map[MAX_IRQ_CH];

static void vppi_event_ack(uint64_t vcpu, int irq, void * cookie) { microkit_vcpu_arm_ack_vppi(GUEST_ID, irq); }
static void sgi_ack(uint64_t vcpu, int irq, void * cookie) {}

static void passthrough_device_ack(uint64_t vcpu, int irq, void *cookie) {
    microkit_channel irq_ch = (microkit_channel)(int64_t)cookie;
    microkit_irq_ack(irq_ch);
}

static void register_passthrough_irq(int irq, microkit_channel irq_ch) {
    LOG_VMM("Register passthrough IRQ %d (channel: 0x%lx)\n", irq, irq_ch);
    assert(irq_ch < MAX_IRQ_CH);
    passthrough_irq_map[irq_ch] = irq;

    int err = vgic_register_irq(GUEST_VCPU_ID, irq, &passthrough_device_ack, (void *)(int64_t)irq_ch);
    if (!err) {
        LOG_VMM_ERR("Failed to register IRQ %d\n", irq);
        return;
    }
}

#define SGI_RESCHEDULE_IRQ  0
#define SGI_FUNC_CALL       1
#define PPI_VTIMER_IRQ      27

bool guest_init_images(void) {
    struct linux_image_header *image_header = (struct linux_image_header *) &_guest_kernel_image;
    if (image_header->magic != LINUX_IMAGE_MAGIC) {
        LOG_VMM_ERR("Linux kernel image magic check failed\n");
        return false;
    }

    uint64_t kernel_image_size = _guest_kernel_image_end - _guest_kernel_image;
    uint64_t kernel_image_vaddr = guest_ram_vaddr + image_header->text_offset;

    // image must be aligned at 2MB, we place the image in text offset of guest_ram_vaddr
    // hence, we enforce that it is aligned by 2MB
    assert((guest_ram_vaddr & ((1 << 20) - 1)) == 0);
    memcpy((char *)kernel_image_vaddr, _guest_kernel_image, kernel_image_size);

    uint64_t dtb_image_size = _guest_dtb_image_end - _guest_dtb_image;
    memcpy((char *)GUEST_DTB_VADDR, _guest_dtb_image, dtb_image_size);

    uint64_t initrd_image_size = _guest_initrd_image_end - _guest_initrd_image;
    LOG_VMM("Copying guest initial RAM disk to 0x%x (0x%x bytes)\n", GUEST_INIT_RAM_DISK_VADDR, initrd_image_size);
    memcpy((char *)GUEST_INIT_RAM_DISK_VADDR, _guest_initrd_image, initrd_image_size);

    return true;
}

bool guest_restart(void) { LOG_VMM_ERR("guest_restart is not implemented"); return false; }

void guest_start(void) {
    vgic_init();

    if (!vgic_register_irq(GUEST_VCPU_ID, PPI_VTIMER_IRQ, &vppi_event_ack, NULL)) {
        LOG_VMM_ERR("Failed to register vCPU virtual timer IRQ: 0x%lx\n", PPI_VTIMER_IRQ);
        return;
    }
    if (!vgic_register_irq(GUEST_VCPU_ID, SGI_RESCHEDULE_IRQ, &sgi_ack, NULL)) {
        LOG_VMM_ERR("Failed to register vCPU SGI 0 IRQ");
        return;
    }
    if (!vgic_register_irq(GUEST_VCPU_ID, SGI_FUNC_CALL, &sgi_ack, NULL)) {
        LOG_VMM_ERR("Failed to register vCPU SGI 1 IRQ");
        return;
    }

    seL4_UserContext regs = {0};
    regs.x0 = GUEST_DTB_VADDR;
    regs.spsr = 5; // PMODE_EL1h
    // Read the entry point and set it to the program counter
    struct linux_image_header *image_header = (struct linux_image_header *) &_guest_kernel_image;
    uint64_t kernel_image_vaddr = guest_ram_vaddr + image_header->text_offset;
    regs.pc = kernel_image_vaddr;
    // Set all the TCB registers
    if(seL4_TCB_WriteRegisters(
        BASE_VM_TCB_CAP + GUEST_ID,
        false, // We'll explcitly start the guest below rather than in this call
        0, // No flags
        SEL4_USER_CONTEXT_SIZE, // Writing to x0, pc, and spsr // @ivanv: for some reason having the number of registers here does not work... (in this case 2)
        &regs))
    {
        assert(0);
    }
    // Set the PC to the kernel image's entry point and start the thread.
    LOG_VMM("starting guest at 0x%lx, DTB at 0x%lx, initial RAM disk at 0x%lx\n",
        regs.pc, regs.x0, GUEST_INIT_RAM_DISK_VADDR);
    microkit_vcpu_restart(GUEST_ID, regs.pc);
}

void guest_stop(void) {
    LOG_VMM("Stopping guest\n");
    microkit_vcpu_stop(GUEST_ID);
    LOG_VMM("Stopped guest\n");
}

void init(void)
{
    LOG_VMM("starting \"%s\"\n", microkit_name);
    if(!guest_init_images()) {
        LOG_VMM_ERR("Failed to initialise guest images\n");
        assert(0);
    }
    guest_start();
}

void notified(microkit_channel ch)
{
    printf("Unexpected notification on channel: 0x%lx\n", ch);
}

static bool handle_vcpu_fault(microkit_msginfo msginfo, uint64_t vcpu_id)
{
    uint32_t hsr = microkit_mr_get(seL4_VCPUFault_HSR);
    uint64_t hsr_ec_class = HSR_EXCEPTION_CLASS(hsr);
    switch (hsr_ec_class) {
        case HSR_SMC_64_EXCEPTION:
            return handle_smc(vcpu_id, hsr);
        case HSR_WFx_EXCEPTION:
            // If we get a WFI exception, we just do nothing in the VMM.
            return true;
        default:
            LOG_VMM_ERR("unknown SMC exception, EC class: 0x%lx, HSR: 0x%lx\n", hsr_ec_class, hsr);
            return false;
    }
}

static bool handle_vm_fault()
{
    uint64_t addr = microkit_mr_get(seL4_VMFault_Addr);
    uint64_t fsr = microkit_mr_get(seL4_VMFault_FSR);

    seL4_UserContext regs;
    int err = seL4_TCB_ReadRegisters(BASE_VM_TCB_CAP + GUEST_ID, false, 0, SEL4_USER_CONTEXT_SIZE, &regs);
    assert(err == seL4_NoError);

    switch (addr) {
        case GIC_DIST_PADDR...GIC_DIST_PADDR + GIC_DIST_SIZE:
            return handle_vgic_dist_fault(GUEST_VCPU_ID, addr, fsr, &regs);
#if defined(GIC_V3)
        /* Need to handle redistributor faults for GICv3 platforms. */
        case GIC_REDIST_PADDR...GIC_REDIST_PADDR + GIC_REDIST_SIZE:
            return handle_vgic_redist_fault(GUEST_VCPU_ID, addr, fsr, &regs);
#endif
        default: {
            uint64_t ip = microkit_mr_get(seL4_VMFault_IP);
            uint64_t is_prefetch = seL4_GetMR(seL4_VMFault_PrefetchFault);
            uint64_t is_write = (fsr & (1 << 6)) != 0;
            LOG_VMM_ERR("unexpected memory fault on address: 0x%lx, FSR: 0x%lx, IP: 0x%lx, is_prefetch: %s, is_write: %s\n", addr, fsr, ip, is_prefetch ? "true" : "false", is_write ? "true" : "false");
            print_tcb_regs(&regs);
            print_vcpu_regs(GUEST_ID);
            return false;
        }
    }
}

static bool handle_vppi_event()
{
    uint64_t ppi_irq = microkit_mr_get(seL4_VPPIEvent_IRQ);
    if (!vgic_inject_irq(GUEST_VCPU_ID, ppi_irq)) {
        LOG_VMM_ERR("VPPI IRQ %lu dropped on vCPU %d\n", ppi_irq, GUEST_VCPU_ID);
        // ack the vppi event as the VM won't do it
        microkit_vcpu_arm_ack_vppi(GUEST_ID, ppi_irq);
    }

    return true;
}


seL4_Bool fault(microkit_child id, microkit_msginfo msginfo, microkit_msginfo *reply_msginfo)
{
    const char *faultLabel = "unknown_fault";
    uint64_t label = microkit_msginfo_get_label(msginfo);
    bool success = false;
    switch (label) {
        case seL4_Fault_VMFault:
            success = handle_vm_fault();
            break;
        case seL4_Fault_UnknownSyscall:
            faultLabel = "unknown_syscall";
            break;
        case seL4_Fault_UserException:
            faultLabel = "user_exception";
            break;
        case seL4_Fault_VGICMaintenance:
            success = handle_vgic_maintenance(GUEST_VCPU_ID);
            break;
        case seL4_Fault_VCPUFault:
            success = handle_vcpu_fault(msginfo, GUEST_VCPU_ID);
            break;
        case seL4_Fault_VPPIEvent:
            success = handle_vppi_event();
            break;
    }

    if(!success)
    {
        LOG_VMM_ERR("%s, stopping VM with ID %d\n", faultLabel, id);
        microkit_vcpu_stop(id);
        return seL4_False;
    }
    return seL4_True;
}
