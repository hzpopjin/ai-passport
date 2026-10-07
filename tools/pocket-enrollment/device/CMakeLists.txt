cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED POCKET_BOARD_REPO OR NOT EXISTS "${POCKET_BOARD_REPO}/components/bsp/src/bsp_button.c")
    message(FATAL_ERROR "Pass -DPOCKET_BOARD_REPO=<verified ai-passport checkout>")
endif()
set(EXTRA_COMPONENT_DIRS
    "${POCKET_BOARD_REPO}/managed_components/espressif__button"
    "${POCKET_BOARD_REPO}/managed_components/espressif__cmake_utilities")
set(IDF_TARGET esp32c3)
set(COMPONENTS main espressif__cmake_utilities)
set(PROJECT_VER "enrollment-v1")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Pocket-Enrollment)
