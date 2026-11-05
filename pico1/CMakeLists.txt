cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico1_controller C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()



set(TINYUSB_DIR ${PICO_SDK_PATH}/lib/tinyusb)
add_subdirectory(${TINYUSB_DIR}/hw/mcu/raspberry_pi/Pico-PIO-USB pico_pio_usb_build)

add_executable(pico1_controller
  src/main.c
  src/tokenizer.c
  src/usb_kbd.c
  ${TINYUSB_DIR}/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
  ${TINYUSB_DIR}/src/portable/raspberrypi/pio_usb/dcd_pio_usb.c
)

target_include_directories(pico1_controller PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/include
)

pico_enable_stdio_usb(pico1_controller 0)
pico_enable_stdio_uart(pico1_controller 0)

target_link_libraries(pico1_controller
  pico_stdlib pico_multicore hardware_spi hardware_uart hardware_clocks
  tinyusb_host pico_pio_usb)

pico_add_extra_outputs(pico1_controller)

