# Adapter/driver options, transcribed from the `if XXX ... DRIVERFILES += ...`
# blocks in src/jtag/drivers/Makefile.am and the corresponding AC_ARG_ENABLE
# flags in configure.ac. One option() per BUILD_<NAME> macro in config.h.
#
# Defaults reflect the Cortex-M verification set only: CMSIS-DAP (HID+USB),
# ST-Link (HLA sources, required even though the verified runtime path is
# dapdirect/non-HLA), J-Link, FTDI, dummy and remote_bitbang. Every other
# adapter is transcribed and buildable but defaults OFF and is unverified.

include(CheckCCompilerFlag)

set(_openocd_host_is_linux FALSE)
set(_openocd_host_is_bsd FALSE)
set(_openocd_host_is_arm FALSE)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(_openocd_host_is_linux TRUE)
endif()
if(CMAKE_SYSTEM_NAME MATCHES "BSD")
    set(_openocd_host_is_bsd TRUE)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm|aarch64)")
    set(_openocd_host_is_arm TRUE)
endif()

# --- USB1/HIDAPI/LIBFTDI/LIBGPIOD/LIBJAYLINK-backed adapters, unrestricted ---
option(OPENOCD_ENABLE_AMTJTAGACCEL     "Amontec JTAG-Accelerator driver"          OFF)
option(OPENOCD_ENABLE_ANGIE            "ANGIE Adapter"                            OFF)
option(OPENOCD_ENABLE_ARMJTAGEW        "Olimex ARM-JTAG-EW Programmer"            OFF)
option(OPENOCD_ENABLE_CMSIS_DAP_HID    "CMSIS-DAP Compliant Debugger (HID)"       ON)
option(OPENOCD_ENABLE_CMSIS_DAP_USB    "CMSIS-DAP v2 Compliant Debugger (bulk USB)" ON)
option(OPENOCD_ENABLE_DUMMY            "Dummy driver (no hardware)"               ON)
option(OPENOCD_ENABLE_ESP_USB_JTAG     "Espressif JTAG Programmer"                OFF)
option(OPENOCD_ENABLE_FT232R           "Bitbang mode of FT232R based devices"     OFF)
option(OPENOCD_ENABLE_FTDI             "MPSSE mode of FTDI based devices"         ON)
option(OPENOCD_ENABLE_GW16012          "Gateworks GW16012 driver"                 OFF)
option(OPENOCD_ENABLE_HLADAPTER_ICDI   "TI ICDI JTAG Programmer"                  OFF)
option(OPENOCD_ENABLE_HLADAPTER_NULINK "Nu-Link Programmer"                       OFF)
option(OPENOCD_ENABLE_HLADAPTER_STLINK "ST-Link Programmer"                       ON)
option(OPENOCD_ENABLE_JLINK            "SEGGER J-Link Programmer"                 ON)
option(OPENOCD_ENABLE_JTAG_DPI         "JTAG DPI"                                 OFF)
option(OPENOCD_ENABLE_JTAG_VPI         "JTAG VPI"                                 OFF)
option(OPENOCD_ENABLE_KITPROG          "Cypress KitProg Programmer"               OFF)
option(OPENOCD_ENABLE_OPENDOUS         "eStick/opendous JTAG Programmer"          OFF)
option(OPENOCD_ENABLE_OPENJTAG         "OpenJTAG Adapter"                         OFF)
option(OPENOCD_ENABLE_OSBDM            "OSBDM (JTAG only) Programmer"             OFF)
option(OPENOCD_ENABLE_PRESTO           "ASIX Presto Adapter"                      OFF)
option(OPENOCD_ENABLE_REMOTE_BITBANG   "Remote Bitbang driver"                    ON)
option(OPENOCD_ENABLE_RLINK            "Raisonance RLink JTAG Programmer"         OFF)
option(OPENOCD_ENABLE_ULINK            "Keil ULINK JTAG Programmer"               OFF)
option(OPENOCD_ENABLE_USBPROG          "USBProg JTAG Programmer"                  OFF)
option(OPENOCD_ENABLE_USB_BLASTER      "Altera USB-Blaster Compatible"            OFF)
option(OPENOCD_ENABLE_USB_BLASTER_2    "Altera USB-Blaster II Compatible"         OFF)
option(OPENOCD_ENABLE_VDEBUG           "Cadence vdebug interface"                 OFF)
option(OPENOCD_ENABLE_VSLLINK          "Versaloon-Link JTAG Programmer"           OFF)
option(OPENOCD_ENABLE_XDS110           "TI XDS110 Debug Probe"                    OFF)

