cmake_minimum_required(VERSION 3.13)
set(PICO_BOARD pico CACHE STRING "Board")
include(pico_sdk_import.cmake)
project(stepmother2 C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
add_executable(stepmother2
 src/persist.c src/save_flash.c src/telemetry.c src/profile.c
 src/main.c src/merged.c src/stepbro.cpp src/ui.c src/audio.c src/panel.c
 src/diag.c src/usb_descriptors.c voice/src/voice303.c voice/src/seq303.c)
target_include_directories(stepmother2 PRIVATE src voice/src)
target_compile_options(stepmother2 PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_compile_definitions(stepmother2 PRIVATE
 PICO_STACK_SIZE=0x1000 PICO_CORE1_STACK_SIZE=0x1000
 PICO_XOSC_STARTUP_DELAY_MULTIPLIER=64 SYS_CLK_MHZ=200 PICO_PANIC_FUNCTION=tb303_panic)
target_link_libraries(stepmother2 PRIVATE pico_stdlib pico_multicore pico_unique_id pico_bootrom
 hardware_watchdog hardware_adc hardware_i2c hardware_flash hardware_sync tinyusb_device)
pico_enable_stdio_usb(stepmother2 0)
pico_enable_stdio_uart(stepmother2 0)
pico_set_binary_type(stepmother2 copy_to_ram)
pico_add_bin_output(stepmother2)
pico_add_hex_output(stepmother2)

target_link_options(stepmother2 PRIVATE "-Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/stepmother2.map")

# Abort linking if the application would overlap the dedicated save banks.
target_link_options(stepmother2 PRIVATE "-Wl,-T,${CMAKE_CURRENT_LIST_DIR}/save_guard.ld")
