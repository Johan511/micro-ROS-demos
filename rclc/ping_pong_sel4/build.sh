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
VM_ECHO_BIN="$BUILD_DIR/vm_echo"
INITRD_ORIG="vm_images/rootfs.cpio.gz.orig"
INITRD_IMAGE="vm_images/rootfs.cpio.gz"

# musl libc build configuration
MUSLLIBC_REPO="${MUSLLIBC_REPO:-https://github.com/seL4/musllibc.git}"
MUSLLIBC_DIR="$SCRIPT_DIR/$BUILD_DIR/musllibc_src"
MUSLLIBC_BUILD_DIR="$SCRIPT_DIR/$BUILD_DIR/musllibc_build"

# micro-ROS cross-compilation output paths
UROS_BUILD_DIR="$BUILD_DIR/uros"
TOOLCHAIN_FILE="$UROS_BUILD_DIR/aarch64-none-elf-toolchain.cmake"

# ------------------------------------------------------------------
# Phase A: Build musl libc (static aarch64 library)
# ------------------------------------------------------------------
build_musllibc() {
    if [ -f "${MUSLLIBC_BUILD_DIR}/lib/libc.a" ]; then
        echo "musllibc already built at ${MUSLLIBC_BUILD_DIR}/lib/libc.a"
        return
    fi

    if [ ! -d "${MUSLLIBC_DIR}" ]; then
        echo "Cloning musllibc from ${MUSLLIBC_REPO}..."
        git clone --depth 1 "${MUSLLIBC_REPO}" "${MUSLLIBC_DIR}"
    fi

    mkdir -p "${MUSLLIBC_BUILD_DIR}"

    if [ ! -f "${MUSLLIBC_BUILD_DIR}/config.mak" ]; then
        echo "Configuring musllibc..."
        pushd "${MUSLLIBC_BUILD_DIR}" > /dev/null
        env \
            SOURCE_DIR="${MUSLLIBC_DIR}" \
            STAGE_DIR="${MUSLLIBC_BUILD_DIR}/install" \
            CC=aarch64-linux-gnu-gcc \
            AR=aarch64-linux-gnu-ar \
            CROSS_COMPILE=aarch64-linux-gnu- \
            "${MUSLLIBC_DIR}/configure" \
            --srcdir="${MUSLLIBC_DIR}" \
            --prefix="${MUSLLIBC_BUILD_DIR}/install" \
            --target=aarch64 \
            --enable-warnings \
            --disable-shared \
            --enable-static
        popd > /dev/null
    fi

    echo "Building musllibc..."
    make -C "${MUSLLIBC_BUILD_DIR}" \
        SOURCE_DIR="${MUSLLIBC_DIR}" \
        STAGE_DIR="${MUSLLIBC_BUILD_DIR}/install" \
        CC=aarch64-linux-gnu-gcc \
        AR=aarch64-linux-gnu-ar \
        CROSS_COMPILE=aarch64-linux-gnu- \
        -j"$(nproc)"

    echo "musllibc built successfully"
}

# ------------------------------------------------------------------
# Phase B: Build Micro-XRCE-DDS-Agent (static aarch64 binary)
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

build_vm_echo() {
    aarch64-linux-gnu-gcc -static -o "$VM_ECHO_BIN" vm_echo.c
    echo "vm_echo built successfully: $VM_ECHO_BIN"
}

repack_initrd() {
    if [ ! -f "$AGENT_BIN" ]; then
        echo "ERROR: MicroXRCEAgent binary not found at $AGENT_BIN"
        exit 1
    fi

    if [ ! -f "$VM_ECHO_BIN" ]; then
        echo "ERROR: vm_echo binary not found at $VM_ECHO_BIN"
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

    # Copy echo listener
    cp "$SCRIPT_DIR/$VM_ECHO_BIN" bin/vm_echo
    chmod +x bin/vm_echo
    cp "$SCRIPT_DIR/S50echolistener" etc/init.d/S50echolistener
    chmod +x etc/init.d/S50echolistener

    # Repack initrd
    find . -print0 | cpio --null -o -H newc | gzip -9 > "$SCRIPT_DIR/$INITRD_IMAGE"

    popd
    rm -rf "$TMP_INITRD"
    echo "Initrd repacked: $INITRD_IMAGE"
}

