cmake_minimum_required(VERSION 3.13)

set(PICO_BOARD pico CACHE STRING "Target board")
include(pico_sdk_import.cmake)

project(hapticpad C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

pico_sdk_init()

include(cmake/dependencies.cmake)

add_executable(hapticpad
    src/main.c
    src/log.c
    src/ipc.c
    src/input.c
    src/led.c
    src/ssd1309.c
    src/gfx.c
    src/ui.c
    src/storage.c
    src/ini.c
    src/settings.c
    src/profile.c
    src/keycodes.c
    src/macro.c
    src/usb_descriptors.c
    src/hid.c
    src/cdc_protocol.c
    src/encoder.c
    src/wheel.c
    src/haptics.cpp
)

target_include_directories(hapticpad PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/src
    ${CMAKE_CURRENT_LIST_DIR}   # for tusb_config.h
)

# PIO programs are assembled at build time into generated headers.
pico_generate_pio_header(hapticpad ${CMAKE_CURRENT_LIST_DIR}/pio/ws2812.pio)
pico_generate_pio_header(hapticpad ${CMAKE_CURRENT_LIST_DIR}/pio/disp_spi.pio)
pico_generate_pio_header(hapticpad ${CMAKE_CURRENT_LIST_DIR}/pio/quadrature.pio)

target_link_libraries(hapticpad
    pico_stdlib
    pico_multicore
    pico_unique_id
    hardware_pio
    hardware_dma
    hardware_spi
    hardware_pwm
    hardware_clocks
    hardware_flash
    hardware_watchdog
    hardware_sync
    tinyusb_device
    tinyusb_board
    pico_fatfs
    simplefoc
)

# We drive USB ourselves (HID + CDC), so the SDK must not also claim stdio
# over USB.  Serial logging goes out on our own CDC interface instead.
pico_enable_stdio_usb(hapticpad 0)
pico_enable_stdio_uart(hapticpad 0)

target_compile_options(hapticpad PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    $<$<CONFIG:Debug>:-Og -g3>
)

# Build type default: Debug keeps symbols and assertions for SWD sessions.
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif ()

pico_add_extra_outputs(hapticpad)   # .uf2, .hex, .bin, .dis, .elf.map
