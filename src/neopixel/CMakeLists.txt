project(neopixel)
add_library(${PROJECT_NAME})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/ws2812.pio)

target_sources(${PROJECT_NAME}
        PRIVATE
        ws2812_driver.cpp
        ws2812_driver.h
        rgb_states.cpp
        rgb_states.h
        )

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        pico_stdlib
        hardware_pio
        hardware_clocks
        hardware_dma
        )
