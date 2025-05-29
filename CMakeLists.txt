cmake_minimum_required(VERSION 3.12)

set(CMAKE_EXPORT_COMPILE_COMMANDS 1)
set(PICO_SDK_FETCH_FROM_GIT 1)

include(pico_sdk_import.cmake)

pico_sdk_init()

project(pico_tas_playback CXX C)
include_directories(include pico-joybus-comms/include)

# Create a target name
set(TARGET_NAME "pico_tas_playback")

# Add an executable target
add_executable(${TARGET_NAME}
src/main.cpp
src/RKGReader.cpp
src/file.S
)

# Generate the PIO header
pico_generate_pio_header(${TARGET_NAME} ${CMAKE_CURRENT_LIST_DIR}/pio/my_pio.pio)

# Add the custom command for generating the PIO header
add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/my_pio.pio.h
      DEPENDS ${CMAKE_CURRENT_LIST_DIR}/my_pio.pio
      COMMAND Pioasm ${CMAKE_CURRENT_LIST_DIR}/my_pio.pio ${CMAKE_CURRENT_LIST_DIR}/generated/my_pio.pio.h
)

# Link libraries
target_link_libraries(${TARGET_NAME} pico_stdlib hardware_pio pico_time pico_bootrom hardware_resets hardware_timer hardware_irq hardware_sync hardware_flash)

# Add extra outputs
pico_add_extra_outputs(${TARGET_NAME})
