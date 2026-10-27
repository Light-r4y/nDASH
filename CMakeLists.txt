cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build only main and what it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

# Every task is a component in tasks/<name>/, shared code is in common/<name>/.
# The container directories pick up all components inside them.
set(EXTRA_COMPONENT_DIRS
    tasks
    common
)

set(SUPPORTED_TARGETS esp32s3)

# ---------------------------------------------------------------------------
# Build options (passed as `idf.py -D NAME=value build`; the value is kept in
# the build directory's CMake cache, so it only has to be given once per build
# directory and again whenever it should change).
# ---------------------------------------------------------------------------

# Target board. There is deliberately no default: the pinout differs per board
# and flashing a build made for the wrong one drives the wrong GPIOs.
set(NDASH_SUPPORTED_BOARDS JC8048W550C WAVESHARE_S3_LCD7 WAVESHARE_S3_LCD5 Sunton_S3_LCD7)
set(NDASH_BOARD "" CACHE STRING "Target board: one of ${NDASH_SUPPORTED_BOARDS}")
if(NOT NDASH_BOARD IN_LIST NDASH_SUPPORTED_BOARDS)
    string(REPLACE ";" "|" _boards "${NDASH_SUPPORTED_BOARDS}")
    message(FATAL_ERROR
        "NDASH_BOARD is '${NDASH_BOARD}' - choose the target board, e.g.:\n"
        "    idf.py -D NDASH_BOARD=WAVESHARE_S3_LCD5 build\n"
        "Supported: ${_boards}")
endif()

# Where the log goes: "usb" (the native USB-Serial-JTAG, the default) or "uart" (UART0, for
# boards with a UART-USB bridge). It is a config default, so it only takes effect when
# sdkconfig is generated: after changing it delete sdkconfig (or build in a fresh -B dir).
set(NDASH_CONSOLE "usb" CACHE STRING "Console: usb or uart")
set(NDASH_LOG_LEVEL "info" CACHE STRING "Log level on the console: info or warn (release builds)")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
if(NDASH_CONSOLE STREQUAL "uart")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.console_uart")
endif()
if(NDASH_LOG_LEVEL STREQUAL "warn")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.log_warn")
endif()

# FPS / frame-time overlay on top of the UI (tasks/ui/fps_overlay.c).
# On for ordinary development builds, tools/build_release.py turns it off.
set(NDASH_UI_SHOW_FPS 1 CACHE STRING "Show the FPS / render time overlay (1 or 0)")
set(NDASH_LAYOUT_BUILTIN 0 CACHE STRING "Ignore the saved layout and use the built-in one (1 or 0), for tests")
set(NDASH_DEMO 0 CACHE STRING "Feed the UI synthetic sweeping values instead of waiting for CAN (1 or 0)")

# The version shown on screen and returned by the firmware comes from
# version.txt next to this file (picked up by ESP-IDF).
project(nDash)

# The options above reach the code as compile definitions set per component
# (common/board and tasks/ui), not globally, so switching board or toggling the
# overlay recompiles those components and relinks instead of rebuilding ESP-IDF.
#
# LVGL is configured through Kconfig (see sdkconfig.defaults, CONFIG_LV_*), there
# is no lv_conf.h in the project.

# The web interface (www/) goes into the "www" SPIFFS partition (partitions.csv),
# flashed together with the firmware. The OTA page is built into the firmware, so it
# works even with an empty www partition.
# The text files of www/ are gzip-compressed for the partition (tools/pack_www.py): the pages load several
# times faster over the board's Wi-Fi. The packed copy is made at configure time and again whenever a
# file of www/ changes.
file(GLOB_RECURSE NDASH_WWW_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/www/*)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${NDASH_WWW_FILES})
set(NDASH_WWW_PACKED ${CMAKE_BINARY_DIR}/www_packed)
idf_build_get_property(NDASH_PYTHON PYTHON)
execute_process(COMMAND ${NDASH_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tools/pack_www.py ${CMAKE_CURRENT_SOURCE_DIR}/www ${NDASH_WWW_PACKED}
                RESULT_VARIABLE NDASH_PACK_RESULT)
if(NOT NDASH_PACK_RESULT EQUAL 0)
    message(FATAL_ERROR "tools/pack_www.py failed")
endif()
spiffs_create_partition_image(www ${NDASH_WWW_PACKED} FLASH_IN_PROJECT)
# Pictures (dial backgrounds): flashed with the firmware, so `idf.py flash` brings back the built-in ones.
spiffs_create_partition_image(media ./media FLASH_IN_PROJECT)
