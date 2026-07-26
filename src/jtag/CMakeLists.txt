openocd_add_object_library(jtag
    adapter.c
    commands.c
    core.c
    interface.c
    interfaces.c
    tcl.c
    swim.c
)
target_link_libraries(jtag PUBLIC jim)

if(OPENOCD_BUILD_HLADAPTER)
    add_subdirectory(hla)
    target_link_libraries(jtag PUBLIC jtag_hla)
endif()

add_subdirectory(drivers)
target_link_libraries(jtag PUBLIC jtag_drivers)
