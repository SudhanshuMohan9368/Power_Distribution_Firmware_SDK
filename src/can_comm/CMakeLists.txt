project(can_comm)
add_library(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
        PRIVATE
        can_bus.cpp
        can_bus.h
        can_handler.cpp
        can_handler.h
        )

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        pico_stdlib
        hardware_pio
        hardware_irq
        hardware_dma
        hardware_sync
        hardware_clocks
        cmsis_core
        can
        neopixel
        )
