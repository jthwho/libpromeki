# CMake toolchain file for cross-compiling promeki to Linux / aarch64.
#
# Used by the cross-aarch64-linux preset (cmake/configs/cross-aarch64-linux.cmake)
# via -DCMAKE_TOOLCHAIN_FILE.  Also re-used by every ExternalProject_Add sub-
# build (the parent project forwards CMAKE_TOOLCHAIN_FILE through to them),
# so vendored thirdparty libraries pick up the same cross compiler.
#
# Targets the Debian/Ubuntu cross toolchain installed by:
#   sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu
#
# A toolchain file (versus setting CMAKE_SYSTEM_PROCESSOR / CMAKE_C_COMPILER
# directly in a config) is what makes CMake actually flip
# CMAKE_CROSSCOMPILING=TRUE, since both host and target are Linux and the
# OS-name comparison alone wouldn't catch this as a cross-build.
#
# Tunable inputs
# --------------
# All inputs below can be supplied via -D on the cmake command line, set
# in a PROMEKI_CONFIG_FILE (via promeki_config_path / promeki_config_string),
# or exported in the environment.  A toolchain file is loaded multiple
# times during a configure run (once for compiler probing, again after
# the cache loads), so each input is checked against all three sources
# and the resolved value is written back to the cache with FORCE — that
# way the same value is seen on every re-include and is inherited by
# every ExternalProject_Add sub-build without further plumbing.
#
#   PROMEKI_CROSS_TOOLCHAIN_PREFIX  Compiler binary prefix (default
#                                   "aarch64-linux-gnu-").  Yocto /
#                                   Buildroot / vendor SDKs typically
#                                   ship under a different name, e.g.
#                                   "/opt/yocto/sdk/.../aarch64-poky-linux-".
#                                   The toolchain appends "gcc" / "g++"
#                                   to this string.
#
#   PROMEKI_SYSROOT                 Absolute path to the target sysroot
#                                   (Yocto / Buildroot / vendor BSP).
#                                   When set, fed to the compiler as
#                                   --sysroot=<path>, prepended to
#                                   CMAKE_FIND_ROOT_PATH, and used to
#                                   wire pkg-config (see below).
#                                   Unset → falls back to Debian's
#                                   /usr/aarch64-linux-gnu multi-arch
#                                   staging area.
#
#   PROMEKI_STAGING_PREFIX          Absolute path used as
#                                   CMAKE_STAGING_PREFIX so that
#                                   `cmake --install --prefix /usr` lands
#                                   inside the staging area instead of
#                                   the host's /usr.  Typically the same
#                                   as PROMEKI_SYSROOT, but can point at
#                                   a separate output directory.
#
#   PROMEKI_TARGET_ARCH             -march=<value> (e.g. "armv8-a+crc",
#                                   "armv8.2-a").  Empty by default.
#
#   PROMEKI_TARGET_CPU              -mcpu=<value> (e.g. "cortex-a72",
#                                   "cortex-a78", "neoverse-n1").
#                                   Empty by default — generic aarch64.
#                                   Prefer this over -march on a known
#                                   SoC; gcc derives a matching -mtune
#                                   automatically.
#
#   PROMEKI_TARGET_TUNE             -mtune=<value>.  Empty by default;
#                                   only useful when overriding the
#                                   tune that -mcpu would imply.
#
# pkg-config (cross)
# ------------------
# When PROMEKI_SYSROOT is set, the toolchain also overrides three
# environment variables so pkg-config returns sysroot-relative paths:
#
#   PKG_CONFIG_LIBDIR        — list of .pc search directories *inside*
#                              the sysroot.  Setting LIBDIR (not PATH)
#                              wipes the host's default search list so
#                              host packages cannot leak into the result.
#   PKG_CONFIG_SYSROOT_DIR   — prefix prepended to every -I / -L pkg-
#                              config returns, so generated flags are
#                              valid for the cross compiler.
#   PKG_CONFIG_PATH          — cleared, for the same leak-prevention
#                              reason.
#
# Without this wiring pkg_check_modules() silently returns host paths
# and the build either picks up host libraries or fails with confusing
# missing-header errors deep in compile.  This is the #1 cross-compile
# gotcha — see the "Custom sysroot" section in docs/building.md.

