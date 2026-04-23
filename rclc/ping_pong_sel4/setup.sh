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
if [! -d "$MICROKIT_SDK" ]; then
    cd "$INSTALL_DIR"
    curl -L -o "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz" \
        "https://github.com/seL4/microkit/releases/download/${MICROKIT_VERSION}/microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    tar -xzf "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    rm "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
fi

TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
if [! -d "$TOOLCHAIN_DIR" ]; then
    cd "$INSTALL_DIR"
    curl -L -o "arm-gnu-toolchain.tar.xz" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz"
    tar -xf "arm-gnu-toolchain.tar.xz"
    rm "arm-gnu-toolchain.tar.xz"
fi

export MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
export TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

echo "Environment configured:"
echo "  MICROKIT_SDK=$MICROKIT_SDK"
echo "  TOOLCHAIN_DIR=$TOOLCHAIN_DIR"
echo "  Added to PATH: $TOOLCHAIN_DIR/bin"
