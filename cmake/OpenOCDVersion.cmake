# Version string assembly, replacing the shell-backtick CPPFLAGS in
# src/Makefile.am:25-34 (RELSTR via guess-rev.sh, GITVERSION via a second,
# independent `git describe`, PKGBLDDATE via `date`).
#
# Deviation from Autotools: these are computed once, at CMake configure
# time, not re-evaluated on every build. Re-run `cmake` to refresh the
# version string after committing. This trade-off avoids patching
# src/openocd.c's OPENOCD_VERSION macro to call out to a script at runtime.

find_package(Git QUIET)

set(OPENOCD_PACKAGE_VERSION "0.12.0+dev")

set(_openocd_relstr "-snapshot")
set(_openocd_gitversion "")

if(Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --dirty
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _openocd_describe_dirty
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _openocd_describe_result
    )
    if(_openocd_describe_result EQUAL 0 AND _openocd_describe_dirty)
        set(_openocd_relstr "-${_openocd_describe_dirty}")
    endif()

    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE _openocd_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _openocd_describe_plain_result
    )
    if(_openocd_describe_plain_result EQUAL 0 AND _openocd_describe)
        set(_openocd_gitversion "${_openocd_describe}")
    endif()
endif()

set(OPENOCD_RELSTR "${_openocd_relstr}")
set(OPENOCD_GITVERSION "${_openocd_gitversion}")

string(TIMESTAMP OPENOCD_PKGBLDDATE "%Y-%m-%d-%H:%M")

message(STATUS "OpenOCD version: ${OPENOCD_PACKAGE_VERSION}${OPENOCD_RELSTR} (git: ${OPENOCD_GITVERSION}, built ${OPENOCD_PKGBLDDATE})")
