# CMake toolchain for cross-building zero-touchd for an ARMv8-A (aarch64) device
# using a PLAIN cross toolchain + a target sysroot (the fallback path in
# build.sh; the recommended path is the Yocto SDK env, which sets all of this
# for you). Drive it via build.sh, or directly:
#
#   cmake -S . -B build-aarch64 \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-toolchain.cmake \
#     -DZT_TARGET_SYSROOT=/path/to/aarch64-sysroot \
#     -DZT_TOOLCHAIN_PREFIX=aarch64-linux-gnu- \
#     -DZT_BUILD_DAEMON=ON -DZT_INSTALL_SYSV=ON
#
# The sysroot must contain the device's ACE, protobuf, libevent, libevent_openssl,
# nghttp2, lua and openssl — i.e. the same set the daemon links (a Yocto image
# sysroot has them all).

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compiler prefix + sysroot come from -D on the command line (build.sh sets them).
if(NOT DEFINED ZT_TOOLCHAIN_PREFIX)
    set(ZT_TOOLCHAIN_PREFIX aarch64-linux-gnu-)
endif()
if(DEFINED ZT_TARGET_SYSROOT)
    set(CMAKE_SYSROOT     ${ZT_TARGET_SYSROOT})
    set(CMAKE_FIND_ROOT_PATH ${ZT_TARGET_SYSROOT})
endif()

set(CMAKE_C_COMPILER   ${ZT_TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${ZT_TOOLCHAIN_PREFIX}g++)

# aarch64 hard-float, Cortex-A53 (raspberrypi3-64). Tune conservatively so the
# binary runs on any ARMv8-A core, not only A53.
set(_zt_arch_flags "-march=armv8-a")
set(CMAKE_C_FLAGS_INIT   "${_zt_arch_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_zt_arch_flags}")

# Find programs on the host, but headers/libs/packages ONLY in the sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
