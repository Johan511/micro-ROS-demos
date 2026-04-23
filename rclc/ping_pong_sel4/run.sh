#!/bin/bash
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

echo "Loader image: $LOADER_IMG"
qemu-system-aarch64 \
    -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -nographic \
    -serial mon:stdio \
    -device loader,file="$LOADER_IMG",addr=0x70000000,cpu-num=0 \
    -m size=2G
