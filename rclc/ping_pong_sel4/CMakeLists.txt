cmake_minimum_required(VERSION 3.5)

project(ping_pong_sel4 LANGUAGES C)

# Microkit ping-pong application for seL4
# This application has two partitions:
#   1. Linux VM - runs the micro-ROS agent
#   2. Native Microkit partition - runs the ping-pong micro-ROS client

find_package(ament_cmake REQUIRED)
find_package(rcl REQUIRED)
find_package(rclc REQUIRED)
find_package(std_msgs REQUIRED)
find_package(rmw_microxrcedds REQUIRED)

# Native Microkit ping-pong application
add_executable(${PROJECT_NAME}
    ping_pong_component.c
)

target_link_libraries(${PROJECT_NAME}
    microkit
)

ament_target_dependencies(${PROJECT_NAME}
    rcl
    rclc
    std_msgs
    builtin_interfaces
    rmw_microxrcedds
)

# Installation
install(TARGETS ${PROJECT_NAME}
    DESTINATION ${PROJECT_NAME}
)

# Install system description and build files
install(FILES
    system.c
    Makefile
    linux_vm.dts
    README.md
    DESTINATION ${PROJECT_NAME}
)
