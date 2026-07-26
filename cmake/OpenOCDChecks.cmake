# Header/function/type probes feeding config.h.in, replacing autoheader's
# output. Every HAVE_* here is tested with #ifdef in the sources (verified
# by grep before writing this file), so plain #cmakedefine is correct -
# unlike the BUILD_* driver macros and HAVE_CAPSTONE, which are tested with
# #if and need #cmakedefine01 (see config.h.in).

include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckTypeSize)
include(CheckCSourceCompiles)
include(TestBigEndian)

set(IS_WIN32   0)
set(IS_MINGW   0)
set(IS_CYGWIN  0)
set(IS_DARWIN  0)
if(WIN32)
    set(IS_WIN32 1)
endif()
if(MINGW)
    set(IS_MINGW 1)
endif()
if(CYGWIN)
    set(IS_CYGWIN 1)
endif()
if(APPLE)
    set(IS_DARWIN 1)
endif()

# --- Headers (configure.ac AC_CHECK_HEADERS) ---
foreach(_hdr
    arpa/inet.h dlfcn.h elf.h fcntl.h inttypes.h malloc.h netdb.h
    netinet/in.h netinet/tcp.h poll.h stdbool.h stdint.h stdio.h stdlib.h
    strings.h string.h sys/ioctl.h sys/io.h sys/param.h sys/select.h
    sys/socket.h sys/stat.h sys/sysctl.h sys/time.h sys/types.h unistd.h
)
    string(TOUPPER "${_hdr}" _hdr_upper)
    string(REGEX REPLACE "[/.]" "_" _hdr_var "${_hdr_upper}")
    check_include_file("${_hdr}" HAVE_${_hdr_var})
endforeach()

# --- Functions (configure.ac AC_CHECK_FUNCS) ---
foreach(_fn realpath strndup strnlen usleep gettimeofday)
    string(TOUPPER "${_fn}" _fn_upper)
    check_function_exists("${_fn}" HAVE_${_fn_upper})
endforeach()

# --- Types ---
# AC_TYPE_LONG_LONG_INT / AC_TYPE_UNSIGNED_LONG_LONG_INT: both are part of
# C99/C11 and universally available on the clang/gcc/mingw toolchains this
# project targets.
set(HAVE_LONG_LONG_INT 1)
set(HAVE_UNSIGNED_LONG_LONG_INT 1)
set(HAVE__BOOL 1)
set(STDC_HEADERS 1)

# HAVE_ELF64: does the system's elf.h define Elf64_Ehdr.
check_c_source_compiles("
    #include <elf.h>
    int main(void) { Elf64_Ehdr e; (void)e; return 0; }
" HAVE_ELF64)

# NEED_ENVIRON_EXTERN: only needed if `environ` isn't visible via headers.
check_symbol_exists(environ "unistd.h" _openocd_environ_declared)
if(_openocd_environ_declared)
    set(NEED_ENVIRON_EXTERN 0)
else()
    set(NEED_ENVIRON_EXTERN 1)
endif()

test_big_endian(OPENOCD_WORDS_BIGENDIAN)
set(WORDS_BIGENDIAN ${OPENOCD_WORDS_BIGENDIAN})

# --- Parallel port backend (#if-tested; PARPORT itself defaults OFF, see
# OpenOCDOptions.cmake, so exactness beyond "does it compile" is low-stakes) ---
set(PARPORT_USE_PPDEV 0)
set(PARPORT_USE_GIVEIO 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PARPORT_USE_PPDEV 1)
elseif(WIN32)
    set(PARPORT_USE_GIVEIO 1)
endif()
