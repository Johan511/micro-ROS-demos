#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ -z "$MICROKIT_SDK" ]; then
    echo "Microkit sdk not found at $MICROKIT_SDK"
    echo "Run $SCRIPT_DIR/setup.sh to ensure it is installed"
    exit 1
fi

BUILD_DIR="${BUILD_DIR:-build}"
MICROKIT_BOARD="${MICROKIT_BOARD:-qemu_virt_aarch64}"
MICROKIT_CONFIG="${MICROKIT_CONFIG:-debug}"

mkdir -p "$BUILD_DIR"
make BUILD_DIR="$BUILD_DIR" \
     MICROKIT_SDK="$MICROKIT_SDK" \
     MICROKIT_BOARD="$MICROKIT_BOARD" \
     MICROKIT_CONFIG="$MICROKIT_CONFIG"

echo ""
echo "Output: $BUILD_DIR/loader.img"
echo "To run on QEMU:"
echo "  ./run.sh"
