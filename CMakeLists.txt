cmake_minimum_required(VERSION 3.20)
project(openocd LANGUAGES C)

if(MSVC)
    message(FATAL_ERROR
        "OpenOCD has no MSVC support"
        "Build with MinGW instead"
        )
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build Type" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Order matters
include(OpenOCDUtils)
include(OpenOCDOptions)
include(OpenOCDDependencies)
include(OpenOCDChecks)
include(OpenOCDVersion)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in"
    "${CMAKE_BINARY_DIR}/src/config.h"
    @ONLY
)

add_library(openocd_config INTERFACE)
target_include_directories(openocd_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_BINARY_DIR}/src"
)
target_compile_definitions(openocd_config INTERFACE
    "PKGDATADIR=\"${CMAKE_INSTALL_PREFIX}/share/openocd\""
    "BINDIR=\"${CMAKE_INSTALL_PREFIX}/bin\""
    HAVE_CONFIG_H
)
if(MINGW)
    target_compile_definitions(openocd_config INTERFACE __USE_MINGW_ANSI_STDIO)
endif()

include(jimtcl)
if(OPENOCD_ENABLE_JLINK AND OPENOCD_INTERNAL_LIBJAYLINK)
    include(libjaylink)
endif()

target_link_libraries(openocd_config INTERFACE jim)

add_subdirectory(src)
