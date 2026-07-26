openocd_add_object_library(server
    server.c
    telnet_server.c
    gdb_server.c
    tcl_server.c
    rtt_server.c
    ipdbg.c
)
target_link_libraries(server PUBLIC jim)
