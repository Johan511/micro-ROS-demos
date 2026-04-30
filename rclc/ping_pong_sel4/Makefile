ifeq ($(strip $(BUILD_DIR)),)
$(error BUILD_DIR must be specified)
endif
ifeq ($(strip $(MICROKIT_SDK)),)
$(error MICROKIT_SDK must be specified)
endif
ifeq ($(strip $(MICROKIT_BOARD)),)
$(error MICROKIT_BOARD must be specified)
endif
ifeq ($(strip $(MICROKIT_CONFIG)),)
$(error MICROKIT_CONFIG must be specified)
endif

BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)
ARCH := ${shell grep 'CONFIG_SEL4_ARCH  ' $(BOARD_DIR)/include/kernel/gen_config.h | cut -d' ' -f4}

ifeq ($(ARCH),aarch64)
  TARGET_TRIPLE := aarch64-none-elf
  CFLAGS_ARCH := -mcpu=cortex-a53 -mstrict-align
else ifeq ($(ARCH),riscv64)
  TARGET_TRIPLE := riscv64-unknown-elf
  CFLAGS_ARCH := -march=rv64imafdc_zicsr_zifencei -mabi=lp64d
else ifeq ($(ARCH),x86_64)
  TARGET_TRIPLE := x86_64-linux-gnu
  CFLAGS_ARCH := -march=x86-64 -mtune=generic
else
$(error Unsupported ARCH)
endif

CC := $(TARGET_TRIPLE)-gcc
LD := $(TARGET_TRIPLE)-gcc
MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

MUSLLIBC_DIR ?= $(BUILD_DIR)/musllibc_src
MUSLLIBC_BUILD_DIR ?= $(BUILD_DIR)/musllibc_build

CFLAGS := -nostdlib -ffreestanding -g -O0 -Wall \
          -Wno-array-bounds -Wno-unused-variable -Wno-unused-function \
          -Werror \
          -I$(MUSLLIBC_BUILD_DIR)/obj/include \
          -I$(MUSLLIBC_DIR)/include \
          -I$(MUSLLIBC_DIR)/arch/aarch64_sel4 \
          -I$(MUSLLIBC_DIR)/arch/generic \
          -I$(BOARD_DIR)/include \
          -Ivmm/src/util \
          -Ivmm/src \
          -Ivmm/src/vgic \
          -Ivmm/src/arch/aarch64 \
          -DBOARD_$(MICROKIT_BOARD) \
          $(CFLAGS_ARCH)

LDFLAGS := -L$(MUSLLIBC_BUILD_DIR)/lib -L$(BOARD_DIR)/lib -nostartfiles
# link order of microkit and libc matters
# we do not want libc overriding microkit
LIBS := -lmicrokit -lc -Tmicrokit.ld

IMAGE_FILE := $(BUILD_DIR)/loader.img
REPORT_FILE := $(BUILD_DIR)/report.txt

KERNEL_IMAGE := vm_images/linux
DTB_IMAGE := vm_images/linux.dtb
INITRD_IMAGE := vm_images/rootfs.cpio.gz

VMM_OBJS := $(BUILD_DIR)/vmm.o \
            $(BUILD_DIR)/fault.o \
            $(BUILD_DIR)/psci.o \
            $(BUILD_DIR)/smc.o \
            $(BUILD_DIR)/printf.o \
            $(BUILD_DIR)/util.o \
            $(BUILD_DIR)/vgic.o \
            $(BUILD_DIR)/vgic_v2.o \
            $(BUILD_DIR)/global_data.o

all: $(IMAGE_FILE)

$(BUILD_DIR)/ping_pong_component.o: ping_pong_component.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vmm.o: vmm/src/vmm.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fault.o: vmm/src/fault.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/psci.o: vmm/src/psci.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/smc.o: vmm/src/smc.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/printf.o: vmm/src/util/printf.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/util.o: vmm/src/util/util.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vgic.o: vmm/src/vgic/vgic.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vgic_v2.o: vmm/src/vgic/vgic_v2.c | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/global_data.o: vmm/src/global_data.S $(KERNEL_IMAGE) $(DTB_IMAGE) $(INITRD_IMAGE) | $(BUILD_DIR)
	$(CC) -c -g -x assembler-with-cpp \
		-DVM_KERNEL_IMAGE_PATH=\"$(KERNEL_IMAGE)\" \
		-DVM_DTB_IMAGE_PATH=\"$(DTB_IMAGE)\" \
		-DVM_INITRD_IMAGE_PATH=\"$(INITRD_IMAGE)\" \
		$< -o $@

$(BUILD_DIR)/ping_pong_component.elf: $(BUILD_DIR)/ping_pong_component.o
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/vmm.elf: $(VMM_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(BUILD_DIR)/ping_pong_component.elf $(BUILD_DIR)/vmm.elf ping_pong_sel4.system
	$(MICROKIT_TOOL) ping_pong_sel4.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
