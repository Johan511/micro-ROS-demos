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
  CFLAGS_ARCH := -mstrict-align
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

IMAGES := ping_pong_component.elf
CFLAGS := -nostdlib -ffreestanding -g -O0 -Wall -Wno-unused-function -Werror -I$(BOARD_DIR)/include $(CFLAGS_ARCH)
LDFLAGS := -L$(BOARD_DIR)/lib
LIBS := -lmicrokit -Tmicrokit.ld

IMAGE_FILE = $(BUILD_DIR)/loader.img
REPORT_FILE = $(BUILD_DIR)/report.txt

all: $(IMAGE_FILE)

$(BUILD_DIR)/%.o: %.c Makefile
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/ping_pong_component.elf: $(addprefix $(BUILD_DIR)/, $(PING_PONG_OBJS))
	$(LD) $(LDFLAGS) $^ $(LIBS) -o $@

$(IMAGE_FILE) $(REPORT_FILE): $(addprefix $(BUILD_DIR)/, $(IMAGES)) ping_pong_sel4.system
	$(MICROKIT_TOOL) ping_pong_sel4.system --search-path $(BUILD_DIR) --board $(MICROKIT_BOARD) --config $(MICROKIT_CONFIG) -o $(IMAGE_FILE) -r $(REPORT_FILE)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all clean
