# Builds libjaylink (vendored submodule at ./src/jtag/drivers/libjaylink)
# directly with CMake. Unlike jimtcl, libjaylink needs no build-time codegen
# at all: it's 22 unconditional .c files (+2 more when libusb is available),
# a config.h with exactly two load-bearing macros, and a version.h whose
# values are static constants for the pinned submodule tag (0.3.1).

set(LIBJAYLINK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src/jtag/drivers/libjaylink")
set(LIBJAYLINK_DIR "${LIBJAYLINK_ROOT}/libjaylink")
if(NOT EXISTS "${LIBJAYLINK_DIR}/core.c")
    message(FATAL_ERROR
        "src/jtag/drivers/libjaylink/ is empty. Run: "
        "git submodule update --init src/jtag/drivers/libjaylink")
endif()

set(_jaylink_generated_dir "${CMAKE_BINARY_DIR}/libjaylink_generated")
file(MAKE_DIRECTORY "${_jaylink_generated_dir}")

set(HAVE_LIBUSB ${OPENOCD_HAVE_LIBUSB1})
include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libjaylink_config.h.in"
    "${_jaylink_generated_dir}/config.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libjaylink_version.h.in"
    "${_jaylink_generated_dir}/version.h"
    @ONLY
)

add_library(jaylink STATIC
    "${LIBJAYLINK_DIR}/buffer.c"
    "${LIBJAYLINK_DIR}/core.c"
    "${LIBJAYLINK_DIR}/c2.c"
    "${LIBJAYLINK_DIR}/device.c"
    "${LIBJAYLINK_DIR}/discovery.c"
    "${LIBJAYLINK_DIR}/discovery_tcp.c"
    "${LIBJAYLINK_DIR}/emucom.c"
    "${LIBJAYLINK_DIR}/error.c"
    "${LIBJAYLINK_DIR}/fileio.c"
    "${LIBJAYLINK_DIR}/jtag.c"
    "${LIBJAYLINK_DIR}/list.c"
    "${LIBJAYLINK_DIR}/log.c"
    "${LIBJAYLINK_DIR}/socket.c"
    "${LIBJAYLINK_DIR}/spi.c"
    "${LIBJAYLINK_DIR}/strutil.c"
    "${LIBJAYLINK_DIR}/swd.c"
    "${LIBJAYLINK_DIR}/swo.c"
    "${LIBJAYLINK_DIR}/target.c"
    "${LIBJAYLINK_DIR}/transport.c"
    "${LIBJAYLINK_DIR}/transport_tcp.c"
    "${LIBJAYLINK_DIR}/util.c"
    "${LIBJAYLINK_DIR}/version.c"
)

if(OPENOCD_HAVE_LIBUSB1)
    target_sources(jaylink PRIVATE
        "${LIBJAYLINK_DIR}/discovery_usb.c"
        "${LIBJAYLINK_DIR}/transport_usb.c"
    )
    target_link_libraries(jaylink PRIVATE OpenOCD::libusb1)
endif()

target_compile_definitions(jaylink PRIVATE HAVE_CONFIG_H)
target_include_directories(jaylink
    PUBLIC "${LIBJAYLINK_ROOT}"
    PRIVATE "${_jaylink_generated_dir}"
)
# version.h is a public header (libjaylink.h:683 #include "version.h") but
# lives only in the generated dir, so downstream consumers need it too.
target_include_directories(jaylink PUBLIC "${_jaylink_generated_dir}")

if(WIN32)
    target_link_libraries(jaylink PUBLIC ws2_32)
endif()

# libjaylink builds upstream with -Wall -Wextra -Werror -fvisibility=hidden
# (configure.ac:34); -Werror is intentionally not reproduced here (see
# plan deviations - it would make the build fragile across compiler
# versions we don't control upstream's warning set for).