# --- Serial-port-backed ---
option(OPENOCD_ENABLE_BUSPIRATE        "Buspirate JTAG driver"                    OFF)
if(MINGW AND OPENOCD_ENABLE_BUSPIRATE)
    message(WARNING "buspirate is not supported on MinGW hosts; forcing OFF (configure.ac:418-436).")
    set(OPENOCD_ENABLE_BUSPIRATE OFF CACHE BOOL "" FORCE)
endif()

# --- Parallel-port-backed: available on Linux/Windows, not offered elsewhere ---
if(_openocd_host_is_linux OR WIN32)
    option(OPENOCD_ENABLE_PARPORT "Parallel port (ppdev/giveio)" OFF)
else()
    set(OPENOCD_ENABLE_PARPORT OFF)
endif()

# --- Linux-only ---
if(_openocd_host_is_linux)
    option(OPENOCD_ENABLE_DMEM          "Debug via Direct Mem"                    OFF)
    option(OPENOCD_ENABLE_LINUXGPIOD    "Linux GPIO bitbang through libgpiod"     OFF)
    option(OPENOCD_ENABLE_SYSFSGPIO     "SysfsGPIO driver"                        OFF)
    option(OPENOCD_ENABLE_XLNX_PCIE_XVC "Xilinx XVC/PCIe driver"                  OFF)
else()
    set(OPENOCD_ENABLE_DMEM OFF)
    set(OPENOCD_ENABLE_LINUXGPIOD OFF)
    set(OPENOCD_ENABLE_SYSFSGPIO OFF)
    set(OPENOCD_ENABLE_XLNX_PCIE_XVC OFF)
endif()

# --- Linux or FreeBSD ---
if(_openocd_host_is_linux OR _openocd_host_is_bsd)
    option(OPENOCD_ENABLE_RSHIM "Debug BlueField SoC via rshim" OFF)
else()
    set(OPENOCD_ENABLE_RSHIM OFF)
endif()

# --- ARM/AArch64 host only (bitbang GPIO on the SoC's own pins) ---
if(_openocd_host_is_arm)
    option(OPENOCD_ENABLE_AM335XGPIO  "Bitbanging on AM335x (Beaglebone)"        OFF)
    option(OPENOCD_ENABLE_BCM2835GPIO "Bitbanging on BCM2835 (Raspberry Pi)"     OFF)
    option(OPENOCD_ENABLE_EP93XX      "Bitbanging on ep93xx"                     OFF)
    option(OPENOCD_ENABLE_IMX_GPIO    "Bitbanging on NXP i.MX processors"        OFF)
    option(OPENOCD_ENABLE_AT91RM9200  "Bitbanging on at91rm9200"                 OFF)
else()
    set(OPENOCD_ENABLE_AM335XGPIO OFF)
    set(OPENOCD_ENABLE_BCM2835GPIO OFF)
    set(OPENOCD_ENABLE_EP93XX OFF)
    set(OPENOCD_ENABLE_IMX_GPIO OFF)
    set(OPENOCD_ENABLE_AT91RM9200 OFF)
endif()

# --- Derived (not user-facing options): umbrella / shared-core macros ---

# BUILD_HLADAPLER = OR(stlink, icdi, nulink) -- configure.ac:712-718.
if(OPENOCD_ENABLE_HLADAPTER_STLINK OR OPENOCD_ENABLE_HLADAPTER_ICDI OR OPENOCD_ENABLE_HLADAPTER_NULINK)
    set(OPENOCD_BUILD_HLADAPTER TRUE)
else()
    set(OPENOCD_BUILD_HLADAPTER FALSE)
endif()

