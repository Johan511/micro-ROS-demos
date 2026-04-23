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
echo "Starting QEMU with virtio-net (VM can access network)"
qemu-system-aarch64 \
    -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -nographic \
    -serial mon:stdio \
    -device loader,file="$LOADER_IMG",addr=0x70000000,cpu-num=0 \
    -m size=2G \
    -netdev user,id=mynet0 \
    -device virtio-net-device,netdev=mynet0,mac=52:55:00:d1:55:01