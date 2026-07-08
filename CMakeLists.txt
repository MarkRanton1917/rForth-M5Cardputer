cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(
  CC_DEBUG=0
  CASE_SENSITIVE=1
  PAD_SIZE=256
  HEAP_SIZE=65536
  USE_FLOAT=1
  ARDUINO_USB_MODE=1
  ARDUINO_USB_CDC_ON_BOOT=1
)
project(rForth)
set(ESP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/components/arduino" CACHE PATH "Path to Arduino component")