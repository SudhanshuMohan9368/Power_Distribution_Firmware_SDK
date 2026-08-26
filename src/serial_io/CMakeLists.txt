project(serial_io)
add_library(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
        PRIVATE
        serial_io.cpp
        serial_io.h
        )

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        pico_stdlib
        hardware_adc
        neopixel
        )
