openocd_add_object_library(flash
    common.c
)

add_subdirectory(nor)
add_subdirectory(nand)
