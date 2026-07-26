add_subdirectory(helper)
add_subdirectory(jtag)
add_subdirectory(transport)
add_subdirectory(xsvf)
add_subdirectory(svf)
add_subdirectory(target)
add_subdirectory(rtos)
add_subdirectory(server)
add_subdirectory(flash)
add_subdirectory(pld)
add_subdirectory(rtt)

set(_startup_tcl_srcs
    "${CMAKE_CURRENT_SOURCE_DIR}/helper/startup.tcl"
    "${CMAKE_CURRENT_SOURCE_DIR}/jtag/startup.tcl"
    "${CMAKE_CURRENT_SOURCE_DIR}/target/startup.tcl"
    "${CMAKE_CURRENT_SOURCE_DIR}/server/startup.tcl"
    "${CMAKE_CURRENT_SOURCE_DIR}/flash/startup.tcl"
)
set(_startup_tcl_inc "${CMAKE_BINARY_DIR}/src/startup_tcl.inc")
add_custom_command(
    OUTPUT "${_startup_tcl_inc}"
    COMMAND ${CMAKE_COMMAND}
        "-DINPUTS=${_startup_tcl_srcs}"
        -DOUTPUT=${_startup_tcl_inc}
        -P "${CMAKE_SOURCE_DIR}/cmake/BinToChar.cmake"
    DEPENDS ${_startup_tcl_srcs} "${CMAKE_SOURCE_DIR}/cmake/BinToChar.cmake"
    COMMENT "Generating startup_tcl.inc"
    VERBATIM
)

openocd_add_object_library(openocd_core
    openocd.c
    hello.c
)
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/openocd.c"
    PROPERTIES OBJECT_DEPENDS "${_startup_tcl_inc}"
)
target_compile_definitions(openocd_core PRIVATE
    "RELSTR=\"${OPENOCD_RELSTR}\""
    "GITVERSION=\"${OPENOCD_GITVERSION}\""
    "PKGBLDDATE=\"${OPENOCD_PKGBLDDATE}\""
)

get_property(_openocd_object_libs GLOBAL PROPERTY OPENOCD_OBJECT_LIBS)
set(_openocd_object_srcs "")
foreach(_lib ${_openocd_object_libs})
    list(APPEND _openocd_object_srcs "$<TARGET_OBJECTS:${_lib}>")
endforeach()

add_library(openocd STATIC ${_openocd_object_srcs})
target_link_libraries(openocd PUBLIC openocd_config jim)
if(OPENOCD_HAVE_CAPSTONE)
    target_link_libraries(openocd PUBLIC OpenOCD::capstone)
endif()
if(OPENOCD_HAVE_LIBUSB1)
    target_link_libraries(openocd PUBLIC OpenOCD::libusb1)
endif()
if(OPENOCD_HAVE_HIDAPI)
    target_link_libraries(openocd PUBLIC OpenOCD::hidapi)
endif()
if(OPENOCD_HAVE_LIBFTDI)
    target_link_libraries(openocd PUBLIC OpenOCD::libftdi)
endif()
if(OPENOCD_HAVE_LIBGPIOD)
    target_link_libraries(openocd PUBLIC OpenOCD::libgpiod)
endif()
if(OPENOCD_ENABLE_JLINK)
    if(OPENOCD_INTERNAL_LIBJAYLINK)
        target_link_libraries(openocd PUBLIC jaylink)
    else()
        target_link_libraries(openocd PUBLIC OpenOCD::libjaylink_system)
    endif()
endif()
if(OPENOCD_ENABLE_ULINK OR OPENOCD_ENABLE_ANGIE)
    target_link_libraries(openocd PUBLIC m)
endif()
if(WIN32)
    target_link_libraries(openocd PUBLIC ws2_32)
endif()

add_executable(openocd-bin main.c)
set_target_properties(openocd-bin PROPERTIES OUTPUT_NAME openocd)
target_link_libraries(openocd-bin PRIVATE openocd)
