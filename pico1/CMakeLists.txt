cmake_minimum_required(VERSION 3.13)

# CMake 4.x에서 오래된 최소 버전을 선언한 서브프로젝트(Pico-PIO-USB 등)가 에러 나는 것 방지
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(PICO_BOARD pico CACHE STRING "")

# 환경변수 PICO_SDK_PATH 또는 -DPICO_SDK_PATH=... 둘 다 허용
if (NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
  file(TO_CMAKE_PATH "$ENV{PICO_SDK_PATH}" PICO_SDK_PATH)
endif()
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

set(TINYUSB_DIR "${PICO_SDK_PATH}/lib/tinyusb")
if (NOT PICO_PIO_USB_PATH)
  set(PICO_PIO_USB_PATH "${TINYUSB_DIR}/hw/mcu/raspberry_pi/Pico-PIO-USB")
endif()
if (NOT EXISTS "${PICO_PIO_USB_PATH}/src/pio_usb.c")
  message(FATAL_ERROR "Pico-PIO-USB was not found. Set PICO_PIO_USB_PATH to its source directory.")
endif()

project(pico1_controller C CXX ASM)
pico_sdk_init()

add_executable(pico1_controller
  src/main.c
  src/tokenizer.c
  src/usb_kbd.c
  src/vocab_data.c
)

target_include_directories(pico1_controller PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/src
  ${CMAKE_CURRENT_LIST_DIR}/include
)

pico_enable_stdio_usb(pico1_controller 0)
pico_enable_stdio_uart(pico1_controller 0)

target_link_libraries(pico1_controller
  pico_stdlib pico_multicore hardware_spi hardware_uart hardware_clocks
  tinyusb_host tinyusb_pico_pio_usb)

pico_add_extra_outputs(pico1_controller)