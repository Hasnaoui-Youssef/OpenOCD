#ifndef OPENOCD_EXIT_H
#define OPENOCD_EXIT_H

/* Force-included into every OpenOCD translation unit (see openocd_config in
 * the top-level CMakeLists.txt) so the exit( -> openocd_exit( sed rewrite
 * always has a declaration in scope. The provider defines this; it must
 * never return, matching every exit() call site it replaces. */
_Noreturn void openocd_exit(int code);

#endif /* OPENOCD_EXIT_H */
