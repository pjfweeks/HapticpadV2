# Locate the Pico SDK and pull in its CMake machinery.
# Honours, in order: PICO_SDK_PATH (cache), PICO_SDK_PATH (env), ~/pico/pico-sdk.

if (DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif ()
if (NOT PICO_SDK_PATH AND EXISTS "$ENV{HOME}/pico/pico-sdk")
    set(PICO_SDK_PATH "$ENV{HOME}/pico/pico-sdk")
endif ()
if (DEFINED ENV{PICO_TOOLCHAIN_PATH} AND (NOT PICO_TOOLCHAIN_PATH))
    set(PICO_TOOLCHAIN_PATH $ENV{PICO_TOOLCHAIN_PATH})
endif ()

if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR
        "Pico SDK not found.  Run tools/setup_macos.sh (macOS) or "
        "tools/setup_linux.sh (Linux), then "
        "'source ~/pico/pico-env.sh' before configuring.")
endif ()

get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
if (NOT EXISTS ${PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATH '${PICO_SDK_PATH}' does not exist")
endif ()

set(PICO_SDK_INIT_CMAKE_FILE ${PICO_SDK_PATH}/pico_sdk_init.cmake)
if (NOT EXISTS ${PICO_SDK_INIT_CMAKE_FILE})
    message(FATAL_ERROR "'${PICO_SDK_PATH}' is not a Pico SDK checkout")
endif ()

set(PICO_SDK_PATH ${PICO_SDK_PATH} CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
include(${PICO_SDK_INIT_CMAKE_FILE})