build_uros_libs() {
    if [ -f "$UROS_BUILD_DIR/lib/libmicroros.a" ]; then
        echo "micro-ROS libraries already built at $UROS_BUILD_DIR/lib/libmicroros.a"
        return
    fi

    if [ ! -f "${MUSLLIBC_BUILD_DIR}/lib/libc.a" ]; then
        echo "ERROR: musllibc must be built first (Phase A)"
        exit 1
    fi

    if [ ! -d /opt/ros/humble ]; then
        echo "ERROR: ROS 2 Humble not found at /opt/ros/humble"
        exit 1
    fi

    echo ""
    echo "=== Phase D: Cross-compiling micro-ROS libraries for seL4 ==="

    source /opt/ros/humble/setup.bash
    if [ -f /microros_ws/install/setup.bash ]; then
        source /microros_ws/install/setup.bash
    fi

    mkdir -p "$UROS_BUILD_DIR"

    TOOLCHAIN_PREFIX="$INSTALL_DIR/arm-gnu-toolchain-${TOOLCHAIN_VERSION}-x86_64-aarch64-none-elf"
    TOOLCHAIN_CC="${TOOLCHAIN_PREFIX}/bin/aarch64-none-elf-gcc"
    TOOLCHAIN_CXX="${TOOLCHAIN_PREFIX}/bin/aarch64-none-elf-g++"

    MUSLLIBC_INCLUDES="-I${MUSLLIBC_BUILD_DIR}/obj/include -I${MUSLLIBC_DIR}/include -I${MUSLLIBC_DIR}/arch/aarch64_sel4 -I${MUSLLIBC_DIR}/arch/generic"

    cat > "$TOOLCHAIN_FILE" << TOOLCHAIN_EOF
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
TOOLCHAIN_EOF

    cat >> "$TOOLCHAIN_FILE" << EOF
set(CMAKE_C_COMPILER ${TOOLCHAIN_CC})
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_CXX})
set(CMAKE_C_FLAGS "-nostdlib -ffreestanding -mcpu=cortex-a53 -mstrict-align ${MUSLLIBC_INCLUDES} \${CMAKE_C_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "-nostdlib -ffreestanding -mcpu=cortex-a53 -mstrict-align ${MUSLLIBC_INCLUDES} \${CMAKE_CXX_FLAGS}" CACHE STRING "" FORCE)
EOF

    echo "Toolchain file: $TOOLCHAIN_FILE"

    FW_DIR="$SCRIPT_DIR/firmware"
    if [ -d "$FW_DIR" ]; then
        echo "Removing previous firmware workspace..."
        rm -rf "$FW_DIR"
    fi

    echo "Creating micro-ROS firmware workspace (cloning repos)..."
    pushd "$SCRIPT_DIR" > /dev/null
    ros2 run micro_ros_setup create_firmware_ws.sh generate_lib
    popd > /dev/null

    echo "Configuring colcon.meta for seL4 (custom transport only)..."
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

    echo "Cross-compiling with seL4 toolchain..."
    pushd "$SCRIPT_DIR" > /dev/null
    ros2 run micro_ros_setup build_firmware.sh "$(realpath "$TOOLCHAIN_FILE")"
    popd > /dev/null

    if [ -f "$FW_DIR/build/libmicroros.a" ]; then
        mkdir -p "$UROS_BUILD_DIR/lib"
        cp "$FW_DIR/build/libmicroros.a" "$UROS_BUILD_DIR/lib/"
        echo "libmicroros.a installed to $UROS_BUILD_DIR/lib/"

        mkdir -p "$UROS_BUILD_DIR/include"
        cp -R "$FW_DIR/build/include/"* "$UROS_BUILD_DIR/include/" 2>/dev/null || true
        echo "Headers installed to $UROS_BUILD_DIR/include/"
    else
        echo "ERROR: libmicroros.a not found at $FW_DIR/build/libmicroros.a"
        echo "Build may have failed. Check output above."
        exit 1
    fi

    echo "=== Phase D complete ==="
    echo ""
}

build_musllibc
build_agent
build_vm_echo
repack_initrd
build_uros_libs
make BUILD_DIR="$BUILD_DIR" \
     MICROKIT_SDK="$MICROKIT_SDK" \
     MICROKIT_BOARD="$MICROKIT_BOARD" \
     MICROKIT_CONFIG="$MICROKIT_CONFIG" \
     MUSLLIBC_DIR="$MUSLLIBC_DIR" \
     MUSLLIBC_BUILD_DIR="$MUSLLIBC_BUILD_DIR" \
     UROS_BUILD_DIR="$UROS_BUILD_DIR"

echo ""
echo "Build complete!"
echo "  Loader image: $BUILD_DIR/loader.img"
echo "  Report: $BUILD_DIR/report.txt"
echo ""
echo "To run on QEMU:"
echo "  ./run.sh"
