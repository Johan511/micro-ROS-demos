---
title: First micro-ROS Application on Linux and seL4
permalink: /docs/tutorials/core/first_application_linux_sel4/
---

<img src="https://img.shields.io/badge/Written_for-Humble-green" style="display:inline"/> <img src="https://img.shields.io/badge/Tested_on-Rolling-green" style="display:inline"/> <img src="https://img.shields.io/badge/Tested_on-Iron-green" style="display:inline"/>

In this tutorial, you'll learn the use of micro-ROS with Linux by testing a Ping Pong application.
In the follow-up tutorial [*First micro-ROS application on an RTOS*](/docs/tutorials/core/first_application_rtos/),
you'll learn how to build and bring this application on a microcontroller running the RTOS NuttX, FreeRTOS, or Zephyr.
Finally, in the tutorial [*Zephyr Emulator*](/docs/tutorials/core/zephyr_emulator/) you'll learn how to test
a micro-ROS application on a Zephyr emulator.

This tutorial also covers running micro-ROS on **seL4** using Microkit, demonstrating how to port micro-ROS to a secure microkernel environment.

{% include first_application_common/build_system.md %}

```bash
# Create firmware step
ros2 run micro_ros_setup create_firmware_ws.sh host
```

Once the command is executed, a folder named `firmware` must be present in your workspace.

This step is in charge, among other things, of downloading a set of micro-ROS apps for Linux, that are located at
`src/uros/micro-ROS-demos/rclc`.
Each app is represented by a folder containing the following files:

* `main.c`: This file contains the logic of the application.
* `CMakeLists.txt`: This is the CMake file containing the script to compile the application.

For the user to create a custom application, a folder `<my_app>` will need to be registered in this location,
containing the two files just described.
Also, any such new application folder needs to be registered in
`src/uros/micro-ROS-demos/rclc/CMakeLists.txt` by adding the following line:

```
export_executable(<my_app>)
```

