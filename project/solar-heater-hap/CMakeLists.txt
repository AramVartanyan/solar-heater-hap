# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# HOMEKIT_PATH: esp-homekit-sdk root (components/, components/homekit/).
# Default: this repository, whose components/ holds button (own fixed version that
# replaces the one of esp-homekit-sdk, git submodule) and homekit (the SDK).
# project/common (git submodules: captive-wifi, outputwrite, esp_tm1637,
# container_nvs, general-ota) is found next to this project folder.
get_filename_component(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../components ABSOLUTE)
if(DEFINED ENV{HOMEKIT_PATH})
  set(HOMEKIT_PATH $ENV{HOMEKIT_PATH})
else()
  set(HOMEKIT_PATH ${CMAKE_CURRENT_LIST_DIR}/../..)
endif(DEFINED ENV{HOMEKIT_PATH})
get_filename_component(HOMEKIT_COMPONENTS ${HOMEKIT_PATH}/components ABSOLUTE)

set(EXTRA_COMPONENT_DIRS
    ${HOMEKIT_COMPONENTS}
    ${HOMEKIT_COMPONENTS}/homekit
    ${CMAKE_CURRENT_LIST_DIR}/../common)

# With an external SDK, this repository's components/ comes last so that its
# button takes precedence over the SDK's own button component.
if(NOT HOMEKIT_COMPONENTS STREQUAL REPO_COMPONENTS)
  list(APPEND EXTRA_COMPONENT_DIRS ${REPO_COMPONENTS})
endif()

# Build only "main" and what it REQUIRES (transitively); other components in
# the common folders stay out of the image.
set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(solar-heater-hap)
