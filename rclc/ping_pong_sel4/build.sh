#!/bin/bash
#
# Build script for ping_pong_sel4
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Source environment
if [ -z "$MICROKIT_SDK" ]; then
    echo "Sourcing environment from env.sh..."
    source "$SCRIPT_DIR/env.sh"
fi

# Build configuration
BUILD_DIR="${BUILD_DIR:-build}"
MICROKIT_BOARD="${MICROKIT_BOARD:-qemu_virt_aarch64}"
MICROKIT_CONFIG="${MICROKIT_CONFIG:-debug}"

echo "=== Building ping_pong_sel4 ==="
echo "Build directory: $BUILD_DIR"
echo "Microkit SDK: $MICROKIT_SDK"
echo "Board: $MICROKIT_BOARD"
echo "Config: $MICROKIT_CONFIG"
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"

# Build
make BUILD_DIR="$BUILD_DIR" \
     MICROKIT_SDK="$MICROKIT_SDK" \
     MICROKIT_BOARD="$MICROKIT_BOARD" \
     MICROKIT_CONFIG="$MICROKIT_CONFIG"

echo ""
echo "=== Build complete ==="
echo "Output: $BUILD_DIR/loader.img"
echo ""
echo "To run on QEMU:"
echo "  ./run.sh"
