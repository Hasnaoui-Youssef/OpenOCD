openocd_add_object_library(helper
    binarybuffer.c
    options.c
    time_support_common.c
    configuration.c
    log.c
    command.c
    crc32.c
    time_support.c
    replacements.c
    fileio.c
    util.c
    jep106.c
    jim-nvp.c
    nvp.c
)
target_link_libraries(helper PUBLIC jim)
