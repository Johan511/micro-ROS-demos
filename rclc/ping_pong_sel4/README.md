# ping_pong_sel4 - Microkit-based Ping-Pong Application

This is a micro-ROS ping-pong application designed to run on seL4 using Microkit.
It demonstrates a native seL4 application running in a protection domain.

## Overview

The application consists of a single Microkit protection domain that:
- Initializes shared memory for inter-PD communication
- Demonstrates basic Microkit APIs (notifications, protected calls)
- Provides a foundation for porting micro-ROS to seL4

## Prerequisites

- Linux system (tested on Ubuntu 22.04)
- Internet connection for downloading dependencies
- sudo access for installing packages

## Quick Start

### 1. Setup (One-time)

```bash
cd /microros_ws/src/uros/micro-ROS-demos/rclc/ping_pong_sel4
./setup.sh
```

This will:
- Install required system packages (QEMU, cross-compiler dependencies, etc.)
- Download Microkit SDK v1.4.1 to `/opt/microkit-sdk-1.4.1`
- Download ARM GCC toolchain to `/opt/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf`

### 2. Build

```bash
source ./env.sh    # Set up environment variables
./build.sh         # Build the application
```

Or manually:
```bash
export MICROKIT_SDK=/opt/microkit-sdk-1.4.1
export PATH="/opt/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin:$PATH"
make BUILD_DIR=build MICROKIT_SDK=$MICROKIT_SDK MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_CONFIG=debug
```

### 3. Run on QEMU

```bash
./run.sh
```

Or manually:
```bash
qemu-system-aarch64 \
    -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -nographic \
    -serial mon:stdio \
    -device loader,file=build/loader.img,addr=0x70000000,cpu-num=0 \
    -m size=2G
```

## Expected Output

```
MON|INFO: Microkit Bootstrap
MON|INFO: bootinfo untyped list matches expected list
MON|INFO: Number of bootstrap invocations: 0x00000009
MON|INFO: Number of system invocations:    0x00000026
MON|INFO: completed bootstrap invocations
MON|INFO: completed system invocations
ping_pong: Initializing...
ping_pong: Initialized, shared memory at 0x0000000002000000
ping_pong: Message: Hello from ping_pong!
```

Press `Ctrl+A` then `X` to exit QEMU.

## Files

| File | Description |
|------|-------------|
| `ping_pong_component.c` | Main application code |
| `ping_pong_sel4.system` | Microkit system description (XML) |
| `Makefile` | Build system |
| `setup.sh` | One-time setup script |
| `env.sh` | Environment configuration (source this) |
| `build.sh` | Build script |
| `run.sh` | Run on QEMU script |
| `CMakeLists.txt` | ROS/cmake integration (for micro-ROS build) |
| `README.md` | This file |

## Architecture

```
┌─────────────────────────────────────────┐
│         seL4 Microkernel (EL2)          │
├─────────────────────────────────────────┤
│  ┌─────────────────────────────────┐   │
│  │   Microkit Monitor              │   │
│  │   (initial task, root server)   │   │
│  └─────────────────────────────────┘   │
│                   │                     │
│  ┌─────────────────────────────────┐   │
│  │   ping_pong Protection Domain   │   │
│  │                                 │   │
│  │  • Shared memory @ 0x2000000    │   │
│  │  • IPC buffer for seL4 calls    │   │
│  │  • Stack (4KB)                  │   │
│  │  • Text/Data sections           │   │
│  │                                 │   │
│  │  Handlers:                      │   │
│  │  • init() - initialization      │   │
│  │  • notified() - notifications   │   │
│  │  • protected() - PPC calls      │   │
│  │  • fault() - fault handling     │   │
│  └─────────────────────────────────┘   │
└─────────────────────────────────────────┘
```

## Building for Other Boards

The default configuration targets `qemu_virt_aarch64`. To build for other boards:

```bash
export MICROKIT_BOARD=imx8mm_evk  # or other supported board
./build.sh
```

Supported boards (from Microkit SDK):
- `qemu_virt_aarch64` (default) - QEMU virt platform for AArch64
- `qemu_virt_riscv64` - QEMU virt platform for RISC-V
- `imx8mm_evk` - NXP i.MX 8M Mini EVK
- `imx8mq_evk` - NXP i.MX 8M Quad EVK
- `odroidc2` - Hardkernel ODROID-C2
- `odroidc4` - Hardkernel ODROID-C4
- `maaxboard` - MaaXBoard
- `zcu102` - Xilinx ZCU102

## Debugging

### With GDB

1. Start QEMU with GDB server:
```bash
qemu-system-aarch64 \
    -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -nographic \
    -serial mon:stdio \
    -device loader,file=build/loader.img,addr=0x70000000,cpu-num=0 \
    -m size=2G \
    -s -S
```

2. In another terminal:
```bash
aarch64-none-elf-gdb build/ping_pong_component.elf
(gdb) target remote :1234
(gdb) break init
(gdb) continue
```

### Build Report

After building, see `build/report.txt` for detailed system information:
- Kernel object allocations
- Memory mappings
- Capabilities
- Invocation counts

## Future Work: micro-ROS Integration

This application provides the foundation for porting micro-ROS to seL4. The next steps would be:

1. **Build micro-ROS libraries for seL4**:
   - Port rcl/rclc to run on Microkit
   - Implement custom allocator
   - Create seL4-compatible transport

2. **Add Linux VM partition**:
   - Run micro-ROS agent in Linux VM
   - Set up shared memory transport between VM and native PD

3. **Implement custom DDS-XRCE transport**:
   - Use shared memory for zero-copy communication
   - Use Microkit notifications for signaling

## Troubleshooting

### "MICROKIT_SDK must be specified"

Run `source ./env.sh` before building.

### "microkit: command not found"

Ensure the Microkit SDK was downloaded correctly by `setup.sh`.

### "aarch64-none-elf-gcc: command not found"

Ensure the ARM toolchain was downloaded and PATH is set:
```bash
export PATH="/opt/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin:$PATH"
```

### QEMU shows no output

Make sure you're using the correct QEMU command with:
- `-machine virt,virtualization=on`
- `-cpu cortex-a53`
- `-device loader,file=...,addr=0x70000000,cpu-num=0`

## References

- [Microkit Documentation](https://github.com/seL4/microkit)
- [seL4 Reference Manual](https://sel4.systems/Info/Docs/seL4-ref-manual-latest.pdf)
- [micro-ROS Documentation](https://micro.ros.org/)
- [Original ping_pong example](../ping_pong/main.c)

## License

This project follows the same license as micro-ROS and seL4 Microkit.