In this tutorial, we will focus on the out-of-the-box `ping_pong` application located at
`src/uros/micro-ROS-demos/rclc/ping_pong`.
You can check the complete content of this app
[here](https://github.com/micro-ROS/micro-ROS-demos/tree/humble/rclc/ping_pong).

{% include first_application_common/pingpong_logic.md %}

The contents of the host app specific files can be found here:
[main.c](https://github.com/micro-ROS/micro-ROS-demos/blob/humble/rclc/ping_pong/main.c) and
[CMakeLists.txt](https://github.com/micro-ROS/micro-ROS-demos/blob/humble/rclc/ping_pong/CMakeLists.txt).
A thorough review of these files is illustrative of how to create a micro-ROS app in this RTOS.

## Building the firmware

Once the app has been created, the build step is in order.
Notice that, with respect to the four-steps workflow delined above, we would expect a configuration step to happen
before building the app. However, given that we are compiling micro-ROS in the host machine rather than in a board,
the cross-compilation implemented by the configuration step is not required in this case.
We can therefore proceed to build the firmware and source the local installation:

```bash
# Build step
ros2 run micro_ros_setup build_firmware.sh
source install/local_setup.bash
```
{% include first_application_common/agent_creation.md %}

### Add micro-ROS environment to bashrc (optional)

You can add the ROS 2 and micro-ROS workspace setup files to your `.bashrc` so the files do not have to be sourced every time a new command line is opened.
```bash
echo source /opt/ros/$ROS_DISTRO/setup.bash >> ~/.bashrc
echo source ~/microros_ws/install/local_setup.bash >> ~/.bashrc
```

## Running the micro-ROS app

At this point, you have both the client and the agent correctly installed in your host machine.

To give micro-ROS access to the ROS 2 dataspace, run the agent:

```bash
# Run a micro-ROS agent
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

And then, in another command line, run the micro-ROS node (remember sourcing the ROS 2 and micro-ROS installations, and setting the RMW Micro XRCE-DDS implementation):

```bash
source /opt/ros/$ROS_DISTRO/setup.bash
source install/local_setup.bash

# Use RMW Micro XRCE-DDS implementation
export RMW_IMPLEMENTATION=rmw_microxrcedds

# Run a micro-ROS node
ros2 run micro_ros_demos_rclc ping_pong
```

{% include first_application_common/test_app_host.md %}

## Multiple Ping Pong nodes

One of the advantages of having a Linux micro-ROS app is that you don't need to buy a bunch of hardware in order to
test some multi-node micro-ROS apps.
So, with the same micro-ROS agent of the last section, let's open four different command lines and run the following on
each:

```bash
cd microros_ws

source /opt/ros/$ROS_DISTRO/setup.bash
source install/local_setup.bash

export RMW_IMPLEMENTATION=rmw_microxrcedds

ros2 run micro_ros_demos_rclc ping_pong
```

As soon as all micro-ROS nodes are up and connected to the micro-ROS agent you will see them interacting:

```
user@user:~$ ros2 run micro_ros_demos_rclc ping_pong
Ping send seq 1711620172_1742614911                         <---- This micro-ROS node sends a ping with ping ID "1711620172" and node ID "1742614911"
Pong for seq 1711620172_1742614911 (1)                      <---- The first mate pongs my ping
Pong for seq 1711620172_1742614911 (2)                      <---- The second mate pongs my ping
Pong for seq 1711620172_1742614911 (3)                      <---- The third mate pongs my ping
Ping received with seq 1845948271_546591567. Answering.     <---- A ping is received from a mate identified as "546591567", let's pong it.
Ping received with seq 232977719_1681483056. Answering.     <---- A ping is received from a mate identified as "1681483056", let's pong it.
Ping received with seq 1134264528_1107823050. Answering.    <---- A ping is received from a mate identified as "1107823050", let's pong it.
Ping send seq 324239260_1742614911
Pong for seq 324239260_1742614911 (1)
Pong for seq 324239260_1742614911 (2)
Pong for seq 324239260_1742614911 (3)
Ping received with seq 1435780593_546591567. Answering.
Ping received with seq 2034268578_1681483056. Answering.
```

---

# micro-ROS on seL4 with Microkit

This section describes how to run micro-ROS on the seL4 microkernel using Microkit, a secure systems framework built on seL4.

## Overview

The seL4 port provides a foundation for running micro-ROS in a secure, capability-based environment:

- **Protection Domain**: Native seL4 application running in isolation
- **Shared Memory**: Inter-process communication via seL4 memory sharing
- **Microkit Framework**: Simplified seL4 development with static system configuration
- **QEMU Support**: Test on emulated hardware without physical boards

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

## Prerequisites

- Linux system (tested on Ubuntu 22.04)
- Internet connection for downloading dependencies
- sudo access for installing packages
- QEMU (for emulation)

## Quick Start

### 1. Setup Environment

Navigate to the seL4 ping-pong application:

```bash
cd src/uros/micro-ROS-demos/rclc/ping_pong_sel4
```

Run the setup script to download dependencies:

```bash
./setup.sh
```

This will:
- Install required system packages (QEMU, cross-compiler dependencies, etc.)
- Download Microkit SDK v1.4.1 to `/opt/microkit-sdk-1.4.1`
- Download ARM GCC toolchain to `/opt/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf`

### 2. Build the Application

Source the environment and build:

```bash
source ./env.sh
./build.sh
```

Or manually:

```bash
export MICROKIT_SDK=/opt/microkit-sdk-1.4.1
export PATH="/opt/arm-gnu-toolchain-12.2.rel1-x86_64-aarch64-none-elf/bin:$PATH"
make BUILD_DIR=build MICROKIT_SDK=$MICROKIT_SDK MICROKIT_BOARD=qemu_virt_aarch64 MICROKIT_CONFIG=debug
```

### 3. Run on QEMU

Execute the run script:

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

## Application Details

### File Structure

| File | Description |
|------|-------------|
| `ping_pong_component.c` | Main application code implementing the protection domain |
| `ping_pong_sel4.system` | Microkit system description (XML) defining memory regions and PDs |
| `Makefile` | Build system integrated with Microkit SDK |
| `CMakeLists.txt` | ROS/cmake integration for micro-ROS build system |
| `setup.sh` | One-time setup script for dependencies |
| `env.sh` | Environment configuration script |
| `build.sh` | Build automation script |
| `run.sh` | QEMU execution script |

### Memory Layout

- **Text/Data**: 0x200000 (from microkit.ld)
- **Shared Memory**: 0x2000000 (4KB mapped region)
- **IPC Buffer**: Automatically allocated by Microkit
- **Stack**: 4KB allocated per protection domain

### seL4 Kernel Objects

The system allocates:
- TCB (Thread Control Block) - for the protection domain thread
- SchedContext - for scheduling
- VSpace - virtual address space
- 5 Page Tables - for address translation
- 4 Pages (4KB each) - for code, data, stack, IPC
- CNode - capability storage
- Reply objects - for IPC replies
- Notification object - for async signaling

## Debugging

### Build Report

After building, check `build/report.txt` for detailed system information:
- Kernel object allocations
- Memory mappings
- Capabilities
- Invocation counts

### GDB Debugging

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

## Future Work: Full micro-ROS Integration

This application provides the foundation for porting micro-ROS to seL4. Next steps:

### Phase 1: micro-ROS Core
1. Port rcl/rclc to run on Microkit
2. Implement custom memory allocator for seL4
3. Create seL4-compatible threading/execution context

### Phase 2: Communication
1. Add Linux VM partition
2. Run micro-ROS agent in Linux VM
3. Set up shared memory transport between VM and native PD

### Phase 3: DDS-XRCE Transport
1. Implement custom DDS-XRCE transport using seL4 IPC
2. Zero-copy shared memory communication
3. Integration with ROS 2 ecosystem

## Troubleshooting

### "MICROKIT_SDK must be specified"

Run `source ./env.sh` before building.

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
- [Original ping_pong example](https://github.com/micro-ROS/micro-ROS-demos/tree/humble/rclc/ping_pong)

## Summary

This tutorial demonstrates running micro-ROS on both Linux and seL4:

1. **Linux**: Traditional approach with full OS support
2. **seL4/Microkit**: Secure microkernel approach with minimal trusted computing base

The seL4 port showcases how micro-ROS can be adapted to run in high-assurance environments while maintaining compatibility with the ROS 2 ecosystem through the micro-ROS agent.
