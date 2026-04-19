#!/bin/bash
#
# Environment setup for ping_pong_sel4
# Source this script before building: source env.sh
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_DIR="${INSTALL_DIR:-/opt}"
MICROKIT_VERSION="${MICROKIT_VERSION:-1.4.1}"
TOOLCHAIN_VERSION="${TOOLCHAIN_VERSION:-12.2.rel1}"

export MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
export TOOLCHAIN_DIR="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"

# Add toolchain to PATH
export PATH="$TOOLCHAIN_DIR/bin:$PATH"

# Verify installations
if [ ! -d "$MICROKIT_SDK" ]; then
    echo "ERROR: Microkit SDK not found at $MICROKIT_SDK"
    echo "Run ./setup.sh first"
    return 1
fi

if [ ! -d "$TOOLCHAIN_DIR" ]; then
    echo "ERROR: ARM toolchain not found at $TOOLCHAIN_DIR"
    echo "Run ./setup.sh first"
    return 1
fi

echo "Environment configured:"
echo "  MICROKIT_SDK=$MICROKIT_SDK"
echo "  TOOLCHAIN_DIR=$TOOLCHAIN_DIR"
echo "  Added to PATH: $TOOLCHAIN_DIR/bin"
