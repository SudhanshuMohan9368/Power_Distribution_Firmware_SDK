cmake_minimum_required(VERSION 3.13)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH "/home/$ENV{USER}/pico/pico-sdk")
endif()

include(cmake/pico_sdk_import.cmake)

project(power_distribution_can C CXX ASM)

# Stock Raspberry Pi Pico (RP2040, 2MB W25Q080 flash) — same target the
# original PlatformIO firmware used (platformio.ini: board = rpipico).
set(PICO_BOARD pico)

pico_sdk_init()

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "-g -O2")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O2")
set(CMAKE_C_FLAGS_RELEASE "-g -O2")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O2")
add_compile_options(-Wall
        -Wno-format
        -Wno-unused-function
        -fasynchronous-unwind-tables)

include_directories(${CMAKE_BINARY_DIR}/generated)
include_directories(${CMAKE_SOURCE_DIR}/inc)

add_subdirectory(lib)
add_subdirectory(src)

if(EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
            COMMAND git rev-parse --abbrev-ref HEAD
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_BRANCH
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
            COMMAND git log -1 --format=%h
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT_HASH
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    set(GIT_BRANCH "")
    set(GIT_COMMIT_HASH "")
endif()

configure_file(
        ${CMAKE_SOURCE_DIR}/inc/version.h.in
        ${CMAKE_BINARY_DIR}/generated/version.h
)
