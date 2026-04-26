#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Auto-configure environment if not already set
MICROKIT_VERSION="${MICROKIT_VERSION:-1.4.1}"
TOOLCHAIN_VERSION="${TOOLCHAIN_VERSION:-12.2.rel1}"
INSTALL_DIR="${INSTALL_DIR:-/opt}"

if [ -z "$MICROKIT_SDK" ]; then
    export MICROKIT_SDK="$INSTALL_DIR/microkit-sdk-$MICROKIT_VERSION"
fi

if [ -z "$(which aarch64-none-elf-gcc 2>/dev/null)" ]; then
    export PATH="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf/bin:$PATH"
fi

if [ ! -d "$MICROKIT_SDK" ]; then
    echo "ERROR: MICROKIT_SDK not found at $MICROKIT_SDK"
    echo "Run ./setup.sh first"
    exit 1
fi

BUILD_DIR="${BUILD_DIR:-build}"
MICROKIT_BOARD="${MICROKIT_BOARD:-qemu_virt_aarch64}"
MICROKIT_CONFIG="${MICROKIT_CONFIG:-debug}"

AGENT_BUILD_DIR="$BUILD_DIR/agent"
AGENT_SRC_DIR="$AGENT_BUILD_DIR/src/Micro-XRCE-DDS-Agent"
AGENT_BIN="$AGENT_BUILD_DIR/MicroXRCEAgent"
INITRD_ORIG="vm_images/rootfs.cpio.gz.orig"
INITRD_IMAGE="vm_images/rootfs.cpio.gz"

# ------------------------------------------------------------------
# Phase A: Build Micro-XRCE-DDS-Agent (static aarch64 binary)
# ------------------------------------------------------------------
build_agent() {
    if [ -f "$AGENT_BIN" ]; then
        echo "MicroXRCEAgent already built at $AGENT_BIN"
        return
    fi

    mkdir -p "$AGENT_BUILD_DIR"
    if [ ! -d "$AGENT_SRC_DIR" ]; then
        echo "Cloning Micro-XRCE-DDS-Agent..."
        git clone --depth 1 --branch v2.4.3 \
            https://github.com/eProsima/Micro-XRCE-DDS-Agent.git \
            "$AGENT_SRC_DIR"
    fi

    printf '%s\n' \
        'set(CMAKE_SYSTEM_NAME Linux)' \
        'set(CMAKE_SYSTEM_PROCESSOR aarch64)' \
        'set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)' \
        'set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)' \
        'set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)' \
        > "$AGENT_BUILD_DIR/aarch64-toolchain.cmake"

    # Build a clean PATH that excludes ROS paths.  CMake searches
    # ../lib/<package>/cmake relative to every PATH entry, which causes it
    # to find the host x86_64 foonathan_memory in /opt/ros/humble.
    CLEAN_PATH=""
    OLDIFS="$IFS"
    IFS=':'
    for p in $PATH; do
        case "$p" in
            /opt/ros/*) ;;  # skip ROS paths
            *) [ -z "$CLEAN_PATH" ] && CLEAN_PATH="$p" || CLEAN_PATH="$CLEAN_PATH:$p" ;;
        esac
    done
    IFS="$OLDIFS"

    mkdir -p "$AGENT_SRC_DIR/build"
    pushd "$AGENT_SRC_DIR/build"

    env -u CMAKE_PREFIX_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH \
        -u LD_LIBRARY_PATH -u PYTHONPATH \
        PATH="$CLEAN_PATH" \
        cmake .. \
        -DCMAKE_TOOLCHAIN_FILE="$SCRIPT_DIR/$AGENT_BUILD_DIR/aarch64-toolchain.cmake" \
        -DUAGENT_SUPERBUILD=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_EXE_LINKER_FLAGS="-static" \
        -DUAGENT_BUILD_EXECUTABLE=ON

    env -u CMAKE_PREFIX_PATH -u AMENT_PREFIX_PATH -u COLCON_PREFIX_PATH \
        -u LD_LIBRARY_PATH -u PYTHONPATH \
        PATH="$CLEAN_PATH" \
        make -j"$(nproc)"

    cp "$SCRIPT_DIR/$AGENT_SRC_DIR/build/MicroXRCEAgent" "$SCRIPT_DIR/$AGENT_BIN"
    echo "MicroXRCEAgent built successfully: $SCRIPT_DIR/$AGENT_BIN"

    popd
}

repack_initrd() {
    if [ ! -f "$AGENT_BIN" ]; then
        echo "ERROR: MicroXRCEAgent binary not found at $AGENT_BIN"
        exit 1
    fi

    if [ ! -f "$INITRD_ORIG" ]; then
        echo "ERROR: Original initrd not found at $INITRD_ORIG"
        echo "Run ./setup.sh first to download VM images"
        exit 1
    fi

    TMP_INITRD=$(mktemp -d)
    pushd "$TMP_INITRD"

    # Extract original initrd
    gunzip -c "$SCRIPT_DIR/$INITRD_ORIG" | cpio -idmv >/dev/null 2>&1

    # Copy agent binary
    cp "$SCRIPT_DIR/$AGENT_BIN" bin/MicroXRCEAgent
    chmod +x bin/MicroXRCEAgent
    cp "$SCRIPT_DIR/S60microros_agent" etc/init.d/S60microros_agent
    chmod +x etc/init.d/S60microros_agent

    # Repack initrd
    find . -print0 | cpio --null -ov -H newc | gzip -9 > "$SCRIPT_DIR/$INITRD_IMAGE"

    popd
    rm -rf "$TMP_INITRD"
    echo "Initrd repacked: $INITRD_IMAGE"
}

build_agent
repack_initrd
make BUILD_DIR="$BUILD_DIR" \
     MICROKIT_SDK="$MICROKIT_SDK" \
     MICROKIT_BOARD="$MICROKIT_BOARD" \
     MICROKIT_CONFIG="$MICROKIT_CONFIG"

echo ""
echo "Build complete!"
echo "  Loader image: $BUILD_DIR/loader.img"
echo "  Report: $BUILD_DIR/report.txt"
echo ""
echo "To run on QEMU:"
echo "  ./run.sh"
