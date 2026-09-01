cmake_minimum_required(VERSION 3.16)
project(dso5200basic CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
find_package(Threads REQUIRED)

add_executable(dso5200basic
    src/main.cpp
    src/cli/command_parser.cpp
    src/device/dso5200.cpp
    src/device/simulator.cpp
    src/streaming/sample_server.cpp
    src/usb/usbdevice.cpp
    src/usb/finddevice.cpp
    src/usb/uploadfirmware.cpp
    src/usb/ezusb.cpp
    src/protocol/bulkcommand.cpp
    src/protocol/bulkStructs.cpp
    src/protocol/controlcommand.cpp
    src/protocol/controlStructs.cpp
)

target_include_directories(dso5200basic PRIVATE src)
target_link_libraries(dso5200basic PRIVATE PkgConfig::LIBUSB Threads::Threads)

# Copy the firmware .hex files next to the built binary so the default
# --firmware-dir (argv[0]'s directory + "firmware") works out of the box.
add_custom_command(TARGET dso5200basic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/firmware
            $<TARGET_FILE_DIR:dso5200basic>/firmware
)
