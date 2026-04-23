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
LD := $(TARGET_TRIPLE)-ld
AS := $(TARGET_TRIPLE)-as

MICROKIT_TOOL ?= $(MICROKIT_SDK)/bin/microkit

# Source files
PING_PONG_OBJS := ping_pong_component.o

# VMM source files
VMM_SRCS := vmm/src/vmm.c \
            vmm/src/fault.c \
            vmm/src/psci.c \
            vmm/src/smc.c \
            vmm/src/util/printf.c \
            vmm/src/util/util.c \
            vmm/src/vgic/vgic.c \
            vmm/src/vgic/vgic_v2.c \
            vmm/src/global_data.S

VMM_OBJS := $(patsubst %.S,%.o,$(patsubst %.c,%.o,$(notdir $(VMM_SRCS))))
VMM_OBJS := $(addprefix $(BUILD_DIR)/,$(VMM_OBJS))

IMAGES := ping_pong_component.elf vmm.elf

# Note: disable some warnings that arise from VMM code
CFLAGS := -nostdlib -ffreestanding -g -O0 -Wall \
          -Wno-array-bounds -Wno-unused-variable -Wno-unused-function \
          -Werror \
          -I$(BOARD_DIR)/include \
          -Ivmm/src/util \
          -Ivmm/src \
          -Ivmm/src/vgic \
          -Ivmm/src/arch/aarch64 \
          -DBOARD_$(MICROKIT_BOARD) \
          $(CFLAGS_ARCH)

LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

# VMM image paths
KERNEL_IMAGE = vm_images/linux
DTB_IMAGE = vm_images/linux.dtb
INITRD_IMAGE = vm_images/rootfs.cpio.gz

# Agent build paths
AGENT_BUILD_DIR = $(BUILD_DIR)/agent
AGENT_BIN = $(AGENT_BUILD_DIR)/MicroXRCEAgent
AGENT_SRC_DIR = $(AGENT_BUILD_DIR)/src/Micro-XRCE-DDS-Agent
INITRD_ORIG = vm_images/rootfs.cpio.gz.orig
INITRD_SCRIPT = S60microros_agent

all: directories build_agent repack_initrd $(IMAGE_FILE)

directories:
	@mkdir -p $(BUILD_DIR)

# ------------------------------------------------------------------
# Build Micro-XRCE-DDS-Agent statically for aarch64
# ------------------------------------------------------------------
build_agent: $(AGENT_BIN)

$(AGENT_BIN):
	@echo "Building Micro-XRCE-DDS-Agent for aarch64..."
	@mkdir -p $(AGENT_BUILD_DIR)
	@if [ ! -d "$(AGENT_SRC_DIR)" ]; then \
		echo "Cloning Micro-XRCE-DDS-Agent..."; \
		git clone --depth 1 --branch v2.4.3 \
			https://github.com/eProsima/Micro-XRCE-DDS-Agent.git \
			"$(AGENT_SRC_DIR)"; \
	fi
	@printf '%s\n' \
		'set(CMAKE_SYSTEM_NAME Linux)' \
		'set(CMAKE_SYSTEM_PROCESSOR aarch64)' \
		'set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)' \
		'set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)' \
		'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)' \
		'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)' \
		'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)' \
		> "$(AGENT_BUILD_DIR)/aarch64-toolchain.cmake"
	@mkdir -p $(AGENT_SRC_DIR)/build
	@CLEAN_PATH=$$(echo "$$PATH" | tr ':' '\n' | grep -v '^/opt/ros/' | tr '\n' ':' | sed 's/:*$$//'); \
	cd $(AGENT_SRC_DIR)/build && \
		env -u CMAKE_PREFIX_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH \
			-u LD_LIBRARY_PATH -u PYTHONPATH \
			PATH="$$CLEAN_PATH" \
			cmake .. \
			-DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/$(AGENT_BUILD_DIR)/aarch64-toolchain.cmake \
			-DUAGENT_SUPERBUILD=ON \
			-DBUILD_SHARED_LIBS=OFF \
			-DCMAKE_EXE_LINKER_FLAGS="-static" \
			-DUAGENT_BUILD_EXECUTABLE=ON
	@CLEAN_PATH=$$(echo "$$PATH" | tr ':' '\n' | grep -v '^/opt/ros/' | tr '\n' ':' | sed 's/:*$$//'); \
	cd $(AGENT_SRC_DIR)/build && \
		env -u CMAKE_PREFIX_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH \
			-u LD_LIBRARY_PATH -u PYTHONPATH \
			PATH="$$CLEAN_PATH" \
			make -j$(shell nproc)
	@cp $(AGENT_SRC_DIR)/build/MicroXRCEAgent $(AGENT_BIN)
	@echo "MicroXRCEAgent built: $(AGENT_BIN)"

# ------------------------------------------------------------------
# Repack initrd with agent binary and init script
# ------------------------------------------------------------------
repack_initrd: $(INITRD_IMAGE)

$(INITRD_IMAGE): $(AGENT_BIN) $(INITRD_SCRIPT)
	@echo "Repacking initrd with MicroXRCEAgent..."
	@if [ ! -f "$(INITRD_ORIG)" ]; then \
		echo "ERROR: Original initrd not found at $(INITRD_ORIG)"; \
		echo "Run ./setup.sh first to download VM images"; \
		exit 1; \
	fi
	@TMP_INITRD=$$(mktemp -d); \
	cd $$TMP_INITRD; \
	gunzip -c "$(CURDIR)/$(INITRD_ORIG)" | cpio -idmv >/dev/null 2>&1; \
	cp "$(CURDIR)/$(AGENT_BIN)" bin/MicroXRCEAgent; \
	chmod +x bin/MicroXRCEAgent; \
	cp "$(CURDIR)/$(INITRD_SCRIPT)" etc/init.d/S60microros_agent; \
	chmod +x etc/init.d/S60microros_agent; \
	rm -f etc/init.d/S60Wordle; \
	mkdir -p run/network; \
	find . -print0 | cpio --null -ov -H newc | gzip -9 > "$(CURDIR)/$(INITRD_IMAGE)"; \
	cd "$(CURDIR)"; \
	rm -rf $$TMP_INITRD
	@echo "Initrd repacked: $(INITRD_IMAGE)"

$(BUILD_DIR)/ping_pong_component.o: ping_pong_component.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vmm.o: vmm/src/vmm.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/fault.o: vmm/src/fault.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/psci.o: vmm/src/psci.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/smc.o: vmm/src/smc.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/printf.o: vmm/src/util/printf.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/util.o: vmm/src/util/util.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vgic.o: vmm/src/vgic/vgic.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/vgic_v2.o: vmm/src/vgic/vgic_v2.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/global_data.o: vmm/src/global_data.S $(KERNEL_IMAGE) $(DTB_IMAGE) $(INITRD_IMAGE) Makefile
	$(CC) -c -g -x assembler-with-cpp \
		-DVM_KERNEL_IMAGE_PATH=\"$(KERNEL_IMAGE)\" \
		-DVM_DTB_IMAGE_PATH=\"$(DTB_IMAGE)\" \
		-DVM_INITRD_IMAGE_PATH=\"$(INITRD_IMAGE)\" \
		$< -o $@

$(BUILD_DIR)/ping_pong_component.elf: $(addprefix $(BUILD_DIR)/, $(PING_PONG_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/vmm.elf: $(VMM_OBJS)
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) ping_pong_sel4.system
	$(MICROKIT_TOOL) ping_pong_sel4.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean directories build_agent repack_initrd
