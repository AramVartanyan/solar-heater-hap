# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# HOMEKIT_PATH: esp-homekit-sdk root (components/, components/homekit/).
# project/common (git submodules: captive-wifi, outputwrite, esp_tm1637, button)
# is found next to this project folder.
if(DEFINED ENV{HOMEKIT_PATH})
  set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
  set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif(DEFINED ENV{HOMEKIT_PATH})

set(EXTRA_COMPONENT_DIRS
    ${HOMEKIT_PATH}/components
    ${HOMEKIT_PATH}/components/homekit
    ${CMAKE_CURRENT_LIST_DIR}/../common)

# Build only "main" and what it REQUIRES (transitively); other components in
# the common folders stay out of the image.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(solar-heater-hap)
