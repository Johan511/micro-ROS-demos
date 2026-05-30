#!/bin/bash
#
# Setup script for ping_pong_sel4
# Downloads and installs the required dependencies
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${INSTALL_DIR:-/opt}"
MICROKIT_VERSION="${MICROKIT_VERSION:-1.4.1}"
TOOLCHAIN_VERSION="${TOOLCHAIN_VERSION:-12.2.rel1}"

apt-get update
apt-get install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    cpio \
    gdb-multiarch \
    qemu-system-arm \
    device-tree-compiler \
    git \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    curl \
    tar \
    xz-utils

MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
if [ ! -d "$MICROKIT_SDK" ]; then
    cd "$INSTALL_DIR"
    curl -L -o "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz" \
        "https://github.com/seL4/microkit/releases/download/${MICROKIT_VERSION}/microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    tar -xzf "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    rm "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
fi

TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
if [ ! -d "$TOOLCHAIN_DIR" ]; then
    cd "$INSTALL_DIR"
    curl -L -o "arm-gnu-toolchain.tar.xz" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz"
    tar -xf "arm-gnu-toolchain.tar.xz"
    rm "arm-gnu-toolchain.tar.xz"
fi

# Download VM images from microkit_tutorial if not present
VM_IMAGES_DIR="$SCRIPT_DIR/vm_images"
if [ ! -f "$VM_IMAGES_DIR/linux" ] || [ ! -f "$VM_IMAGES_DIR/linux.dtb" ] || [ ! -f "$VM_IMAGES_DIR/rootfs.cpio.gz" ]; then
    echo "Downloading VM images from microkit_tutorial..."
    mkdir -p "$VM_IMAGES_DIR"
    TMP_DIR=$(mktemp -d)
    cd "$TMP_DIR"
    git clone --depth 1 https://github.com/au-ts/microkit_tutorial.git
    cp microkit_tutorial/solutions/vmm/images/linux "$VM_IMAGES_DIR/"
    cp microkit_tutorial/solutions/vmm/images/linux.dtb "$VM_IMAGES_DIR/"
    cp microkit_tutorial/solutions/vmm/images/rootfs.cpio.gz "$VM_IMAGES_DIR/"
    rm -rf "$TMP_DIR"
    echo "VM images downloaded to $VM_IMAGES_DIR"
fi

# Preserve pristine original initrd so build can repack from a known-good base
if [ -f "$VM_IMAGES_DIR/rootfs.cpio.gz" ] && [ ! -f "$VM_IMAGES_DIR/rootfs.cpio.gz.orig" ]; then
    cp "$VM_IMAGES_DIR/rootfs.cpio.gz" "$VM_IMAGES_DIR/rootfs.cpio.gz.orig"
    echo "Preserved original initrd as rootfs.cpio.gz.orig"
fi

# Clone libvmm and its SDDF submodule
LIBVMM_DIR="$SCRIPT_DIR/libvmm"
if [ ! -d "$LIBVMM_DIR" ]; then
    echo "Cloning libvmm..."
    git clone --depth 1 --recurse-submodules https://github.com/au-ts/libvmm "$LIBVMM_DIR"
    echo "libvmm cloned"
fi

export MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
export TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

echo "Environment configured:"
echo "  MICROKIT_SDK=$MICROKIT_SDK"
echo "  TOOLCHAIN_DIR=$TOOLCHAIN_DIR"
echo "  PATH includes: $TOOLCHAIN_DIR/bin"

FW_DIR="$SCRIPT_DIR/firmware"
if [ -d "$FW_DIR" ]; then
    echo "micro-ROS firmware workspace already exists at $FW_DIR"
else
    echo "Creating micro-ROS firmware workspace (cloning repos including rclc)..."
    if [ ! -f /opt/ros/humble/setup.bash ]; then
        echo "ERROR: ROS 2 Humble not found at /opt/ros/humble"
        echo "Install ROS 2 Humble first, then re-run ./setup.sh"
        exit 1
    fi
    source /opt/ros/humble/setup.bash
    if [ -f /microros_ws/install/setup.bash ]; then
        source /microros_ws/install/setup.bash
    fi

    pushd "$SCRIPT_DIR" > /dev/null
    ros2 run micro_ros_setup create_firmware_ws.sh generate_lib

    python3 -c "
import json
with open('$FW_DIR/mcu_ws/colcon.meta') as f:
    meta = json.load(f)
meta['names']['microxrcedds_client']['cmake-args'] += [
    '-DUCLIENT_PROFILE_UDP=OFF',
    '-DUCLIENT_PROFILE_TCP=OFF',
    '-DUCLIENT_PROFILE_SERIAL=OFF',
    '-DUCLIENT_PROFILE_DISCOVERY=OFF',
    '-DUCLIENT_PROFILE_CUSTOM_TRANSPORT=ON'
]
meta['names']['rmw_microxrcedds']['cmake-args'] += [
    '-DRMW_UXRCE_TRANSPORT=custom'
]
with open('$FW_DIR/mcu_ws/colcon.meta', 'w') as f:
    json.dump(meta, f, indent=4)
"
    popd > /dev/null
    echo "micro-ROS firmware workspace created"
fi