set(CMAKE_SYSTEM_NAME      Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# ---------------------------------------------------------------------------
# Helper — resolve a tunable input from -D / cache / environment in that
# precedence order, validate it, and cache it with FORCE so re-includes
# of this toolchain file (and ExternalProject sub-builds) see the same
# value.
# ---------------------------------------------------------------------------
macro(_promeki_xc_resolve var doc)
    if(NOT DEFINED ${var} AND DEFINED ENV{${var}})
        set(${var} "$ENV{${var}}")
    endif()
    if(DEFINED ${var} AND NOT "${${var}}" STREQUAL "")
        set(${var} "${${var}}" CACHE STRING "${doc}" FORCE)
    endif()
endmacro()

_promeki_xc_resolve(PROMEKI_CROSS_TOOLCHAIN_PREFIX "Cross compiler binary prefix")
_promeki_xc_resolve(PROMEKI_SYSROOT                "Target sysroot for cross-compilation")
_promeki_xc_resolve(PROMEKI_STAGING_PREFIX         "Cross-install staging prefix (CMAKE_STAGING_PREFIX)")
_promeki_xc_resolve(PROMEKI_TARGET_ARCH            "Cross-target -march value")
_promeki_xc_resolve(PROMEKI_TARGET_CPU             "Cross-target -mcpu value")
_promeki_xc_resolve(PROMEKI_TARGET_TUNE            "Cross-target -mtune value")

# ---------------------------------------------------------------------------
# Compiler selection.  Default to Debian's aarch64-linux-gnu-{gcc,g++};
# override via PROMEKI_CROSS_TOOLCHAIN_PREFIX for Yocto / Buildroot /
# vendor SDKs.
# ---------------------------------------------------------------------------
if(NOT PROMEKI_CROSS_TOOLCHAIN_PREFIX)
    set(PROMEKI_CROSS_TOOLCHAIN_PREFIX "aarch64-linux-gnu-"
        CACHE STRING "Cross compiler binary prefix" FORCE)
endif()
set(CMAKE_C_COMPILER   "${PROMEKI_CROSS_TOOLCHAIN_PREFIX}gcc")
set(CMAKE_CXX_COMPILER "${PROMEKI_CROSS_TOOLCHAIN_PREFIX}g++")

# Useful auxiliary tools — set these so CMake doesn't fall back to the
# host's ar/ranlib/strip on the cross binaries.  CMake will quietly skip
# any that don't exist on disk; setting them explicitly avoids the
# "found host ar instead of target ar" silent-wrong-tool class of bug.
foreach(_tool AR;RANLIB;STRIP;OBJCOPY;OBJDUMP;NM;READELF;LD)
    string(TOLOWER "${_tool}" _toollc)
    set(_cand "${PROMEKI_CROSS_TOOLCHAIN_PREFIX}${_toollc}")
    if(NOT DEFINED CMAKE_${_tool})
        set(CMAKE_${_tool} "${_cand}")
    endif()
endforeach()
unset(_tool)
unset(_toollc)
unset(_cand)

# ---------------------------------------------------------------------------
# Sysroot wiring.  CMAKE_SYSROOT propagates as --sysroot=<path> to every
# compile / link invocation; CMAKE_FIND_ROOT_PATH gates where
# find_library / find_package look.
# ---------------------------------------------------------------------------
if(PROMEKI_SYSROOT)
    if(NOT IS_DIRECTORY "${PROMEKI_SYSROOT}")
        message(FATAL_ERROR
            "PROMEKI_SYSROOT='${PROMEKI_SYSROOT}' does not exist or is not a directory.")
    endif()
    set(CMAKE_SYSROOT "${PROMEKI_SYSROOT}")
    set(CMAKE_FIND_ROOT_PATH "${PROMEKI_SYSROOT}" /usr/aarch64-linux-gnu)
    message(STATUS "Cross-compile sysroot: ${PROMEKI_SYSROOT}")

    # pkg-config wiring — without this, FindPkgConfig.cmake leaks host
    # paths into the cross build.  We override the env vars (not cache)
    # so pkg-config subprocesses spawned by FindPkgConfig.cmake see them
    # while the rest of the system keeps its host defaults.
    set(_pc_libdirs
        "${PROMEKI_SYSROOT}/usr/lib/pkgconfig"
        "${PROMEKI_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig"
        "${PROMEKI_SYSROOT}/usr/share/pkgconfig")
    string(REPLACE ";" ":" _pc_libdirs "${_pc_libdirs}")
    set(ENV{PKG_CONFIG_LIBDIR}      "${_pc_libdirs}")
    set(ENV{PKG_CONFIG_SYSROOT_DIR} "${PROMEKI_SYSROOT}")
    set(ENV{PKG_CONFIG_PATH}        "")
    unset(_pc_libdirs)
else()
    # Where CMake looks for cross-compiled libraries / headers.  The
    # `aarch64-linux-gnu` triplet is the Debian multi-arch location.
    set(CMAKE_FIND_ROOT_PATH /usr/aarch64-linux-gnu)
endif()

# ---------------------------------------------------------------------------
# Staging prefix.  Without this, `cmake --install --prefix /usr` writes
# into the *host* /usr.  CMAKE_STAGING_PREFIX reroutes that to a path
# inside the staging area while still letting the build see the target's
# /usr layout via CMAKE_INSTALL_PREFIX.
# ---------------------------------------------------------------------------
if(PROMEKI_STAGING_PREFIX)
    if(NOT IS_ABSOLUTE "${PROMEKI_STAGING_PREFIX}")
        message(FATAL_ERROR
            "PROMEKI_STAGING_PREFIX='${PROMEKI_STAGING_PREFIX}' must be an absolute path.")
    endif()
    set(CMAKE_STAGING_PREFIX "${PROMEKI_STAGING_PREFIX}")
    message(STATUS "Cross-compile staging prefix: ${PROMEKI_STAGING_PREFIX}")
endif()

# ---------------------------------------------------------------------------
# CPU / ABI tuning.  Pushed via CMAKE_{C,CXX}_FLAGS_INIT — these are the
# toolchain-only knobs CMake consults *before* the cache exists, so they
# end up in CMAKE_{C,CXX}_FLAGS without overwriting later user additions.
# Generic aarch64 by default; opt in to -mcpu / -march / -mtune on a
# known SoC for a measurable codec / DSP speedup.
# ---------------------------------------------------------------------------
set(_xc_tune_flags "")
if(PROMEKI_TARGET_ARCH)
    string(APPEND _xc_tune_flags " -march=${PROMEKI_TARGET_ARCH}")
endif()
if(PROMEKI_TARGET_CPU)
    string(APPEND _xc_tune_flags " -mcpu=${PROMEKI_TARGET_CPU}")
endif()
if(PROMEKI_TARGET_TUNE)
    string(APPEND _xc_tune_flags " -mtune=${PROMEKI_TARGET_TUNE}")
endif()
if(NOT _xc_tune_flags STREQUAL "")
    string(STRIP "${_xc_tune_flags}" _xc_tune_flags)
    set(CMAKE_C_FLAGS_INIT   "${_xc_tune_flags}")
    set(CMAKE_CXX_FLAGS_INIT "${_xc_tune_flags}")
    message(STATUS "Cross-compile CPU/ABI tuning: ${_xc_tune_flags}")
endif()
unset(_xc_tune_flags)

# Standard cross-compile find behaviour: programs come from the host
# (so we can still invoke nasm / pkg-config / etc.), but libraries
# and headers come from the target sysroot.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