# generic bitbang.c core is pulled in automatically by several drivers rather
# than being a user-facing option -- configure.ac:474-531,599-607,708-709.
if(OPENOCD_ENABLE_PARPORT OR OPENOCD_ENABLE_DUMMY OR OPENOCD_ENABLE_EP93XX OR
   OPENOCD_ENABLE_AT91RM9200 OR OPENOCD_ENABLE_BCM2835GPIO OR OPENOCD_ENABLE_IMX_GPIO OR
   OPENOCD_ENABLE_AM335XGPIO OR OPENOCD_ENABLE_REMOTE_BITBANG OR OPENOCD_ENABLE_SYSFSGPIO OR
   OPENOCD_ENABLE_LINUXGPIOD)
    set(OPENOCD_BUILD_BITBANG TRUE)
else()
    set(OPENOCD_BUILD_BITBANG FALSE)
endif()

# bitq.c core is likewise pulled in automatically -- configure.ac:734-742.
if(OPENOCD_ENABLE_PRESTO OR OPENOCD_ENABLE_ESP_USB_JTAG)
    set(OPENOCD_BUILD_BITQ TRUE)
else()
    set(OPENOCD_BUILD_BITQ FALSE)
endif()

# --- Misc non-adapter toggles (configure.ac AC_ARG_ENABLE, non-driver) ---
option(OPENOCD_VERBOSE_USB_IO          "Print verbose USB I/O messages"          OFF)
option(OPENOCD_VERBOSE_USB_COMMS       "Print verbose USB communication messages" OFF)
option(OPENOCD_MALLOC_LOGGING          "Include malloc free space in logging"    OFF)
option(OPENOCD_INTERNAL_LIBJAYLINK     "Build the vendored libjaylink submodule instead of using a system one" ON)

# config.h.in's macro names for these three don't match the option names
# (matching the reference config.h: _DEBUG_USB_IO_, _DEBUG_USB_COMMS_,
# _DEBUG_FREE_SPACE_ -- all #ifdef-tested).
set(_DEBUG_USB_IO_ ${OPENOCD_VERBOSE_USB_IO})
set(_DEBUG_USB_COMMS_ ${OPENOCD_VERBOSE_USB_COMMS})
set(_DEBUG_FREE_SPACE_ ${OPENOCD_MALLOC_LOGGING})

# usb_blaster driver is a special case: the DRIVERFILES it contributes are
# split between plain (USB_BLASTER, needs LIBFTDI) and v2 (USB_BLASTER_2,
# needs USB1) backends, both funneled through the usb_blaster/ subdirectory
# convenience library -- src/jtag/drivers/Makefile.am:88-91.
if(OPENOCD_ENABLE_USB_BLASTER OR OPENOCD_ENABLE_USB_BLASTER_2)
    set(OPENOCD_BUILD_USB_BLASTER_DRIVER TRUE)
else()
    set(OPENOCD_BUILD_USB_BLASTER_DRIVER FALSE)
endif()

# --- BUILD_<NAME> aliases for config.h.in -------------------------------
# config.h.in's #cmakedefine01 directives must name a CMake variable
# identical to the macro (e.g. `BUILD_FTDI`), not the namespaced
# OPENOCD_ENABLE_* option variable. Alias every one here, once, so
# config.h.in reads exactly like the reference config.h.
set(_openocd_build_macros
    AM335XGPIO AMTJTAGACCEL ANGIE ARMJTAGEW AT91RM9200 BCM2835GPIO BUSPIRATE
    CMSIS_DAP_HID CMSIS_DAP_USB DMEM DUMMY EP93XX ESP_USB_JTAG FT232R FTDI
    GW16012 HLADAPTER_ICDI HLADAPTER_NULINK HLADAPTER_STLINK IMX_GPIO JLINK
    JTAG_DPI JTAG_VPI KITPROG LINUXGPIOD OPENDOUS OPENJTAG OSBDM PARPORT
    PRESTO REMOTE_BITBANG RLINK RSHIM SYSFSGPIO ULINK USBPROG USB_BLASTER
    USB_BLASTER_2 VDEBUG VSLLINK XDS110 XLNX_PCIE_XVC
)
foreach(_macro ${_openocd_build_macros})
    set(BUILD_${_macro} ${OPENOCD_ENABLE_${_macro}})
endforeach()
set(BUILD_HLADAPTER ${OPENOCD_BUILD_HLADAPTER})
# HAVE_CAPSTONE alias is set in OpenOCDDependencies.cmake, which runs after
# this file and is where OPENOCD_HAVE_CAPSTONE is actually computed.
