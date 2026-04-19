#!/bin/bash
#
# Run script for ping_pong_sel4 on QEMU
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

BUILD_DIR="${BUILD_DIR:-build}"
LOADER_IMG="$BUILD_DIR/loader.img"

if [ ! -f "$LOADER_IMG" ]; then
    echo "ERROR: $LOADER_IMG not found"
    echo "Run ./build.sh first"
    exit 1
fi

echo "=== Running ping_pong_sel4 on QEMU ==="
echo "Loader image: $LOADER_IMG"
echo ""
echo "Press Ctrl+A then X to exit QEMU"
echo ""

qemu-system-aarch64 \
    -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -nographic \
    -serial mon:stdio \
    -device loader,file="$LOADER_IMG",addr=0x70000000,cpu-num=0 \
    -m size=2G
