project(can)
add_library(${PROJECT_NAME})

pico_generate_pio_header(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/pio/can2040.pio)

target_sources(${PROJECT_NAME}
        PRIVATE
        can2040/can2040.c
        can2040/can2040.h
        )

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/can2040)

target_link_libraries(${PROJECT_NAME}
        PUBLIC
        pico_stdlib
        hardware_pio
        PRIVATE
        cmsis_core
        pico_multicore
        hardware_sync
        hardware_dma
        hardware_irq
        )