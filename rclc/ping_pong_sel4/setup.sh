#!/bin/bash
#
# Setup script for ping_pong_sel4
# This script downloads and installs the required dependencies
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${INSTALL_DIR:-/opt}"
MICROKIT_VERSION="${MICROKIT_VERSION:-1.4.1}"
TOOLCHAIN_VERSION="${TOOLCHAIN_VERSION:-12.2.rel1}"

echo "=== ping_pong_sel4 Setup ==="
echo "Install directory: $INSTALL_DIR"
echo ""

# Install system dependencies
echo "=== Installing system dependencies ==="
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

# Download Microkit SDK if not present
MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
if [ -d "$MICROKIT_SDK" ]; then
    echo "=== Microkit SDK already installed at $MICROKIT_SDK ==="
else
    echo "=== Downloading Microkit SDK v$MICROKIT_VERSION ==="
    cd "$INSTALL_DIR"
    curl -L -o "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz" \
        "https://github.com/seL4/microkit/releases/download/${MICROKIT_VERSION}/microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    tar -xzf "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    rm "microkit-sdk-${MICROKIT_VERSION}-linux-x86-64.tar.gz"
    echo "Microkit SDK installed at $MICROKIT_SDK"
fi

# Download ARM toolchain if not present
TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
if [ -d "$TOOLCHAIN_DIR" ]; then
    echo "=== ARM toolchain already installed at $TOOLCHAIN_DIR ==="
else
    echo "=== Downloading ARM GCC toolchain v$TOOLCHAIN_VERSION ==="
    cd "$INSTALL_DIR"
    curl -L -o "arm-gnu-toolchain.tar.xz" \
        "https://developer.arm.com/-/media/Files/downloads/gnu/${TOOLCHAIN_VERSION}/binrel/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf.tar.xz"
    tar -xf "arm-gnu-toolchain.tar.xz"
    rm "arm-gnu-toolchain.tar.xz"
    echo "ARM toolchain installed at $TOOLCHAIN_DIR"
fi

echo ""
echo "=== Setup complete ==="
echo ""
echo "Add the following to your shell profile or run before building:"
echo "  export MICROKIT_SDK=$MICROKIT_SDK"
echo "  export PATH=\"$TOOLCHAIN_DIR/bin:\$PATH\""
echo ""
echo "Or source the environment script:"
echo "  source $SCRIPT_DIR/env.sh"
