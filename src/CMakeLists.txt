add_executable(${PROJECT_NAME} main.cpp)

include_directories(${CMAKE_SOURCE_DIR}/inc)

add_subdirectory(neopixel)
add_subdirectory(can_comm)
add_subdirectory(serial_io)

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        pico_stdlib
        hardware_gpio
        hardware_adc
        hardware_watchdog
        hardware_pio
        hardware_irq
        hardware_dma
        hardware_sync
        hardware_clocks
        can_comm
        can
        neopixel
        serial_io
        )

pico_add_extra_outputs(${PROJECT_NAME})
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Bound how long a printf() over USB CDC can stall the main loop when no
# host is reading the port -- without this the SDK default lets stdio_usb
# block for up to ~1s per call, during which incoming CAN frames pile up
# in the (now filtered, still finite) RX ring and get dropped.
target_compile_definitions(${PROJECT_NAME} PRIVATE PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000)
