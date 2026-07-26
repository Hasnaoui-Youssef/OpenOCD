# External library detection, mirroring the PKG_CHECK_MODULES probes in
# configure.ac (:620-679). configure.ac uses pkg-config exclusively (zero
# AC_CHECK_LIB calls); we probe via pkg-config first and fall back to
# find_package config-mode packages (vcpkg-style) so Windows builds without
# pkg-config still work.
#
# Every probe here is non-fatal. A driver that needs a dependency that wasn't
# found is turned off (with a warning) in the drivers CMakeLists, not here.

find_package(PkgConfig QUIET)

# --- libusb-1.0 --------------------------------------------------------
set(OPENOCD_HAVE_LIBUSB1 FALSE)
if(PKG_FOUND OR PkgConfig_FOUND)
    pkg_check_modules(LIBUSB1 QUIET IMPORTED_TARGET libusb-1.0)
endif()
if(NOT LIBUSB1_FOUND)
    find_package(libusb-1.0 QUIET)
    if(libusb-1.0_FOUND OR LIBUSB_1_FOUND)
        set(LIBUSB1_FOUND TRUE)
    endif()
endif()
if(LIBUSB1_FOUND)
    set(OPENOCD_HAVE_LIBUSB1 TRUE)
    # HAVE_LIBUSB_GET_PORT_NUMBERS: libusb-1.0 >= 1.0.16 (configure.ac:626-630).
    if(LIBUSB1_VERSION)
        if(NOT LIBUSB1_VERSION VERSION_LESS "1.0.16")
            set(OPENOCD_HAVE_LIBUSB_GET_PORT_NUMBERS TRUE)
        endif()
    else()
        # Version unknown (found via find_package fallback): probe the symbol directly.
        include(CheckSymbolExists)
        set(CMAKE_REQUIRED_INCLUDES ${LIBUSB1_INCLUDE_DIRS})
        check_symbol_exists(libusb_get_port_numbers "libusb.h" OPENOCD_HAVE_LIBUSB_GET_PORT_NUMBERS)
        unset(CMAKE_REQUIRED_INCLUDES)
    endif()
endif()

# --- hidapi: try hidapi, then hidapi-hidraw, then hidapi-libusb --------
set(OPENOCD_HAVE_HIDAPI FALSE)
foreach(_hidapi_variant hidapi hidapi-hidraw hidapi-libusb)
    if(NOT OPENOCD_HAVE_HIDAPI)
        pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET ${_hidapi_variant})
        if(HIDAPI_FOUND)
            set(OPENOCD_HAVE_HIDAPI TRUE)
        endif()
    endif()
endforeach()
if(NOT OPENOCD_HAVE_HIDAPI)
    find_package(hidapi QUIET)
    if(hidapi_FOUND)
        set(OPENOCD_HAVE_HIDAPI TRUE)
    endif()
endif()

# --- libftdi: 1.x preferred, else legacy 0.x ---------------------------
set(OPENOCD_HAVE_LIBFTDI FALSE)
pkg_check_modules(LIBFTDI QUIET IMPORTED_TARGET libftdi1)
if(LIBFTDI_FOUND)
    set(OPENOCD_HAVE_LIBFTDI TRUE)
    if(NOT LIBFTDI_VERSION VERSION_LESS "1.5")
        set(OPENOCD_HAVE_LIBFTDI_TCIOFLUSH TRUE)
    endif()
else()
    pkg_check_modules(LIBFTDI QUIET IMPORTED_TARGET libftdi)
    if(LIBFTDI_FOUND)
        set(OPENOCD_HAVE_LIBFTDI TRUE)
    endif()
endif()

