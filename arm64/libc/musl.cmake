# CMake toolchain file for programs that are built against the musl of libc/Makefile (see musl-cc and the chapter "The C Library: musl on Ulix").
# The compiler wrapper musl-cc links statically with libc.a and, when MUSL_LDSCRIPT is set in the environment, with libc/musl.ld.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${CMAKE_CURRENT_LIST_DIR}/musl-cc)
find_program(CMAKE_AR aarch64-elf-ar REQUIRED)
find_program(CMAKE_RANLIB aarch64-elf-ranlib REQUIRED)
set(CMAKE_FIND_ROOT_PATH ${CMAKE_CURRENT_LIST_DIR}/build/sysroot)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
