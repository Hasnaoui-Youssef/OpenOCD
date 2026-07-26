# Builds jimtcl (vendored submodule at ./jimtcl) directly with CMake,
# replacing its own Tcl-based `autosetup` build. Fixed configuration
# equivalent to OpenOCD's own jimtcl invocation:
#   --disable-install-jim --with-ext=json --minimal --disable-ssl
# (see AX_CONFIG_SUBDIR_OPTION in the fork's configure.ac). That flag set is
# reproduced here as literal choices (JIM_UTF8 off, JIM_REFERENCES/REGEXP/
# STATICLIB on, the 25-extension "minimal + json" list) rather than
# re-derived, since it doesn't vary by platform. What DOES vary by platform
# are the ~35 function/header/struct probes below, which are real checks so
# Windows (MinGW) gets correct results instead of copied Linux values.

set(JIMTCL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/jimtcl")
if(NOT EXISTS "${JIMTCL_DIR}/jim.c")
    message(FATAL_ERROR
        "jimtcl/ is empty. Run: git submodule update --init jimtcl")
endif()

include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckStructHasMember)
include(CheckTypeSize)
include(CheckCSourceCompiles)
include(CheckCCompilerFlag)

# --- Headers ---
foreach(_hdr
    arpa/inet.h crt_externs.h dirent.h dlfcn.h execinfo.h fcntl.h math.h
    netdb.h netinet/in.h pty.h stdlib.h sys/socket.h sys/stat.h
    sys/sysinfo.h sys/time.h sys/types.h sys/un.h time.h unistd.h util.h
)
    string(TOUPPER "${_hdr}" _hdr_upper)
    string(REGEX REPLACE "[/.]" "_" _hdr_var "${_hdr_upper}")
    check_include_file("${_hdr}" HAVE_${_hdr_var})
endforeach()

# --- Functions ---
foreach(_fn
    backtrace clock_gettime dup execvpe fork fseeko fsync ftello
    getaddrinfo geteuid gmtime inet_ntop isascii isatty link localtime
    lstat mkstemp opendir openpty pipe readlink realpath regcomp select
    shutdown sigaction sleep socket socketpair strptime symlink sysinfo
    syslog system ualarm umask usleep utimes vfork waitpid
)
    string(TOUPPER "${_fn}" _fn_upper)
    check_function_exists("${_fn}" HAVE_${_fn_upper})
endforeach()

# dlopen (POSIX) vs the Windows LoadLibrary-based compat path jim-load.c
# and jim-win32compat.c fall back to when dlopen itself isn't available.
check_function_exists(dlopen HAVE_DLOPEN)
if(NOT HAVE_DLOPEN AND WIN32)
    set(HAVE_DLOPEN_COMPAT 1)
endif()

# --- Declared symbols (AC_CHECK_DECLS equivalent) ---
check_symbol_exists(isinf "math.h" HAVE_DECL_ISINF)
check_symbol_exists(isnan "math.h" HAVE_DECL_ISNAN)
check_symbol_exists(S_IRWXG "sys/stat.h" HAVE_DECL_S_IRWXG)
check_symbol_exists(S_IRWXO "sys/stat.h" HAVE_DECL_S_IRWXO)
check_symbol_exists(S_IXUSR "sys/stat.h" HAVE_DECL_S_IXUSR)
check_symbol_exists(_NSGetEnviron "crt_externs.h" HAVE__NSGETENVIRON)
check_symbol_exists(sys_siglist "signal.h" HAVE_SYS_SIGLIST)
check_symbol_exists(sys_signame "signal.h" HAVE_SYS_SIGNAME)

# --- Struct members ---
check_struct_has_member("struct stat" st_mtim "sys/stat.h" HAVE_STRUCT_STAT_ST_MTIM)
check_struct_has_member("struct stat" st_mtimespec "sys/stat.h" HAVE_STRUCT_STAT_ST_MTIMESPEC)
check_struct_has_member("struct sysinfo" uptime "sys/sysinfo.h" HAVE_STRUCT_SYSINFO_UPTIME)
check_c_source_compiles("
    #include <sys/file.h>
    int main(void) { struct flock fl; (void)fl; return 0; }
" HAVE_STRUCT_FLOCK)

# --- Compiler feature checks ---
check_c_compiler_flag(-fno-asynchronous-unwind-tables HAVE_CFLAG_FNO_ASYNCHRONOUS_UNWIND_TABLES)
check_c_compiler_flag(-fno-unwind-tables HAVE_CFLAG_FNO_UNWIND_TABLES)
check_c_source_compiles("int f(int *restrict p) { return *p; } int main(void) { return 0; }" HAVE_RESTRICT)
set(HAVE_LONG_LONG 1)

# --- Sizes / large file support ---
check_type_size("int" JIM_SIZEOF_INT)
check_type_size("off_t" JIM_SIZEOF_OFF_T)
check_type_size("time_t" JIM_SIZEOF_TIME_T)
if(JIM_SIZEOF_OFF_T GREATER_EQUAL 8)
    set(HAVE_LFS 1)
endif()

# --- Platform strings (TCL_PLATFORM_*, auto.def:309-326) ---
if(WIN32)
    set(JIM_TCL_PLATFORM_OS "windows")
    set(JIM_TCL_PLATFORM_PATH_SEPARATOR ";")
    set(JIM_TCL_PLATFORM_PLATFORM "windows")
else()
    set(JIM_TCL_PLATFORM_OS "${CMAKE_SYSTEM_NAME}")
    string(TOLOWER "${JIM_TCL_PLATFORM_OS}" JIM_TCL_PLATFORM_OS)
    set(JIM_TCL_PLATFORM_PATH_SEPARATOR ":")
    set(JIM_TCL_PLATFORM_PLATFORM "unix")
endif()
set(JIM_TCL_LIBRARY "${CMAKE_INSTALL_PREFIX}/lib/jim")

# --- Fixed configuration (matches --disable-install-jim --with-ext=json
#     --minimal --disable-ssl, independent of platform) ---
set(JIM_VERSION 82)
set(JIM_UTF8 OFF)
set(JIM_INSTALL OFF)
set(JIM_RANDOMISE_HASH OFF)

set(_jimtcl_generated_dir "${CMAKE_BINARY_DIR}/jimtcl_generated")
file(MAKE_DIRECTORY "${_jimtcl_generated_dir}")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jim-config.h.in"
    "${_jimtcl_generated_dir}/jim-config.h"
    @ONLY
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/jimautoconf.h.in"
    "${_jimtcl_generated_dir}/jimautoconf.h"
    @ONLY
)

