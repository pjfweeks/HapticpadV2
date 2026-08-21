# External dependencies, pinned to exact revisions so a build is reproducible.
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

# --------------------------------------------------------------------------
# FatFs + microSD over SPI (elehobica/pico_fatfs)
#
# Its default SPI0 pin set (MISO 4, CS 5, SCK 2, MOSI 3) is exactly the
# HapticPad wiring, so the card runs on real hardware SPI with DMA rather than
# a PIO fallback, and every PIO state machine stays free for us.
# --------------------------------------------------------------------------
FetchContent_Declare(pico_fatfs
    GIT_REPOSITORY https://github.com/elehobica/pico_fatfs.git
    GIT_TAG        fatfs-R0.15-1.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(pico_fatfs)

# --------------------------------------------------------------------------
# SimpleFOC, compiled as a plain static library against the Pico SDK.
#
# We build only the files the haptic engine needs.  Everything Arduino-shaped
# (Commander, StepDirListener, current sensing, other MCU ports) is left out,
# which is what keeps the compatibility shim in compat/ down to one header.
# --------------------------------------------------------------------------
FetchContent_Declare(simplefoc
    GIT_REPOSITORY https://github.com/simplefoc/Arduino-FOC.git
    GIT_TAG        v2.3.4
    GIT_SHALLOW    TRUE
)
# The repository is an Arduino library with no CMakeLists.txt of its own, so
# MakeAvailable just downloads it and leaves the build wiring to us.
FetchContent_MakeAvailable(simplefoc)

set(SFOC_SRC ${simplefoc_SOURCE_DIR}/src)

add_library(simplefoc STATIC
    ${SFOC_SRC}/BLDCMotor.cpp
    ${SFOC_SRC}/common/foc_utils.cpp
    ${SFOC_SRC}/common/time_utils.cpp
    ${SFOC_SRC}/common/lowpass_filter.cpp
    ${SFOC_SRC}/common/pid.cpp
    ${SFOC_SRC}/common/base_classes/FOCMotor.cpp
    ${SFOC_SRC}/common/base_classes/Sensor.cpp
    ${SFOC_SRC}/common/base_classes/CurrentSense.cpp
    ${SFOC_SRC}/drivers/BLDCDriver6PWM.cpp
    ${SFOC_SRC}/drivers/hardware_specific/rp2040/rp2040_mcu.cpp
)

target_include_directories(simplefoc PUBLIC
    ${CMAKE_SOURCE_DIR}/compat      # our Arduino.h must win over anything else
    ${SFOC_SRC}
)

target_compile_definitions(simplefoc PUBLIC
    TARGET_RP2040                   # selects the RP2040 hardware-specific driver
    SIMPLEFOC_DISABLE_DEBUG         # removes the String/F()/Serial dependency
)

target_link_libraries(simplefoc PUBLIC
    pico_stdlib
    hardware_pwm
    hardware_clocks
    hardware_gpio
)

# Upstream code, not ours: don't let its warnings drown out our own.
target_compile_options(simplefoc PRIVATE -w)