# --- libgpiod < 2.0 (Linux only) ---------------------------------------
set(OPENOCD_HAVE_LIBGPIOD FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    pkg_check_modules(LIBGPIOD QUIET IMPORTED_TARGET "libgpiod < 2.0")
    if(LIBGPIOD_FOUND)
        set(OPENOCD_HAVE_LIBGPIOD TRUE)
        if(NOT LIBGPIOD_VERSION VERSION_LESS "1.5")
            set(OPENOCD_HAVE_LIBGPIOD1_FLAGS_BIAS TRUE)
        endif()
    endif()
endif()

# --- libjaylink (>= 0.2), only relevant when JLINK is on but the internal
#     submodule build is not requested -------------------------------
set(OPENOCD_HAVE_LIBJAYLINK FALSE)
if(OPENOCD_ENABLE_JLINK AND NOT OPENOCD_INTERNAL_LIBJAYLINK)
    pkg_check_modules(LIBJAYLINK QUIET IMPORTED_TARGET "libjaylink >= 0.2")
    if(LIBJAYLINK_FOUND)
        set(OPENOCD_HAVE_LIBJAYLINK TRUE)
    endif()
endif()

# --- capstone (optional, disassembly) ----------------------------------
set(OPENOCD_HAVE_CAPSTONE FALSE)
pkg_check_modules(CAPSTONE QUIET IMPORTED_TARGET capstone)
if(NOT CAPSTONE_FOUND)
    find_package(capstone QUIET)
    if(capstone_FOUND)
        set(CAPSTONE_FOUND TRUE)
    endif()
endif()
if(CAPSTONE_FOUND)
    set(OPENOCD_HAVE_CAPSTONE TRUE)
endif()

# --- Normalized link targets ---
# pkg_check_modules(... IMPORTED_TARGET) creates PkgConfig::<PREFIX>, but
# the find_package() fallback (Windows without pkg-config) lands on
# whatever target name that particular package happens to export - there's
# no single standard name across vcpkg/system config packages. Wrap
# whichever path succeeded in one alias per dependency so every consumer in
# Phase 3/4 links against a stable name. The pkg-config side is exercised
# by this session's Linux build; the find_package side is best-effort and
# may need a per-package-manager tweak the first time a Windows build is
# actually attempted.
function(_openocd_make_alias alias_name prefix)
    if(TARGET PkgConfig::${prefix})
        add_library(${alias_name} INTERFACE IMPORTED)
        target_link_libraries(${alias_name} INTERFACE PkgConfig::${prefix})
    elseif(${prefix}_LIBRARIES OR ${prefix}_LINK_LIBRARIES)
        add_library(${alias_name} INTERFACE IMPORTED)
        target_include_directories(${alias_name} INTERFACE ${${prefix}_INCLUDE_DIRS})
        target_link_libraries(${alias_name} INTERFACE ${${prefix}_LIBRARIES} ${${prefix}_LINK_LIBRARIES})
    endif()
endfunction()

_openocd_make_alias(OpenOCD::libusb1 LIBUSB1)
_openocd_make_alias(OpenOCD::hidapi HIDAPI)
_openocd_make_alias(OpenOCD::libftdi LIBFTDI)
_openocd_make_alias(OpenOCD::libgpiod LIBGPIOD)
_openocd_make_alias(OpenOCD::libjaylink_system LIBJAYLINK)
_openocd_make_alias(OpenOCD::capstone CAPSTONE)

# --- Bare-name aliases for config.h.in ---
# config.h.in's #cmakedefine/#cmakedefine01 directives name the macro
# itself, not our namespaced OPENOCD_HAVE_* variables (same reasoning as the
# BUILD_* aliasing at the bottom of OpenOCDOptions.cmake).
set(HAVE_CAPSTONE ${OPENOCD_HAVE_CAPSTONE})                             # #if-tested
set(HAVE_LIBUSB1 ${OPENOCD_HAVE_LIBUSB1})                               # #ifdef-tested
set(HAVE_LIBUSB_GET_PORT_NUMBERS ${OPENOCD_HAVE_LIBUSB_GET_PORT_NUMBERS})
set(HAVE_LIBFTDI_TCIOFLUSH ${OPENOCD_HAVE_LIBFTDI_TCIOFLUSH})
set(HAVE_LIBGPIOD1_FLAGS_BIAS ${OPENOCD_HAVE_LIBGPIOD1_FLAGS_BIAS})

# --- Fatal only when an explicitly enabled driver's hard dependency is missing ---
function(_openocd_require_dep option_var dep_found_var dep_name)
    if(${option_var} AND NOT ${dep_found_var})
        message(FATAL_ERROR
            "${option_var} is ON but ${dep_name} was not found. "
            "Install ${dep_name} (or its pkg-config .pc file) or turn ${option_var} OFF.")
    endif()
endfunction()

_openocd_require_dep(OPENOCD_ENABLE_CMSIS_DAP_HID    OPENOCD_HAVE_HIDAPI  "hidapi")
_openocd_require_dep(OPENOCD_ENABLE_CMSIS_DAP_USB    OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_HLADAPTER_STLINK OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_HLADAPTER_ICDI   OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_HLADAPTER_NULINK OPENOCD_HAVE_HIDAPI  "hidapi")
_openocd_require_dep(OPENOCD_ENABLE_FTDI             OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_KITPROG          OPENOCD_HAVE_HIDAPI  "hidapi")
_openocd_require_dep(OPENOCD_ENABLE_KITPROG          OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_LINUXGPIOD       OPENOCD_HAVE_LIBGPIOD "libgpiod (<2.0)")
_openocd_require_dep(OPENOCD_ENABLE_USB_BLASTER      OPENOCD_HAVE_LIBFTDI "libftdi")
_openocd_require_dep(OPENOCD_ENABLE_OPENJTAG         OPENOCD_HAVE_LIBFTDI "libftdi")
_openocd_require_dep(OPENOCD_ENABLE_OPENJTAG         OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
_openocd_require_dep(OPENOCD_ENABLE_PRESTO           OPENOCD_HAVE_LIBFTDI "libftdi")
if(OPENOCD_ENABLE_JLINK AND NOT OPENOCD_INTERNAL_LIBJAYLINK)
    _openocd_require_dep(OPENOCD_ENABLE_JLINK OPENOCD_HAVE_LIBJAYLINK "libjaylink (>= 0.2)")
endif()
foreach(_usb1_driver ANGIE ARMJTAGEW ESP_USB_JTAG FT232R OPENDOUS RLINK ULINK USBPROG USB_BLASTER_2 VSLLINK XDS110 OSBDM)
    _openocd_require_dep(OPENOCD_ENABLE_${_usb1_driver} OPENOCD_HAVE_LIBUSB1 "libusb-1.0")
endforeach()
