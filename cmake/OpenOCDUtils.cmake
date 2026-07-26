# Shared helper for declaring one OBJECT library per Makefile.am source
# grouping. Every call registers itself in the OPENOCD_OBJECT_LIBS global
# property, which src/CMakeLists.txt reads in Phase 5 to build the final
# `openocd` aggregate via $<TARGET_OBJECTS:...> without every subdirectory
# having to manually propagate a list up through parent scopes.
function(openocd_add_object_library name)
    add_library(${name} OBJECT ${ARGN})
    target_link_libraries(${name} PUBLIC openocd_config)
    set_property(GLOBAL APPEND PROPERTY OPENOCD_OBJECT_LIBS ${name})
endfunction()