# --- Tcl-to-C codegen (script-mode, no tclsh needed - see JimMakeCExt.cmake) ---
set(_jim_tcl_ext_names glob jsonencode nshelper oo stdlib tclcompat tree)
set(_jim_generated_tcl_ext_srcs "")
foreach(_ext ${_jim_tcl_ext_names})
    set(_out "${_jimtcl_generated_dir}/_${_ext}.c")
    add_custom_command(
        OUTPUT "${_out}"
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE=${JIMTCL_DIR}/${_ext}.tcl
            -DOUTPUT=${_out}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/JimMakeCExt.cmake"
        DEPENDS "${JIMTCL_DIR}/${_ext}.tcl" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/JimMakeCExt.cmake"
        COMMENT "jimtcl: generating _${_ext}.c from ${_ext}.tcl"
        VERBATIM
    )
    list(APPEND _jim_generated_tcl_ext_srcs "${_out}")
endforeach()

# STATIC_EXTS order (auto.def:611-613): static-C list, then static-Tcl list.
# JimMakeLoadStaticExts.cmake re-sorts by load priority itself.
set(_jim_static_exts
    aio array clock eventloop exec file history interp json load namespace
    pack package posix readdir regexp signal syslog
    glob jsonencode nshelper oo stdlib tclcompat tree
)
set(_jim_load_static_exts_c "${_jimtcl_generated_dir}/_load-static-exts.c")
add_custom_command(
    OUTPUT "${_jim_load_static_exts_c}"
    COMMAND ${CMAKE_COMMAND}
        "-DEXTS=${_jim_static_exts}"
        -DOUTPUT=${_jim_load_static_exts_c}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/JimMakeLoadStaticExts.cmake"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/JimMakeLoadStaticExts.cmake"
    COMMENT "jimtcl: generating _load-static-exts.c"
    VERBATIM
)

# --- jim static library ---
# Source list matches the reference build's actual OBJS (Makefile:44-45)
# for --minimal --with-ext=json: 8 core + jsmn + 18 static-C extensions +
# 7 static-Tcl extensions (generated) + _load-static-exts.c (generated).
# jimsh0/jimsh/initjimsh and _unicode_mapping.c are unreachable under
# --minimal (JIM_UTF8 off) and unneeded by the engine (Phase 2 note).
add_library(jim STATIC
    "${JIMTCL_DIR}/jim.c"
    "${JIMTCL_DIR}/jim-subcmd.c"
    "${JIMTCL_DIR}/jim-interactive.c"
    "${JIMTCL_DIR}/jim-format.c"
    "${JIMTCL_DIR}/utf8.c"
    "${JIMTCL_DIR}/jimregexp.c"
    "${JIMTCL_DIR}/jimiocompat.c"
    "${JIMTCL_DIR}/jsmn/jsmn.c"
    "${JIMTCL_DIR}/jim-aio.c"
    "${JIMTCL_DIR}/jim-array.c"
    "${JIMTCL_DIR}/jim-clock.c"
    "${JIMTCL_DIR}/jim-eventloop.c"
    "${JIMTCL_DIR}/jim-exec.c"
    "${JIMTCL_DIR}/jim-file.c"
    "${JIMTCL_DIR}/jim-history.c"
    "${JIMTCL_DIR}/jim-interp.c"
    "${JIMTCL_DIR}/jim-json.c"
    "${JIMTCL_DIR}/jim-load.c"
    "${JIMTCL_DIR}/jim-namespace.c"
    "${JIMTCL_DIR}/jim-pack.c"
    "${JIMTCL_DIR}/jim-package.c"
    "${JIMTCL_DIR}/jim-posix.c"
    "${JIMTCL_DIR}/jim-readdir.c"
    "${JIMTCL_DIR}/jim-regexp.c"
    "${JIMTCL_DIR}/jim-signal.c"
    "${JIMTCL_DIR}/jim-syslog.c"
    ${_jim_generated_tcl_ext_srcs}
    "${_jim_load_static_exts_c}"
)

if(WIN32)
    # auto.def:540 -- Windows dynamic-module/console compat shim.
    target_sources(jim PRIVATE "${JIMTCL_DIR}/jim-win32compat.c")
endif()

target_include_directories(jim
    PUBLIC "${JIMTCL_DIR}"
    PUBLIC "${_jimtcl_generated_dir}"
)
target_compile_definitions(jim PRIVATE JIM_STATICLIB)
if(WIN32)
    target_link_libraries(jim PUBLIC ws2_32)
endif()
