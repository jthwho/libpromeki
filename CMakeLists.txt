cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(APP_NAME "promeki")
set(APP_VENDOR "JASONHOWARD")
set(APP_HUMAN_NAME "libpromeki")
set(APP_HUMAN_VENDOR "Jason Howard")

# The `VERSION` file in the repository root is the single source of truth for
# the project version. The first non-comment line must be a MAJOR.MINOR.PATCH
# semantic version (project(... VERSION ...) requires that form). Everything
# downstream — BuildInfo, the build identity string, shared library SOVERSION,
# and the Doxygen version field — derives from this value.
#
# The file name is intentionally upper-case: a lower-case `version` file at
# the repo root would shadow the C++20 standard header `<version>` on any
# build that has the project source root on its include path (mbedtls and
# a few other vendored deps add it).  `VERSION` is the standard Unix
# convention (Linux kernel, GCC, glibc) and avoids that collision.
set(_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
if(NOT EXISTS "${_VERSION_FILE}")
    message(FATAL_ERROR "VERSION file not found at ${_VERSION_FILE}")
endif()
# Re-run CMake configure when the VERSION file changes so a manual edit
# propagates without a fresh `cmake` invocation.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_VERSION_FILE}")
file(STRINGS "${_VERSION_FILE}" _VERSION_LINES)
set(_VERSION_RAW "")
foreach(_line IN LISTS _VERSION_LINES)
    string(STRIP "${_line}" _line)
    if(_line STREQUAL "" OR _line MATCHES "^#")
        continue()
    endif()
    set(_VERSION_RAW "${_line}")
    break()
endforeach()
# Accepted forms (semver pre-release subset):
#   MAJOR.MINOR.PATCH               → release (stage=Release, stageNum=0)
#   MAJOR.MINOR.PATCH-alpha[N]      → alpha   (N optional; missing means N=1)
#   MAJOR.MINOR.PATCH-beta[N]       → beta    (N optional; missing means N=1)
#   MAJOR.MINOR.PATCH-rcN           → rc      (N REQUIRED, N >= 1)
#
# When a stage number is present it must be 1 or larger with no leading
# zeros (e.g. -alpha0, -beta01, -rc0 all rejected).  Alpha and beta let
# you write `-alpha` / `-beta` as shorthand for the first iteration of
# that stage; the printed forms (in BuildInfo::ident and the {extra}
# token of formatBuildInfo) reciprocate by omitting the "1".  RC always
# shows its number — `-rc1`, `-rc2`, … — because release candidates
# almost always come in numbered series.
#
# APP_VERSION holds the bare MAJOR.MINOR.PATCH triple (so
# project(... VERSION ...) and BuildInfo::version stay clean);
# APP_VERSION_STAGE_NAME / APP_VERSION_STAGE_NUM carry the pre-release
# tag separately.  Anything that does not match one of the patterns
# above is a hard configure-time error.
if(_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    set(APP_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(APP_VERSION_STAGE_NAME "release")
    set(APP_VERSION_STAGE_NUM 0)
elseif(_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)-(alpha|beta)([0-9]*)$")
    # alpha / beta — number is optional; absent means stageNum = 1.
    set(APP_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(APP_VERSION_STAGE_NAME "${CMAKE_MATCH_4}")
    set(_stage_num_raw "${CMAKE_MATCH_5}")
    if(_stage_num_raw STREQUAL "")
        set(APP_VERSION_STAGE_NUM 1)
    elseif(_stage_num_raw MATCHES "^[1-9][0-9]*$")
        set(APP_VERSION_STAGE_NUM "${_stage_num_raw}")
    else()
        message(FATAL_ERROR
            "VERSION file ${_VERSION_FILE}: stage number must be 1 or larger "
            "with no leading zeros; got '-${APP_VERSION_STAGE_NAME}${_stage_num_raw}'")
    endif()
    unset(_stage_num_raw)
elseif(_VERSION_RAW MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)-rc([0-9]+)$")
    # rc — number REQUIRED.
    set(APP_VERSION "${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3}")
    set(APP_VERSION_STAGE_NAME "rc")
    set(APP_VERSION_STAGE_NUM "${CMAKE_MATCH_4}")
    if(NOT APP_VERSION_STAGE_NUM MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR
            "VERSION file ${_VERSION_FILE}: rc stage number must be 1 or larger "
            "with no leading zeros; got '-rc${APP_VERSION_STAGE_NUM}'")
    endif()
else()
    message(FATAL_ERROR
        "VERSION file ${_VERSION_FILE}: not a recognized version string. "
        "Expected one of:\n"
        "    MAJOR.MINOR.PATCH                  (release)\n"
        "    MAJOR.MINOR.PATCH-alpha[N]         (alpha; N>=1, default 1)\n"
        "    MAJOR.MINOR.PATCH-beta[N]          (beta;  N>=1, default 1)\n"
        "    MAJOR.MINOR.PATCH-rcN              (rc;    N>=1, required)\n"
        "Got '${_VERSION_RAW}'")
endif()
unset(_VERSION_FILE)
unset(_VERSION_LINES)
unset(_VERSION_RAW)

# CI build number (optional).  Read from a `BUILD` file at the repo root if
# present.  CI is expected to write a monotonically increasing integer to
# this file as part of its pipeline; local developer builds typically have
# no BUILD file and the build number defaults to 0.  The number is exposed
# on BuildInfo as the integer field `build` and folded into
# PROMEKI_BUILD_IDENT when non-zero — the human-readable `version` string
# stays MAJOR.MINOR.PATCH and is unaffected.
set(_BUILD_FILE "${CMAKE_CURRENT_SOURCE_DIR}/BUILD")
# Register the BUILD file as a configure dependency unconditionally — even
# when it does not currently exist — so that creating, modifying, or
# deleting the file triggers a CMake reconfigure on the next build.
# Registering only inside `if(EXISTS)` would miss the create-from-absent
# transition (CI's first run after a clean checkout).
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_BUILD_FILE}")
set(APP_VERSION_BUILD 0)
if(EXISTS "${_BUILD_FILE}")
    file(STRINGS "${_BUILD_FILE}" _BUILD_LINES)
    foreach(_line IN LISTS _BUILD_LINES)
        string(STRIP "${_line}" _line)
        if(_line STREQUAL "" OR _line MATCHES "^#")
            continue()
        endif()
        if(NOT _line MATCHES "^[0-9]+$")
            message(FATAL_ERROR
                "BUILD file ${_BUILD_FILE} must contain a non-negative integer "
                "on its first non-comment line; got '${_line}'")
        endif()
        set(APP_VERSION_BUILD "${_line}")
        break()
    endforeach()
    unset(_BUILD_LINES)
endif()
unset(_BUILD_FILE)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# DevRelease build type --------------------------------------------------------
# Release-level optimisations with developer features: line-table debug info
# for useful crash logs / core dumps (-g1, enough for symbol+file:line in
# backtraces without the full DWARF type tree), and promekiDebug() logging
# compiled in.  `-g1` instead of `-g` cuts peak compile RSS ~40% on this
# project's template-heavy TUs — switch to CMAKE_BUILD_TYPE=Debug for full
# DWARF when stepping through templates in gdb.
# Must be set before project() so CMake picks up the _INIT flags.
set(CMAKE_C_FLAGS_DEVRELEASE_INIT            "-O3 -g1 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEVRELEASE_INIT          "-O3 -g1 -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_DEVRELEASE_INIT   "")
set(CMAKE_SHARED_LINKER_FLAGS_DEVRELEASE_INIT "")

# Config file ##################################################################
#
# Users can preconfigure a build by passing -DPROMEKI_CONFIG_FILE=<spec>
# instead of listing every PROMEKI_* / CMAKE_* option individually on the
# command line.  <spec> can be any of:
#
#   - An absolute path:                /etc/promeki/site.cmake
#   - A path relative to the source:   etc/my-config.cmake
#   - A bare name resolved against     cmake/configs/<spec>.cmake
#     cmake/configs/ (the .cmake       (e.g. -DPROMEKI_CONFIG_FILE=minimal)
#     suffix is optional)
#
# The config is loaded BEFORE project(), so it can also set the standard
# CMake toolchain / build-type / compiler variables that must be in place
# for project()'s compiler detection:
#
#   CMAKE_BUILD_TYPE        Debug / Release / DevRelease / ...
#   CMAKE_TOOLCHAIN_FILE    path to a CMake toolchain file (cross-compile)
#   CMAKE_C_COMPILER        explicit C compiler  (e.g. aarch64-linux-gnu-gcc)
#   CMAKE_CXX_COMPILER      explicit C++ compiler
#   CMAKE_SYSTEM_NAME       target OS for cross builds (Linux / Windows / ...)
#   CMAKE_SYSTEM_PROCESSOR  target arch         (x86_64 / aarch64 / armv7l / ...)
#   PROMEKI_SANITIZER       e.g. address,undefined
#
# Several example configs ship under cmake/configs/ — see the README there
# for descriptions and the full list of variables a config can set.  Configs
# are plain CMake (use set(), if(), include(), etc.) and should set values
# through the promeki_config_* helper macros below, which use cache-FORCE
# semantics so editing the config and re-running cmake takes effect even
# when the cache already has a previous value.
#
# Precedence: values written by the config file override any -D values
# supplied alongside it on the same cmake invocation.  To override a
# config setting, edit the config (or copy it and point at the copy)
# rather than relying on command-line -D.
#
# Caveat: CMAKE_TOOLCHAIN_FILE and the compiler variables are read by
# project() exactly once, on the first configure of a given build
# directory.  Changing them in the config after the fact requires
# wiping the build directory — this is a CMake limitation, not specific
# to this loader.
# NOTE: declared as STRING (not FILEPATH) so a bare name like "minimal"
# survives untouched.  FILEPATH would silently resolve relative inputs
# against the binary dir, breaking the cmake/configs/<name> shorthand.
set(PROMEKI_CONFIG_FILE "" CACHE STRING
    "Path or name of a PROMEKI config file (see cmake/configs/)")

# Helper macros for config files.  Cache-FORCE is intentional: a config
# file is an authoritative preset, not a default to be silently shadowed
# by a stale cache entry from a previous run.
macro(promeki_config_option _name _value)
    set(${_name} ${_value} CACHE BOOL "Set via PROMEKI_CONFIG_FILE" FORCE)
endmacro()
macro(promeki_config_string _name _value)
    set(${_name} "${_value}" CACHE STRING "Set via PROMEKI_CONFIG_FILE" FORCE)
endmacro()
macro(promeki_config_path _name _value)
    set(${_name} "${_value}" CACHE PATH "Set via PROMEKI_CONFIG_FILE" FORCE)
endmacro()
macro(promeki_config_filepath _name _value)
    set(${_name} "${_value}" CACHE FILEPATH "Set via PROMEKI_CONFIG_FILE" FORCE)
endmacro()

if(PROMEKI_CONFIG_FILE)
    set(_promeki_config_candidates "")
    if(IS_ABSOLUTE "${PROMEKI_CONFIG_FILE}")
        list(APPEND _promeki_config_candidates "${PROMEKI_CONFIG_FILE}")
    else()
        list(APPEND _promeki_config_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/${PROMEKI_CONFIG_FILE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/configs/${PROMEKI_CONFIG_FILE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/configs/${PROMEKI_CONFIG_FILE}.cmake")
    endif()
    set(_promeki_config_resolved "")
    foreach(_cand IN LISTS _promeki_config_candidates)
        if(EXISTS "${_cand}" AND NOT IS_DIRECTORY "${_cand}")
            set(_promeki_config_resolved "${_cand}")
            break()
        endif()
    endforeach()
    if(NOT _promeki_config_resolved)
        string(REPLACE ";" "\n    " _promeki_config_tried "${_promeki_config_candidates}")
        message(FATAL_ERROR
            "PROMEKI_CONFIG_FILE='${PROMEKI_CONFIG_FILE}' not found.  Tried:\n"
            "    ${_promeki_config_tried}")
    endif()
    message(STATUS "Loading promeki config file: ${_promeki_config_resolved}")
    # Re-run CMake when the config file changes so a manual edit propagates
    # without a fresh `cmake` invocation.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_promeki_config_resolved}")
    include("${_promeki_config_resolved}")
    unset(_promeki_config_candidates)
    unset(_promeki_config_resolved)
    unset(_promeki_config_tried)
endif()

# Default to DevRelease when no build type is specified (single-config
# generators only).  This runs AFTER the config-file include so a config
# that sets CMAKE_BUILD_TYPE pre-empts the default cleanly.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE DevRelease CACHE STRING "Build type (Debug, Release, DevRelease, ...)" FORCE)
        message(STATUS "No CMAKE_BUILD_TYPE specified — defaulting to DevRelease")
endif()

project(${APP_NAME} VERSION ${APP_VERSION} LANGUAGES C CXX)

mark_as_advanced(
    CMAKE_C_FLAGS_DEVRELEASE
    CMAKE_CXX_FLAGS_DEVRELEASE
    CMAKE_EXE_LINKER_FLAGS_DEVRELEASE
    CMAKE_SHARED_LINKER_FLAGS_DEVRELEASE
)

# Docs-only mode ###############################################################
#
# PROMEKI_DOCS_ONLY=ON skips every library, executable, and third-party
# dependency and configures only the `docs` target.  This lets the GitHub
# Actions documentation workflow build docs from a checkout that did NOT
# pull any submodules — the only inputs Doxygen needs are the in-tree
# headers, the markdown under docs/, and the version metadata.  When ON
# we set up just enough state for the `docs` target and `return()` early,
# bypassing all of the dependency wiring (mbedtls, libsndfile, freetype,
# and friends) that would otherwise fail without their submodule sources.
option(PROMEKI_DOCS_ONLY
    "Configure only the documentation target; skip libraries and third-party deps"
    OFF)

if(PROMEKI_DOCS_ONLY)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiDocsTarget.cmake)
    promeki_setup_docs_target()
    return()
endif()

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ExternalProject)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiGplComponents.cmake)

# Warning interface library ####################################################
#
# `promeki::warnings` is the single attachment point for our compiler-warning
# policy.  Every target *we* build (libraries, demos, utilities, tests) links
# it PRIVATE; vendored third-party targets do not, so their builds stay quiet.
#
# `-Wall -Wextra` always; `-Werror` only when PROMEKI_WARNINGS_AS_ERRORS is on
# (off by default, flipped on by scripts/precommit.sh).  `-Wno-unused-parameter`
# is intentional — the codebase has many virtual-method overrides that ignore
# inherited parameters and we don't want to litter casts or `[[maybe_unused]]`
# everywhere.
add_library(promeki_warnings INTERFACE)
add_library(promeki::warnings ALIAS promeki_warnings)

# Sanitizer wiring: when PROMEKI_SANITIZER is non-empty, apply the flags
# globally so every target (library, executables, vendored deps) is
# instrumented and the linker pulls in the runtime once.  We disable
# PCH and ccache here too: PCH headers built without the sanitizer
# flag mismatch the consuming TUs and ccache's cache key doesn't
# include CFLAGS, so a hit would silently return uninstrumented code.
if(NOT "${PROMEKI_SANITIZER}" STREQUAL "")
    message(STATUS "Sanitizer enabled: -fsanitize=${PROMEKI_SANITIZER}")
    add_compile_options(-fsanitize=${PROMEKI_SANITIZER} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${PROMEKI_SANITIZER})
    set(PROMEKI_USE_PCH OFF CACHE BOOL "PCH disabled while sanitizer is on" FORCE)
    set(PROMEKI_USE_CCACHE OFF CACHE BOOL "ccache disabled while sanitizer is on" FORCE)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(promeki_warnings INTERFACE
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-unused-parameter>
        # -Wmaybe-uninitialized is a known-noisy GCC interprocedural pass
        # whose value-flow tracking through std::variant union storage and
        # heavily-inlined templates produces false positives on this code
        # base.  Disable just the "maybe" variant; -Wuninitialized (the
        # reliable intra-function form) stays on via -Wall.
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-maybe-uninitialized>
        # Designated-initializer struct literals (`FormatDesc{.create = ...,
        # .configSpecs = ...}`) intentionally omit fields that should
        # value-initialize.  -Wmissing-field-initializers fires on every
        # such site even when the elided fields are well-defined defaults.
        $<$<COMPILE_LANGUAGE:C,CXX>:-Wno-missing-field-initializers>
        $<$<AND:$<BOOL:${PROMEKI_WARNINGS_AS_ERRORS}>,$<COMPILE_LANGUAGE:C,CXX>>:-Werror>
    )
endif()

# Build options ################################################################

option(PROMEKI_BUILD_TUI        "Build the promeki-tui library"    ON)
# Default ON when SDL3 is available, OFF otherwise.
find_package(SDL3 QUIET)
option(PROMEKI_BUILD_SDL        "Build the promeki-sdl library"    ${SDL3_FOUND})
option(PROMEKI_BUILD_UTILS      "Build utility applications"       ON)
option(PROMEKI_BUILD_DEMOS      "Build demonstration applications" ON)
option(PROMEKI_BUILD_TESTS      "Configure unit-test targets (excluded from the default `all` build; use the `check` target to build + run, or `tests` to just build)" ON)
option(PROMEKI_BUILD_DOCS       "Build documentation"              OFF)
# Warning policy: -Wall -Wextra is always applied to our targets (not to
# vendored third-party).  Promoting warnings to errors is opt-in so a new
# compiler version's freshly-introduced warning category cannot break a
# clean local build out of the blue.  scripts/precommit.sh flips this ON
# so the gating build is strict.
option(PROMEKI_WARNINGS_AS_ERRORS "Treat warnings as errors when compiling our targets (-Werror)" OFF)
option(PROMEKI_BUILD_BENCHMARKS "Build the promeki-bench utility"  ON)
# Per-TU build statistics: wraps every compile with /usr/bin/time -v so
# peak RSS + wall/user/sys time land in ${CMAKE_BINARY_DIR}/build-stats.log
# (one tab-separated line per successful compile).  Silently ignored on
# platforms where the GNU-time `-v` probe fails (e.g. Windows, busybox).
# Summary report: scripts/build-stats-report.sh [log] [N]
option(PROMEKI_BUILD_STATS      "Record per-TU compile wall-time and peak RSS (requires GNU /usr/bin/time)" ON)
# Precompiled headers: front-load the hot stable headers (stdlib + promeki
# infrastructure) into a PCH so incremental compiles skip re-parsing them.
# Excludes headers that are edited frequently (variant.h, metadata.h) since
# touching a PCH input invalidates the PCH and cascades.  See
# PROMEKI_PCH_HEADERS below for the content.
option(PROMEKI_USE_PCH          "Use precompiled headers to speed up incremental builds" ON)
# ccache speeds up rebuilds when preprocessed compiler input hasn't actually
# changed (branch switches, undo/redo, whitespace-only edits).  Auto-enabled
# if `ccache` is on PATH, silently skipped otherwise.  Chains cleanly with
# PROMEKI_BUILD_STATS (stats wrapper times the ccache call, so cache hits
# log near-zero wall + small RSS).
option(PROMEKI_USE_CCACHE       "Use ccache as a compiler launcher when available" ON)
# Split debug symbols out of every shared library we build into a sibling
# build/lib-debug/ tree, leaving a stripped (but still backtrace-capable)
# .so in build/lib.  Keeps build/bin + build/lib a lean, copyable runtime
# bundle while the symbols stay retrievable on demand.  No-op for build
# types without debug info (Release / MinSizeRel) and on non-ELF
# toolchains.  See cmake/PromekiSplitDebug.cmake.
option(PROMEKI_SPLIT_DEBUG      "Split debug symbols into build/lib-debug, shipping stripped shared libraries" ON)

# Compiler sanitizer: applied as both compile and link flags to every target
# the build defines (library + executables).  Empty = none.  Useful values
# include "address", "address,undefined", "thread", "leak".  ASan and TSan
# are mutually exclusive; ASan and UBSan compose freely.  Sanitizer runs
# disable PCH (incompatible with -fsanitize on some toolchains) and ccache
# (cache key would not include the flag).
set(PROMEKI_SANITIZER "" CACHE STRING
    "Build with -fsanitize=<value> (e.g. address, thread, address,undefined)")

# Feature flags for the main promeki library
# NOTE: When adding a new PROMEKI_ENABLE_* flag, also update
# src/core/buildinfo.cpp.in (buildFeatureString) and
# include/promeki/config.h.in so the flag appears in build-info output.
option(PROMEKI_ENABLE_NETWORK  "Include networking support"             ON)
option(PROMEKI_ENABLE_HTTP     "Include HTTP server / client (vendored llhttp)" ON)
option(PROMEKI_ENABLE_TLS      "Include TLS support (vendored mbedTLS)"  ON)
option(PROMEKI_ENABLE_SRT      "Include SRT (Secure Reliable Transport) support (vendored libsrt + isolated mbedTLS-3.6)" ON)
option(PROMEKI_ENABLE_MDNS     "Include mDNS / DNS-SD service discovery (vendored mjansson/mdns)" ON)
option(PROMEKI_ENABLE_PROAV    "Include pro A/V support"                ON)
option(PROMEKI_ENABLE_MUSIC    "Include music support"                  ON)
option(PROMEKI_ENABLE_PNG      "Include PNG image I/O"                  ON)
option(PROMEKI_ENABLE_JPEG     "Include JPEG codec"                     ON)
# SVT-JPEG-XS is x86-64 only and needs nasm/yasm at build time; default
# ON only when the host toolchain can actually build it, so it's silently
# picked up on capable machines and silently skipped elsewhere.  Explicit
# -DPROMEKI_ENABLE_JPEGXS=ON/OFF on the command line overrides the probe.
set(PROMEKI_ENABLE_JPEGXS_DEFAULT OFF)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
    find_program(PROMEKI_JPEGXS_ASM NAMES nasm yasm)
    if(PROMEKI_JPEGXS_ASM)
        set(PROMEKI_ENABLE_JPEGXS_DEFAULT ON)
    endif()
endif()
option(PROMEKI_ENABLE_JPEGXS   "Include JPEG XS codec (SVT-JPEG-XS, x86-64 + nasm/yasm)" ${PROMEKI_ENABLE_JPEGXS_DEFAULT})
# x264 — H.264 software encoder.  GPL-2.0-or-later (or a commercial license
# from x264 LLC); enabling it makes the libpromeki build subject to the GPL
# (see THIRD-PARTY-LICENSES and the configure-time GPL COMPLIANCE NOTICE).
# Default ON per project direction.  x86-64 builds use nasm/yasm for the asm
# kernels and fall back to --disable-asm when neither is present; ARM /
# aarch64 assembles its NEON kernels with the C toolchain (no nasm needed).
option(PROMEKI_ENABLE_X264     "Include x264 H.264 software encoder (GPL; vendored libx264)" ON)
option(PROMEKI_ENABLE_FREETYPE "Include FreeType font rendering"        ON)
option(PROMEKI_ENABLE_AUDIO    "Include audio file I/O (libsndfile)"    ON)
option(PROMEKI_ENABLE_OPUS     "Include Opus audio codec (libopus)"     ON)
option(PROMEKI_ENABLE_AAC      "Include AAC audio codec (vendored fdk-aac)" ON)
# Vendored full FFmpeg suite (libav*).  Provides the generic FFmpeg audio
# codec backend (AC-3 / MP3 / FLAC encode+decode) and is shared foundation
# for future FFmpeg-backed work.  Built LGPL-2.1+ (no --enable-gpl /
# --enable-nonfree); it consumes our vendored libmp3lame for MP3 encode.
option(PROMEKI_ENABLE_FFMPEG   "Include vendored FFmpeg (libav*) — AC-3/MP3/FLAC codecs + future" ON)
# Extra codecs surfaced through libsndfile.  Each one transitively pulls
# in its own vendored submodule(s): FLAC requires libFLAC+libogg, Vorbis
# requires libvorbis+libogg, MP3 requires libmpg123 (read) + libmp3lame
# (write).  libsndfile itself is the wrapper — when any of these are on,
# libsndfile-ep is rebuilt with -DENABLE_EXTERNAL_LIBS=ON or
# -DENABLE_MPEG=ON.  Each flag depends transitively on
# PROMEKI_ENABLE_AUDIO; turning AUDIO off forces all three off.
option(PROMEKI_ENABLE_FLAC     "Include FLAC audio codec via libsndfile (libFLAC + libogg)"   ON)
option(PROMEKI_ENABLE_VORBIS   "Include Vorbis audio codec via libsndfile (libvorbis + libogg)" ON)
option(PROMEKI_ENABLE_MP3      "Include MP3 audio codec via libsndfile (mpg123 read + lame write)" ON)
# Vendored whisper.cpp speech-to-text backend.  Drives the WhisperCpp
# TranscriptionEngine; GGML built CPU-only on the first cut.  CUDA /
# Metal / Vulkan backends inside whisper.cpp are off (we'll light them
# up behind PROMEKI_ENABLE_CUDA in a follow-up).  Default ON when
# PROMEKI_ENABLE_PROAV is on, since speech-to-text is a PROAV feature.
option(PROMEKI_ENABLE_WHISPER  "Include vendored whisper.cpp speech-to-text engine" ON)
option(PROMEKI_ENABLE_SRC      "Include audio resampling (libsamplerate)" ON)
option(PROMEKI_ENABLE_CSC      "Include color space conversion (Highway SIMD)" ON)
option(PROMEKI_ENABLE_CIRF     "Include the built-in :/.PROMEKI/ resource set" ON)
# V4L2 capture is Linux-only; default ON when videodev2.h and libasound are
# found, silently OFF elsewhere.  Explicit -DPROMEKI_ENABLE_V4L2=ON/OFF
# on the command line overrides the probe.
set(PROMEKI_ENABLE_V4L2_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckIncludeFile)
    check_include_file(linux/videodev2.h PROMEKI_HAVE_VIDEODEV2_H)
    find_package(ALSA QUIET)
    if(PROMEKI_HAVE_VIDEODEV2_H AND ALSA_FOUND)
        set(PROMEKI_ENABLE_V4L2_DEFAULT ON)
    endif()
endif()
option(PROMEKI_ENABLE_V4L2 "Include V4L2 video capture + ALSA audio capture (Linux only)" ${PROMEKI_ENABLE_V4L2_DEFAULT})
# memfd_create + F_SEAL_WRITE are Linux-only and were added in kernel
# 3.17 / 4.16 respectively.  When available, the SystemCow MemSpace
# uses them to back Buffers with a sealed memfd region whose clones
# are MAP_PRIVATE — giving page-level CoW for free on detach.  Without
# this, SystemCow silently falls back to HostBufferImpl (correctness
# preserved, no CoW optimisation).
set(PROMEKI_ENABLE_MEMFD_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #define _GNU_SOURCE
        #include <sys/mman.h>
        #include <fcntl.h>
        int main() {
            int fd = memfd_create(\"probe\", MFD_CLOEXEC | MFD_ALLOW_SEALING);
            (void)F_ADD_SEALS;
            (void)F_SEAL_WRITE;
            (void)F_SEAL_SHRINK;
            (void)F_SEAL_GROW;
            return fd < 0 ? 1 : 0;
        }
    " PROMEKI_HAVE_MEMFD_CREATE)
    if(PROMEKI_HAVE_MEMFD_CREATE)
        set(PROMEKI_ENABLE_MEMFD_DEFAULT ON)
    endif()
endif()
option(PROMEKI_ENABLE_MEMFD "Use memfd_create-backed sealed regions for the SystemCow MemSpace (Linux only)" ${PROMEKI_ENABLE_MEMFD_DEFAULT})
# DMABUF support: import / export of dma-buf file descriptors so Buffers can
# be shared zero-copy between DMA-capable subsystems — V4L2 capture devices,
# the Xilinx VCU (and other V4L2 mem2mem codecs), DRM/KMS, GPUs, etc.  The
# DmabufBufferImpl backend wraps a dma-buf fd, mmaps it on demand for host
# access, and brackets CPU access with the DMA_BUF_IOCTL_SYNC cache-coherency
# ioctl.  Linux-only; needs <linux/dma-buf.h>.  Without this, Buffer::wrapDmabuf
# returns an invalid Buffer and the MemSpace/MemDomain remain registered as
# metadata only.
set(PROMEKI_ENABLE_DMABUF_DEFAULT OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(CheckCXXSourceCompiles)
    check_cxx_source_compiles("
        #include <linux/dma-buf.h>
        #include <sys/ioctl.h>
        int main() {
            struct dma_buf_sync s;
            s.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_RW;
            (void)DMA_BUF_IOCTL_SYNC;
            (void)s;
            return 0;
        }
    " PROMEKI_HAVE_DMA_BUF_H)
    if(PROMEKI_HAVE_DMA_BUF_H)
        set(PROMEKI_ENABLE_DMABUF_DEFAULT ON)
    endif()
endif()
option(PROMEKI_ENABLE_DMABUF "Enable dma-buf import/export for zero-copy buffer sharing (Linux only)" ${PROMEKI_ENABLE_DMABUF_DEFAULT})
# CUDA support: device-memory + pinned-host MemSpace backends and the
# per-process CudaBootstrap.  Required by every GPU-accelerated codec
# backend that follows (NVENC, NVDEC, etc.).  Default ON when the CUDA
# toolkit is found via find_package(CUDAToolkit), silently OFF elsewhere.
# Explicit -DPROMEKI_ENABLE_CUDA=ON/OFF on the command line overrides
# the probe.
set(PROMEKI_ENABLE_CUDA_DEFAULT OFF)
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    set(PROMEKI_ENABLE_CUDA_DEFAULT ON)
endif()
option(PROMEKI_ENABLE_CUDA     "Include CUDA support (device / pinned-host memspaces, foundation for NVENC / NVDEC)" ${PROMEKI_ENABLE_CUDA_DEFAULT})
# NVENC video encoder backend: NVIDIA hardware H.264 / HEVC via the
# NVIDIA Video Codec SDK.  The SDK is distributed separately from CUDA
# and the driver — see docs/nvenc.dox for setup instructions.  Default
# ON when the SDK is located via PROMEKI_NVENC_SDK_DIR (cache var set
# via -D, or PROMEKI_NVENC_SDK_DIR / NVENC_SDK_DIR environment
# variables), silently OFF elsewhere.  Requires PROMEKI_ENABLE_CUDA.
# The -D cache value overrides the environment variable since CMake
# only honours the default on the first configure when the cache is
# unset.
set(_promeki_nvenc_sdk_dir_default "")
if(DEFINED ENV{PROMEKI_NVENC_SDK_DIR})
    set(_promeki_nvenc_sdk_dir_default "$ENV{PROMEKI_NVENC_SDK_DIR}")
endif()
set(PROMEKI_NVENC_SDK_DIR "${_promeki_nvenc_sdk_dir_default}" CACHE PATH
    "Path to the NVIDIA Video Codec SDK (the directory containing Interface/nvEncodeAPI.h).")
set(PROMEKI_ENABLE_NVENC_DEFAULT OFF)
if(PROMEKI_ENABLE_CUDA_DEFAULT OR PROMEKI_ENABLE_CUDA)
    set(_nvenc_hints "")
    if(PROMEKI_NVENC_SDK_DIR)
        list(APPEND _nvenc_hints "${PROMEKI_NVENC_SDK_DIR}/Interface")
    endif()
    if(DEFINED ENV{NVENC_SDK_DIR})
        list(APPEND _nvenc_hints "$ENV{NVENC_SDK_DIR}/Interface")
    endif()
    find_path(PROMEKI_NVENC_INCLUDE_DIR nvEncodeAPI.h
        HINTS ${_nvenc_hints}
        PATHS /usr/include /usr/local/include
    )
    if(PROMEKI_NVENC_INCLUDE_DIR)
        set(PROMEKI_ENABLE_NVENC_DEFAULT ON)
        message(STATUS "Found NVENC SDK: ${PROMEKI_NVENC_INCLUDE_DIR}")
    else()
        message(STATUS "NVENC SDK not found; set PROMEKI_NVENC_SDK_DIR to enable (see docs/nvenc.dox)")
    endif()
endif()
option(PROMEKI_ENABLE_NVENC    "Include NVIDIA NVENC H.264/HEVC hardware encoder (requires CUDA + Video Codec SDK)" ${PROMEKI_ENABLE_NVENC_DEFAULT})
# NVDEC hardware decoder — nvcuvid.h / cuviddec.h ship in the same
# Video Codec SDK directory as nvEncodeAPI.h, so the NVENC probe's
# include dir doubles as the NVDEC probe.  Keeps the flags separate
# so users can build either/both.
set(PROMEKI_ENABLE_NVDEC_DEFAULT OFF)
if((PROMEKI_ENABLE_CUDA_DEFAULT OR PROMEKI_ENABLE_CUDA) AND PROMEKI_NVENC_INCLUDE_DIR
   AND EXISTS "${PROMEKI_NVENC_INCLUDE_DIR}/nvcuvid.h")
    set(PROMEKI_ENABLE_NVDEC_DEFAULT ON)
endif()
option(PROMEKI_ENABLE_NVDEC    "Include NVIDIA NVDEC H.264/HEVC hardware decoder (requires CUDA + Video Codec SDK)" ${PROMEKI_ENABLE_NVDEC_DEFAULT})
# NDI (Network Device Interface) media transport: NewTek / Vizrt's
# IP video / audio protocol.  The NDI SDK is distributed separately
# from the library and cannot be vendored — see docs/ndi.md for
# setup instructions.  Default ON when the SDK is located via
# PROMEKI_NDI_SDK_DIR (cache var set via -D, or PROMEKI_NDI_SDK_DIR
# / NDI_SDK_DIR environment variables), silently OFF elsewhere.
# Requires PROMEKI_ENABLE_PROAV.  The SDK is headers-only at build
# time; libndi.so / libndi_advanced.so is loaded at runtime via the
# dynamic-load shim shipped in the SDK headers.  The -D cache value
# overrides the environment variable since CMake only honours the
# default on the first configure when the cache is unset.
set(_promeki_ndi_sdk_dir_default "")
if(DEFINED ENV{PROMEKI_NDI_SDK_DIR})
    set(_promeki_ndi_sdk_dir_default "$ENV{PROMEKI_NDI_SDK_DIR}")
endif()
set(PROMEKI_NDI_SDK_DIR "${_promeki_ndi_sdk_dir_default}" CACHE PATH
    "Path to the NDI SDK (the directory containing include/Processing.NDI.Lib.h).")
set(PROMEKI_ENABLE_NDI_DEFAULT OFF)
set(_ndi_hints "")
if(PROMEKI_NDI_SDK_DIR)
    list(APPEND _ndi_hints "${PROMEKI_NDI_SDK_DIR}/include")
endif()
if(DEFINED ENV{NDI_SDK_DIR})
    list(APPEND _ndi_hints "$ENV{NDI_SDK_DIR}/include")
endif()
find_path(PROMEKI_NDI_INCLUDE_DIR Processing.NDI.Lib.h
    HINTS ${_ndi_hints}
    PATHS /usr/include /usr/local/include
)
if(PROMEKI_NDI_INCLUDE_DIR)
    set(PROMEKI_ENABLE_NDI_DEFAULT ON)
    message(STATUS "Found NDI SDK: ${PROMEKI_NDI_INCLUDE_DIR}")
    if(EXISTS "${PROMEKI_NDI_INCLUDE_DIR}/Processing.NDI.Advanced.h")
        message(STATUS "  NDI Advanced SDK headers detected")
    endif()
    # Probe the SDK's own runtime tree for libndi[_advanced].so.6 so the
    # bootstrap can dlopen the absolute path before falling back to the
    # system loader path.  The SDK ships per-arch subdirectories under
    # lib/ (x86_64-linux-gnu, aarch64-linux-gnu, ...); find_file recurses
    # through them so we don't have to hard-code the host triple.
    set(PROMEKI_NDI_RUNTIME_DIR "")
    if(PROMEKI_NDI_SDK_DIR)
        find_file(_promeki_ndi_runtime_so
            NAMES libndi.so.6 libndi_advanced.so.6
            HINTS "${PROMEKI_NDI_SDK_DIR}/lib"
            PATH_SUFFIXES x86_64-linux-gnu aarch64-linux-gnu
                          arm-linux-gnueabihf
            NO_DEFAULT_PATH
        )
        if(_promeki_ndi_runtime_so)
            get_filename_component(PROMEKI_NDI_RUNTIME_DIR
                "${_promeki_ndi_runtime_so}" DIRECTORY)
            message(STATUS "  NDI SDK runtime: ${PROMEKI_NDI_RUNTIME_DIR}")
        else()
            message(STATUS
                "  NDI SDK runtime not found under ${PROMEKI_NDI_SDK_DIR}/lib; "
                "the loader will fall back to LD_LIBRARY_PATH / system paths")
        endif()
    endif()
else()
    message(STATUS "NDI SDK not found; set PROMEKI_NDI_SDK_DIR to enable (see docs/ndi.md)")
endif()
option(PROMEKI_ENABLE_NDI      "Include NDI (Network Device Interface) media transport (requires PROAV + NDI SDK)" ${PROMEKI_ENABLE_NDI_DEFAULT})
# AJA NTV2 SDI / HDMI capture & playout: AJA's open-source libajantv2
# library targeting their Kona / Corvid / IO / T-TAP product line.  The
# library is fully vendored as a submodule under thirdparty/libajantv2/
# (no separate SDK install needed), so this flag defaults to ON when
# PROMEKI_ENABLE_PROAV is on.  Requires PROMEKI_ENABLE_PROAV.  The
# Linux build pulls in libudev at link time — install libudev-dev (or
# equivalent for your distro) before enabling.
option(PROMEKI_ENABLE_NTV2     "Include AJA NTV2 SDI / HDMI capture & playout (vendored libajantv2; requires PROAV)" ON)

# Vendored vs system dependency options
option(PROMEKI_USE_SYSTEM_ZLIB          "Use system zlib instead of vendored zlib-ng"   OFF)
option(PROMEKI_USE_SYSTEM_LIBSPNG       "Use system libspng instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_LIBJPEG       "Use system libjpeg instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_SVT_JPEG_XS   "Use system SVT-JPEG-XS instead of vendored"    OFF)
option(PROMEKI_USE_SYSTEM_X264          "Use system libx264 instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_FREETYPE      "Use system freetype instead of vendored"       OFF)
option(PROMEKI_USE_SYSTEM_SNDFILE       "Use system libsndfile instead of vendored"     OFF)
option(PROMEKI_USE_SYSTEM_OPUS          "Use system libopus instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_FDKAAC        "Use system fdk-aac instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_FFMPEG        "Use system FFmpeg (libav*) instead of vendored" OFF)
# libsndfile external codec deps (FLAC / Vorbis / MP3): each one
# separately switchable between vendored submodule and a system install.
# libogg is the shared dependency for FLAC and Vorbis.
option(PROMEKI_USE_SYSTEM_OGG           "Use system libogg instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_FLAC          "Use system libFLAC instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_VORBIS        "Use system libvorbis instead of vendored"      OFF)
option(PROMEKI_USE_SYSTEM_MPG123        "Use system libmpg123 instead of vendored"      OFF)
option(PROMEKI_USE_SYSTEM_LAME          "Use system libmp3lame instead of vendored"     OFF)
option(PROMEKI_USE_SYSTEM_SAMPLERATE   "Use system libsamplerate instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_NLOHMANN_JSON "Use system nlohmann_json instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_PUGIXML       "Use system pugixml instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_VTC           "Use system libvtc instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_HIGHWAY      "Use system Highway instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_CIRF          "Use system cirf instead of vendored"           OFF)
option(PROMEKI_USE_SYSTEM_LLHTTP        "Use system llhttp instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_MBEDTLS       "Use system mbedTLS instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_SRT           "Use system libsrt instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_MDNS          "Use system mjansson/mdns instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_NTV2          "Use system libajantv2 instead of vendored"     OFF)
option(PROMEKI_USE_SYSTEM_WHISPER       "Use system whisper.cpp instead of vendored"   OFF)

# Build statistics wrapper #####################################################
# When PROMEKI_BUILD_STATS is ON and the platform has GNU `/usr/bin/time -v`,
# wrap every C/C++ compile via CMAKE_*_COMPILER_LAUNCHER so peak RSS and
# wall/user/sys time are recorded to ${CMAKE_BINARY_DIR}/build-stats.log.
# Use scripts/build-stats-report.sh to see the slowest / heaviest TUs.
# Silently skipped when the probe fails (e.g. non-GNU time, Windows).
set(_promeki_launcher_prefix "")
if(PROMEKI_BUILD_STATS)
    find_program(PROMEKI_GNU_TIME NAMES time PATHS /usr/bin /usr/local/bin NO_DEFAULT_PATH)
    if(PROMEKI_GNU_TIME)
        execute_process(
            COMMAND ${PROMEKI_GNU_TIME} -f "ok" -o /dev/null true
            RESULT_VARIABLE _promeki_time_probe
            OUTPUT_QUIET ERROR_QUIET
        )
        if(_promeki_time_probe EQUAL 0)
            set(_promeki_stats_wrapper "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build-stats-wrapper.sh")
            set(_promeki_stats_log     "${CMAKE_BINARY_DIR}/build-stats.log")
            set(_promeki_launcher_prefix "${_promeki_stats_wrapper};${_promeki_stats_log}")
            message(STATUS "PROMEKI_BUILD_STATS: per-TU stats -> ${_promeki_stats_log}")
        else()
            message(STATUS "PROMEKI_BUILD_STATS: /usr/bin/time -f probe failed; stats disabled")
        endif()
    else()
        message(STATUS "PROMEKI_BUILD_STATS: GNU /usr/bin/time not found; stats disabled")
    endif()
endif()

# ccache compiler launcher #####################################################
# Auto-detected; chained AFTER the build-stats wrapper so the wrapper times
# the ccache invocation (cache hits log near-zero wall + tiny RSS).
# Silently skipped when ccache isn't installed.
if(PROMEKI_USE_CCACHE)
    find_program(PROMEKI_CCACHE ccache)
    if(PROMEKI_CCACHE)
        # `CCACHE_SLOPPINESS=pch_defines,time_macros` lets ccache cache
        # compiles that use precompiled headers — without it, every PCH-
        # consuming TU is marked uncacheable and skipped.  Injected via
        # /usr/bin/env so we don't have to touch the user's ~/.ccache/config.
        #
        # pch_defines also makes ccache blind to #define changes that arrive
        # through a PCH, so config.h flag changes can otherwise serve a stale
        # baked-in PCH.  The PROMEKI_CONFIG_SIG define (added where config.h is
        # generated) folds a config.h hash into the compile key to compensate.
        find_program(PROMEKI_ENV_BIN NAMES env PATHS /usr/bin /bin NO_DEFAULT_PATH)
        if(PROMEKI_ENV_BIN)
            set(_ccache_chain "${PROMEKI_ENV_BIN};CCACHE_SLOPPINESS=pch_defines,time_macros;${PROMEKI_CCACHE}")
        else()
            set(_ccache_chain "${PROMEKI_CCACHE}")
        endif()
        if(_promeki_launcher_prefix)
            set(_promeki_launcher_prefix "${_promeki_launcher_prefix};${_ccache_chain}")
        else()
            set(_promeki_launcher_prefix "${_ccache_chain}")
        endif()
        message(STATUS "PROMEKI_USE_CCACHE: using ${PROMEKI_CCACHE}")
    else()
        message(STATUS "PROMEKI_USE_CCACHE: ccache not found on PATH; skipped")
    endif()
endif()

if(_promeki_launcher_prefix)
    set(CMAKE_C_COMPILER_LAUNCHER   "${_promeki_launcher_prefix}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_promeki_launcher_prefix}")
endif()

# Dependency validation ########################################################

if(PROMEKI_ENABLE_MUSIC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_MUSIC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_PNG AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_PNG requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_JPEG AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_JPEG requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_JPEGXS AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_JPEGXS requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_X264 AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_X264 requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_FREETYPE AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_FREETYPE requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_AUDIO AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_AUDIO requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_OPUS AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_OPUS requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_AAC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_AAC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_FFMPEG AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_FFMPEG requires PROMEKI_ENABLE_PROAV")
endif()
# The libsndfile-fronted codecs ride on the libsndfile build (which is the
# audio file-I/O backend), so they transitively need PROMEKI_ENABLE_AUDIO.
# Each one also needs PROMEKI_ENABLE_PROAV via the AUDIO chain.
if(PROMEKI_ENABLE_FLAC AND NOT PROMEKI_ENABLE_AUDIO)
    message(FATAL_ERROR "PROMEKI_ENABLE_FLAC requires PROMEKI_ENABLE_AUDIO")
endif()
if(PROMEKI_ENABLE_VORBIS AND NOT PROMEKI_ENABLE_AUDIO)
    message(FATAL_ERROR "PROMEKI_ENABLE_VORBIS requires PROMEKI_ENABLE_AUDIO")
endif()
if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_ENABLE_AUDIO)
    message(FATAL_ERROR "PROMEKI_ENABLE_MP3 requires PROMEKI_ENABLE_AUDIO")
endif()
if(PROMEKI_ENABLE_WHISPER AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_WHISPER requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_NETWORK AND NOT PROMEKI_ENABLE_PROAV)
    # The library follows a strict layering chain: CORE -> PROAV -> NETWORK -> HTTP.
    # NETWORK code is free to use PROAV types (Frame, AudioBuffer, payload classes,
    # etc.) because PROAV is guaranteed to be on whenever NETWORK is on.
    message(FATAL_ERROR "PROMEKI_ENABLE_NETWORK requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_HTTP AND NOT PROMEKI_ENABLE_NETWORK)
    message(FATAL_ERROR "PROMEKI_ENABLE_HTTP requires PROMEKI_ENABLE_NETWORK")
endif()
if(PROMEKI_ENABLE_TLS AND NOT PROMEKI_ENABLE_NETWORK)
    message(FATAL_ERROR "PROMEKI_ENABLE_TLS requires PROMEKI_ENABLE_NETWORK")
endif()
if(PROMEKI_ENABLE_SRT AND NOT PROMEKI_ENABLE_NETWORK)
    message(FATAL_ERROR "PROMEKI_ENABLE_SRT requires PROMEKI_ENABLE_NETWORK")
endif()
if(PROMEKI_ENABLE_SRT AND NOT PROMEKI_ENABLE_TLS)
    # libsrt requires a crypto backend for handshake auth + AES; we always
    # ride on the same vendored mbedTLS that the rest of the library uses.
    message(FATAL_ERROR "PROMEKI_ENABLE_SRT requires PROMEKI_ENABLE_TLS (vendored mbedTLS)")
endif()
if(PROMEKI_ENABLE_MDNS AND NOT PROMEKI_ENABLE_NETWORK)
    # mDNS rides on the project's UDP socket / multicast plumbing in src/network/.
    # Without NETWORK there is no socket layer to bind 5353 on.
    message(FATAL_ERROR "PROMEKI_ENABLE_MDNS requires PROMEKI_ENABLE_NETWORK")
endif()
if(PROMEKI_ENABLE_SRC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_SRC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_CSC AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_CSC requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_BUILD_SDL AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_BUILD_SDL requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_V4L2 AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_V4L2 requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_V4L2 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "PROMEKI_ENABLE_V4L2 is only supported on Linux")
endif()
if(PROMEKI_ENABLE_V4L2)
    if(NOT PROMEKI_HAVE_VIDEODEV2_H)
        message(FATAL_ERROR "PROMEKI_ENABLE_V4L2 requires linux/videodev2.h")
    endif()
    find_package(ALSA REQUIRED)
endif()
if(PROMEKI_ENABLE_CUDA)
    find_package(CUDAToolkit REQUIRED)
endif()
if(PROMEKI_ENABLE_NVENC)
    if(NOT PROMEKI_ENABLE_CUDA)
        message(FATAL_ERROR "PROMEKI_ENABLE_NVENC requires PROMEKI_ENABLE_CUDA")
    endif()
    if(NOT PROMEKI_ENABLE_PROAV)
        message(FATAL_ERROR "PROMEKI_ENABLE_NVENC requires PROMEKI_ENABLE_PROAV")
    endif()
    if(NOT PROMEKI_NVENC_INCLUDE_DIR)
        message(FATAL_ERROR
            "PROMEKI_ENABLE_NVENC=ON but nvEncodeAPI.h was not found. "
            "Set PROMEKI_NVENC_SDK_DIR to the Video Codec SDK directory "
            "(the one containing Interface/nvEncodeAPI.h).  See docs/nvenc.dox.")
    endif()
endif()
if(PROMEKI_ENABLE_NVDEC)
    if(NOT PROMEKI_ENABLE_CUDA)
        message(FATAL_ERROR "PROMEKI_ENABLE_NVDEC requires PROMEKI_ENABLE_CUDA")
    endif()
    if(NOT PROMEKI_ENABLE_PROAV)
        message(FATAL_ERROR "PROMEKI_ENABLE_NVDEC requires PROMEKI_ENABLE_PROAV")
    endif()
    if(NOT PROMEKI_NVENC_INCLUDE_DIR
       OR NOT EXISTS "${PROMEKI_NVENC_INCLUDE_DIR}/nvcuvid.h")
        message(FATAL_ERROR
            "PROMEKI_ENABLE_NVDEC=ON but nvcuvid.h was not found. "
            "Set PROMEKI_NVENC_SDK_DIR to the Video Codec SDK directory "
            "(the one containing Interface/nvcuvid.h + Interface/cuviddec.h).")
    endif()
endif()
if(PROMEKI_ENABLE_NDI)
    if(NOT PROMEKI_ENABLE_PROAV)
        message(FATAL_ERROR "PROMEKI_ENABLE_NDI requires PROMEKI_ENABLE_PROAV")
    endif()
    if(NOT PROMEKI_NDI_INCLUDE_DIR)
        message(FATAL_ERROR
            "PROMEKI_ENABLE_NDI=ON but Processing.NDI.Lib.h was not found. "
            "Set PROMEKI_NDI_SDK_DIR to the NDI SDK directory "
            "(the one containing include/Processing.NDI.Lib.h).  See docs/ndi.md.")
    endif()
endif()
if(PROMEKI_ENABLE_NTV2 AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_NTV2 requires PROMEKI_ENABLE_PROAV")
endif()

# Build info ###################################################################

find_package(Git QUIET REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE res
    OUTPUT_VARIABLE BUILD_INFO_REPOIDENT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Human-readable "where this build came from" label — surfaced on
# BuildInfo::ref and the {ref} token in formatBuildInfo() for user-
# facing reports.  This is meant to be the most informative git ref
# label available, falling back through:
#
#   1. branch name           — e.g. "main", "mediaio_refactor"
#                              (preferred; user is on a real branch)
#   2. exact tag             — e.g. "v1.2.3"
#                              (detached on a tagged commit, e.g. CI
#                              cutting a release build from a tag)
#   3. `git describe`        — e.g. "v1.2.0-15-gabc1234" or short hash
#                              (detached between tags, or no tags yet)
#   4. "unknown"             — no git info available at all
#
# Step 1 uses `symbolic-ref --short HEAD` rather than `rev-parse
# --abbrev-ref HEAD`, because the latter returns the literal string
# "HEAD" on a detached checkout — which is the case we want to
# *detect* so we can fall through to the tag / describe lookups.
#
# The ref is *not* included in PROMEKI_BUILD_IDENT — the git hash
# already uniquely identifies the source state, and switching
# branches/tags without rebuilding should not trip the staleness
# check.
execute_process(
    COMMAND "${GIT_EXECUTABLE}" symbolic-ref --short HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    RESULT_VARIABLE _git_ref_res
    OUTPUT_VARIABLE BUILD_INFO_REF
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _git_ref_res EQUAL 0 OR BUILD_INFO_REF STREQUAL "")
    # Detached HEAD — try exact tag at HEAD.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" describe --tags --exact-match HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        RESULT_VARIABLE _git_ref_res
        OUTPUT_VARIABLE BUILD_INFO_REF
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _git_ref_res EQUAL 0 OR BUILD_INFO_REF STREQUAL "")
        # Not on a tagged commit — fall back to "closest tag + offset"
        # or, when the repo has no tags at all, the short hash.
        # `--always` is what guarantees the short-hash fallback.
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" describe --tags --always
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            RESULT_VARIABLE _git_ref_res
            OUTPUT_VARIABLE BUILD_INFO_REF
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(NOT _git_ref_res EQUAL 0 OR BUILD_INFO_REF STREQUAL "")
            set(BUILD_INFO_REF "unknown")
        endif()
    endif()
endif()
unset(_git_ref_res)

# Re-run configure when HEAD moves to a new ref.  .git/HEAD is the
# tiny file `git checkout` rewrites with the new ref pointer, so it's
# the right trigger for branch / tag switches (the existing .git/index
# entry below covers commit / index changes).
set_property(GLOBAL APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/.git/index"
    "${PROJECT_SOURCE_DIR}/.git/HEAD"
)

# BuildInfo::version is always the bare MAJOR.MINOR.PATCH triple — it never
# embeds the pre-release stage tag or the CI build number.  Stage and CI
# build live in their own integer fields (stage / stageNum / build) and
# can be re-composed at display time via formatBuildInfo() or by reading
# the opaque BuildInfo::ident.
set(BUILD_INFO_VERSION "${APP_VERSION}")
# Numeric version components — exposed as integer fields on BuildInfo so
# callers can do arithmetic comparisons without parsing the version string.
set(BUILD_INFO_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(BUILD_INFO_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(BUILD_INFO_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(BUILD_INFO_VERSION_BUILD "${APP_VERSION_BUILD}")
# Pre-release stage — translated to the C++ enum identifier so it can be
# substituted directly into the BuildInfo initializer in buildinfo.cpp.in.
if(APP_VERSION_STAGE_NAME STREQUAL "alpha")
    set(BUILD_INFO_VERSION_STAGE "BuildStage::Alpha")
elseif(APP_VERSION_STAGE_NAME STREQUAL "beta")
    set(BUILD_INFO_VERSION_STAGE "BuildStage::Beta")
elseif(APP_VERSION_STAGE_NAME STREQUAL "rc")
    set(BUILD_INFO_VERSION_STAGE "BuildStage::RC")
else()
    set(BUILD_INFO_VERSION_STAGE "BuildStage::Release")
endif()
set(BUILD_INFO_VERSION_STAGE_NUM "${APP_VERSION_STAGE_NUM}")
set(BUILD_INFO_NAME "${APP_HUMAN_NAME}")
string(TIMESTAMP BUILD_INFO_DATE "%Y-%m-%d")
string(TIMESTAMP BUILD_INFO_TIME "%H:%M:%S")
set(BUILD_INFO_TYPE "${CMAKE_BUILD_TYPE}")
cmake_host_system_information(RESULT BUILD_INFO_HOSTNAME QUERY HOSTNAME)

# Build identity string ###################################################
#
# A single fingerprint that uniquely identifies *this particular build* of
# libpromeki.  It is exposed two ways:
#
#   1. As a compile-time #define PROMEKI_BUILD_IDENT in the public header
#      <promeki/buildident.h> (generated below).  Applications and dependent
#      libraries capture this value at *their* compile time.
#
#   2. As BuildInfo::ident (populated in buildinfo.cpp from the same value).
#      The library reports its own current ident via getBuildInfo()->ident.
#
# At application startup, the captured compile-time value is compared
# against the library's runtime value (see verifyBuildIdent() in
# buildinfo.h).  Any divergence means the application binary is stale
# against the libpromeki .so it is dynamically linked to and must be
# rebuilt.  This is intentionally a strict equality check — it is *only*
# meant to detect stale binaries, not to gate version compatibility.
#
# The format is unstructured, opaque, and may change at any time.  Do not
# parse it; use BuildInfo for human-readable build information.
#
# Components are chosen so that any meaningful change to the library's
# ABI or behaviour produces a new ident: short git hash, a +dirty marker
# when the working tree is dirty, the build type, and the build
# date+time (which guarantees a clean rebuild without source changes
# still advances the ident).
execute_process(
    COMMAND "${GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE _git_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(BUILD_INFO_REPOIDENT)
    string(SUBSTRING "${BUILD_INFO_REPOIDENT}" 0 12 _repoident_short)
else()
    set(_repoident_short "unknown")
endif()
if(_git_status)
    set(_repo_dirty "+dirty")
else()
    set(_repo_dirty "")
endif()
# Compose the version segment of the ident in standard semver order:
#   MAJOR.MINOR.PATCH[-stage][+buildN]
# where the stage suffix is:
#   alpha           when stage=Alpha and stageNum=1
#   alphaN          when stage=Alpha and stageNum>1
#   beta            when stage=Beta  and stageNum=1
#   betaN           when stage=Beta  and stageNum>1
#   rcN             when stage=RC    (always numbered)
# Release builds carry no stage suffix.  CI build is folded in only
# when non-zero, so local developer builds of a release version keep
# the clean "1.2.3" form.
if(APP_VERSION_STAGE_NAME STREQUAL "release")
    set(_version_with_stage "${BUILD_INFO_VERSION}")
elseif(APP_VERSION_STAGE_NAME STREQUAL "rc")
    set(_version_with_stage "${BUILD_INFO_VERSION}-rc${APP_VERSION_STAGE_NUM}")
elseif(APP_VERSION_STAGE_NUM EQUAL 1)
    # alpha / beta with stageNum=1 print without the trailing "1".
    set(_version_with_stage "${BUILD_INFO_VERSION}-${APP_VERSION_STAGE_NAME}")
else()
    set(_version_with_stage "${BUILD_INFO_VERSION}-${APP_VERSION_STAGE_NAME}${APP_VERSION_STAGE_NUM}")
endif()
if(NOT APP_VERSION_BUILD STREQUAL "0")
    set(_version_full "${_version_with_stage}+${APP_VERSION_BUILD}")
else()
    set(_version_full "${_version_with_stage}")
endif()
set(BUILD_INFO_IDENT
    "${APP_NAME}/${_version_full}/${_repoident_short}${_repo_dirty}/${BUILD_INFO_TYPE}/${BUILD_INFO_DATE}T${BUILD_INFO_TIME}/${BUILD_INFO_HOSTNAME}"
)
unset(_git_status)
unset(_repoident_short)
unset(_repo_dirty)
unset(_version_with_stage)
unset(_version_full)

configure_file(src/core/buildinfo.cpp.in buildinfo.cpp)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/buildident.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/buildident.h
    @ONLY
)

# Touch the .in file every build so configure_file re-generates buildinfo.cpp
# (and the generated buildident.h) with a fresh timestamp and git hash.  The
# touched .in is registered with CMAKE_CONFIGURE_DEPENDS by configure_file(),
# so cmake reconfigures and regenerates *all* configure_file() outputs in
# this directory — including buildident.h — with the new timestamp.  This
# is what makes the build identity advance on every library rebuild even
# without source changes, which is essential for the staleness check.
#
# The generated file also #includes config.h and platform.h for
# buildPlatformString(), buildFeatureString(), etc. — if you add a new
# PROMEKI_ENABLE_* flag or platform macro that should appear in build-info
# output, update buildinfo.cpp.in as well.
add_custom_target(
    libpromeki_invalidate_build_info
    ALL
    COMMAND ${CMAKE_COMMAND} -E touch ${PROJECT_SOURCE_DIR}/src/core/buildinfo.cpp.in
)

# Feature detection ############################################################

# abi::__cxa_demangle from <cxxabi.h> is part of the Itanium C++ ABI and
# is available on GCC and Clang toolchains, but not on MSVC and can be
# absent on some minimal libc++ configurations.  The crash handler uses
# it to demangle stack frames when present.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
    #include <cxxabi.h>
    int main() {
        int status = 0;
        char *d = abi::__cxa_demangle(\"_Z3foov\", nullptr, nullptr, &status);
        (void)d;
        return 0;
    }
" PROMEKI_HAVE_CXA_DEMANGLE)

# GPL-licensed component registration + compliance notice.  Each GPL
# dependency registers itself here (inside its enable guard); the notice is
# emitted once and PROMEKI_GPL_BUILD is set before config.h is generated so
# the flag is mirrored into the header.  libx265 will add its own register
# line here when it lands.
if(PROMEKI_ENABLE_X264)
    promeki_register_gpl_component("libx264" "GPL-2.0-or-later")
endif()
promeki_emit_gpl_notice()

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
)

# Precompiled-header / ccache staleness guard ##################################
#
# config.h is baked into the precompiled header (the PCH pulls in namespace.h,
# error.h, string.h, … and every one of those #includes config.h).  ccache is
# run with CCACHE_SLOPPINESS=pch_defines (see the ccache launcher block above),
# which is *required* to cache PCH-consuming TUs at all — but it makes ccache
# blind to #define changes that arrive through a PCH.  So a reconfigure that
# flips a PROMEKI_ENABLE_* flag regenerates config.h yet ccache can hand a TU
# the previously-cached object still baked with the old flag values.  That
# surfaces either as a spurious "PROMEKI_ENABLE_X redefined" warning (a fresh
# config.h disagreeing with a stale baked-in PCH value) or, worse, as silently
# miscompiled feature gates.
#
# Fold a hash of the generated config.h into every PCH-consuming compile as a
# command-line define.  ccache *does* key on -D options even under pch_defines,
# so any change to a baked flag changes PROMEKI_CONFIG_SIG, which busts the
# cache and forces both the PCH and its dependent objects to rebuild — while
# no-op rebuilds still hit the cache.  The value is never read by code; it
# exists purely to make the feature configuration part of the compile key.
file(SHA256 ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h PROMEKI_CONFIG_SIG)
string(SUBSTRING ${PROMEKI_CONFIG_SIG} 0 16 PROMEKI_CONFIG_SIG)

# Output directories ###########################################################

# build/bin + build/lib together form a self-contained, copyable runtime
# bundle: executables in bin/, shared libraries (+ their SONAME symlink
# chains) in lib/, resolved against each other via $ORIGIN.  Keep the
# intermediate static archives (.a) — internal deps absorbed into
# libpromeki.so, never shipped — out of that bundle by routing them to a
# separate build/lib-static dir.  The archive dir is a plain (non-cache)
# set so the redirect takes effect even on a build tree previously
# configured with archives pointed at build/lib.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE STRING "Runtime directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Library directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib-static)

# Split-out debug symbols land here — a third sibling of lib/ and
# lib-static/, deliberately OUTSIDE build/lib so the bin/+lib/ runtime
# bundle stays symbol-free and copyable on its own.  Both our own libraries
# (promeki_split_debug, PromekiSplitDebug.cmake) and the vendored / SRT
# shared objects (promeki_strip_so, PromekiStripSo.cmake) drop their .debug
# files here.
set(PROMEKI_DEBUG_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib-debug)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiSplitDebug.cmake)

# Single gate for the whole debug-splitting feature: enabled, ELF toolchain
# (Mach-O uses dSYM, a different mechanism), objcopy available, and a build
# type that actually emits debug info — Release / MinSizeRel compile without
# -g, so there is nothing to split.  Computed once and reused by the
# first-party helper, the vendored-dep stager, the SRT bundler, and the
# install rules.
set(PROMEKI_SPLIT_DEBUG_ACTIVE FALSE)
if(PROMEKI_SPLIT_DEBUG AND UNIX AND NOT APPLE AND CMAKE_OBJCOPY)
    string(TOUPPER "${CMAKE_BUILD_TYPE}" _promeki_bt_upper)
    if(_promeki_bt_upper MATCHES "^(DEBUG|DEVRELEASE|RELWITHDEBINFO)$")
        set(PROMEKI_SPLIT_DEBUG_ACTIVE TRUE)
        message(STATUS "PROMEKI_SPLIT_DEBUG: debug symbols -> ${PROMEKI_DEBUG_OUTPUT_DIRECTORY}")
    else()
        message(STATUS "PROMEKI_SPLIT_DEBUG: build type '${CMAKE_BUILD_TYPE}' carries no debug info; nothing to split")
    endif()
    unset(_promeki_bt_upper)
endif()

# Runtime library search path ($ORIGIN-relative RPATH) ########################
#
# promeki ships several vendored dependencies as their own shared objects
# (the LGPL-licensed libsndfile / libmp3lame / libmpg123 so end users can
# drop in their own builds, plus libfdk-aac and the self-contained
# libpromeki_srt).  Every promeki binary must therefore locate sibling
# libraries *relative to its own install location* rather than falling
# through to whatever the system loader happens to find — otherwise a
# stray /usr/lib/libsndfile.so.1 of a different vintage could be picked up.
#
# We bake an $ORIGIN-relative RPATH into every promeki library and
# executable.  Two entries cover both layouts in one list:
#   $ORIGIN              — a library in lib/ finding a sibling in lib/
#   $ORIGIN/<bin→lib>    — an executable in bin/ reaching up into lib/
# Both are relative, so the whole bin/+lib/ pair can be relocated anywhere
# on disk and still resolve.  The harmless extra entry (an app also gets
# $ORIGIN, a lib also gets the ../lib form) simply never resolves anything.
#
# INSTALL_RPATH_USE_LINK_PATH stays OFF: that default would fold the
# absolute link-time directories (including system and thirdparty-prefix
# paths) into the install RPATH, which is exactly the accidental
# system-library leak we are guarding against.
file(RELATIVE_PATH _PROMEKI_BIN_TO_LIB
    "${CMAKE_INSTALL_FULL_BINDIR}"
    "${CMAKE_INSTALL_FULL_LIBDIR}")
set(CMAKE_INSTALL_RPATH "$ORIGIN" "$ORIGIN/${_PROMEKI_BIN_TO_LIB}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
# Use $ORIGIN-relative entries in the build tree too, so binaries run
# straight out of build/bin + build/lib without an absolute RPATH.
set(CMAKE_BUILD_RPATH_USE_ORIGIN ON)

# By default we emit modern DT_RUNPATH (new dtags): bundled libraries win
# over the system loader's default paths, but an explicit LD_LIBRARY_PATH
# can still override them — useful for debugging and overrides.  Turning
# this option ON switches to legacy DT_RPATH (--disable-new-dtags), which
# is consulted *before* LD_LIBRARY_PATH, forcing the bundled copies to win
# unconditionally.
option(PROMEKI_FORCE_BUNDLED_LIBS
    "Use legacy DT_RPATH so bundled libraries override LD_LIBRARY_PATH" OFF)
if(PROMEKI_FORCE_BUNDLED_LIBS)
    add_link_options("LINKER:--disable-new-dtags")
endif()

# Third Party Dependencies ####################################################

# Pull in any thirdparty/* git submodules the enabled feature set needs.
# Respects PROMEKI_USE_SYSTEM_* opt-outs and PROMEKI_ENABLE_* gates so we
# only fetch what we'll actually build.  Set PROMEKI_MIRRORS_FILE to a
# CMake-includable file to rewrite upstream URLs via internal mirrors —
# see cmake/mirrors.example.cmake.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiSubmodules.cmake)
promeki_init_submodules()

# `thirdparty-info` target: report each vendored submodule's used/pulled
# status and the (possibly mirrored) URL the build fetches it from.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiThirdpartyInfo.cmake)
promeki_add_thirdparty_info_target()

# Prefix for vendored thirdparty builds
set(PROMEKI_THIRDPARTY_PREFIX ${CMAKE_BINARY_DIR}/thirdparty-install)
file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)

# --- nlohmann/json ---
if(PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    find_package(nlohmann_json REQUIRED)
else()
    set(JSON_BuildTests OFF CACHE INTERNAL "")
    set(JSON_Install OFF CACHE INTERNAL "")
    set(JSON_SystemInclude ON CACHE INTERNAL "")
    add_subdirectory(thirdparty/nlohmann-json EXCLUDE_FROM_ALL)
endif()

# --- pugixml ---
# pugixml is a small, fast XML DOM parser used as the backing store for
# XmlDocument / XmlElement / XmlNode / XmlAttribute. We always build the
# static variant with -fPIC so it links cleanly into the promeki shared
# library; pugixml's own install rules are disabled because we install
# its public header under promeki/thirdparty/ alongside our own.
if(PROMEKI_USE_SYSTEM_PUGIXML)
    find_package(pugixml REQUIRED)
    set(PROMEKI_PUGIXML_TARGET pugixml::pugixml)
else()
    set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
    set(PUGIXML_BUILD_TESTS OFF CACHE INTERNAL "")
    set(PUGIXML_INSTALL OFF CACHE INTERNAL "")
    set(PUGIXML_BUILD_SHARED_AND_STATIC_LIBS OFF CACHE INTERNAL "")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(thirdparty/pugixml EXCLUDE_FROM_ALL)
    set(PROMEKI_PUGIXML_TARGET pugixml-static)
endif()

# --- mjansson/mdns ---
# mjansson/mdns is a public-domain, single-header C library implementing
# the wire-level mDNS / DNS-SD protocols from RFC 6762 + RFC 6763.  It is
# the protocol backend for the promeki::Mdns* family (MdnsManager,
# MdnsBrowser, MdnsResolver, MdnsPublisher) wired up in src/network/.
# The library is header-only — the upstream mdns.c is an example/test
# executable that we don't build.  We expose it as an INTERFACE target
# carrying just the include directory; the one source file that
# instantiates the implementation (src/network/mjanssonmdnsbackend.cpp,
# added in a follow-up step) #includes "mdns.h" and uses the inline
# implementation directly.
if(PROMEKI_ENABLE_MDNS)
    if(PROMEKI_USE_SYSTEM_MDNS)
        # Distros do not package mjansson/mdns; PROMEKI_USE_SYSTEM_MDNS=ON
        # only makes sense for users who installed the header by hand into
        # a system include path.  Probe and fail loudly if it is missing.
        find_path(PROMEKI_MDNS_INCLUDE_DIR mdns.h)
        if(NOT PROMEKI_MDNS_INCLUDE_DIR)
            message(FATAL_ERROR
                "PROMEKI_USE_SYSTEM_MDNS=ON but mdns.h was not found on the "
                "system include path. Install mjansson/mdns or unset the flag.")
        endif()
        add_library(promeki_mdns_vendor INTERFACE)
        target_include_directories(promeki_mdns_vendor INTERFACE
            ${PROMEKI_MDNS_INCLUDE_DIR})
    else()
        add_library(promeki_mdns_vendor INTERFACE)
        target_include_directories(promeki_mdns_vendor INTERFACE
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mdns>)
    endif()
    set(PROMEKI_MDNS_TARGET promeki_mdns_vendor)
endif()

# Cross-build detection (used by the cirf block below and by the
# ExternalProject toolchain-propagation block further down).  CMake only
# flips CMAKE_CROSSCOMPILING=TRUE when CMAKE_SYSTEM_NAME differs from the
# host — so a Linux-host -> Linux-aarch64 build (same OS, different arch)
# is not flagged as cross-compile by that variable alone.  Detect the
# arch-mismatch case explicitly here so downstream code can branch on it.
set(_PROMEKI_IS_CROSS_BUILD FALSE)
if(CMAKE_CROSSCOMPILING)
    set(_PROMEKI_IS_CROSS_BUILD TRUE)
elseif(CMAKE_SYSTEM_PROCESSOR AND CMAKE_HOST_SYSTEM_PROCESSOR
       AND NOT CMAKE_SYSTEM_PROCESSOR STREQUAL CMAKE_HOST_SYSTEM_PROCESSOR)
    set(_PROMEKI_IS_CROSS_BUILD TRUE)
endif()

# --- libvtc ---
if(PROMEKI_USE_SYSTEM_VTC)
    find_package(vtc REQUIRED)
    set(PROMEKI_VTC_TARGET vtc::vtc)
else()
    set(VTC_BUILD_SHARED OFF CACHE INTERNAL "")
    set(VTC_BUILD_STATIC ON CACHE INTERNAL "")
    set(VTC_BUILD_UTILS OFF CACHE INTERNAL "")
    set(VTC_BUILD_TESTS OFF CACHE INTERNAL "")
    set(VTC_BUILD_GUI OFF CACHE INTERNAL "")
    set(VTC_BUILD_CPP OFF CACHE INTERNAL "")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory(thirdparty/libvtc EXCLUDE_FROM_ALL)
    set(PROMEKI_VTC_TARGET vtc_static)
endif()

# --- cirf (compiled-in resource filesystem) ---
# cirf is a small C99 utility that compiles arbitrary files into a
# const cirf_folder_t tree. We use it as the storage backend for the
# Qt-style ":/" resource path scheme exposed by the Resource class
# and consumed transparently by File / FileIODevice / Dir.
#
# Rather than add_subdirectory(thirdparty/cirf), we build cirf's
# targets directly here. This avoids pulling in cirf's CMakeLists.txt
# side effects (notably the unscoped `format` and `format-check`
# custom targets, which collide with libvtc's same-named targets).
# The targets we wire up:
#
#   cirf_runtime  — static lib (target arch), linked into promeki
#   cirf_lib      — static lib backing the code-generator binary (host arch)
#   cirf          — code-generator executable (host arch), runs at build
#                   time to emit the generated resource C source from a
#                   JSON config
#
# When cross-compiling, `cirf` must be executable on the build host, not
# the target.  We split it out into a separate host-arch ExternalProject
# (cirf_host) that builds with the system's default toolchain rather than
# the cross toolchain, then point CIRFAddResources.cmake at that binary
# via the CIRF_HOST_EXECUTABLE cache variable.  cirf_runtime continues
# to be built for the target arch in-tree because it is linked into the
# target promeki shared library.
#
# CIRFAddResources.cmake is included directly so the
# `cirf_add_resources()` function is in scope for the call below.
if(PROMEKI_USE_SYSTEM_CIRF)
    find_package(CIRF REQUIRED)
    set(PROMEKI_CIRF_RUNTIME_TARGET CIRF::cirf_runtime)
else()
    set(PROMEKI_CIRF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cirf)

    # cirf_runtime — target-arch static lib, linked into promeki.
    add_library(cirf_runtime STATIC ${PROMEKI_CIRF_DIR}/src/runtime.c)
    target_include_directories(cirf_runtime PUBLIC
        $<BUILD_INTERFACE:${PROMEKI_CIRF_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
    set_target_properties(cirf_runtime PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON
        OUTPUT_NAME cirf_runtime
    )

    if(_PROMEKI_IS_CROSS_BUILD)
        # Cross build: the in-tree cirf binary would target the wrong
        # arch and can't run at build time.  Build a host-arch cirf via
        # ExternalProject_Add (no cross-toolchain args forwarded) and
        # have cirf_add_resources() invoke it through CIRF_HOST_EXECUTABLE.
        set(_PROMEKI_CIRF_HOST_PREFIX ${CMAKE_BINARY_DIR}/cirf-host)
        set(_PROMEKI_CIRF_HOST_BINDIR ${_PROMEKI_CIRF_HOST_PREFIX}/install/bin)
        set(_PROMEKI_CIRF_HOST_BIN    ${_PROMEKI_CIRF_HOST_BINDIR}/cirf)

        ExternalProject_Add(
            cirf_host
            SOURCE_DIR ${PROMEKI_CIRF_DIR}
            PREFIX     ${_PROMEKI_CIRF_HOST_PREFIX}
            # No _PROMEKI_EXTPROJ_CROSS_ARGS here — this sub-build must
            # use the host's default toolchain so the resulting `cirf`
            # binary is executable on the build host.
            CMAKE_ARGS
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_INSTALL_PREFIX=${_PROMEKI_CIRF_HOST_PREFIX}/install
                -DCIRF_BUILD_GENERATOR=ON
                -DCIRF_BUILD_RUNTIME=OFF
                -DCIRF_BUILD_EXAMPLES=OFF
            BUILD_BYPRODUCTS ${_PROMEKI_CIRF_HOST_BIN}
            BUILD_ALWAYS OFF
        )

        set(CIRF_HOST_EXECUTABLE ${_PROMEKI_CIRF_HOST_BIN} CACHE FILEPATH
            "Host-built cirf executable (set automatically by cross build)" FORCE)
        # Name the host-build target so cirf_add_resources() can take a
        # target-level dependency on it.  This orders EVERY resource library
        # (the library's own, plus each demo/util that embeds resources)
        # after cirf_host — without it, only hand-patched call sites were
        # safe and the rest raced the host cirf build under parallel make.
        set(CIRF_HOST_TARGET cirf_host CACHE STRING
            "Target that builds the host-arch cirf (set automatically by cross build)" FORCE)
    else()
        # Native build: cirf can be built for the host directly in-tree.
        add_library(cirf_lib STATIC
            ${PROMEKI_CIRF_DIR}/src/error.c
            ${PROMEKI_CIRF_DIR}/src/json.c
            ${PROMEKI_CIRF_DIR}/src/mime.c
            ${PROMEKI_CIRF_DIR}/src/vfs.c
            ${PROMEKI_CIRF_DIR}/src/glob.c
            ${PROMEKI_CIRF_DIR}/src/config.c
            ${PROMEKI_CIRF_DIR}/src/codegen.c
            ${PROMEKI_CIRF_DIR}/src/writer.c
        )
        target_include_directories(cirf_lib PUBLIC
            $<BUILD_INTERFACE:${PROMEKI_CIRF_DIR}/include>
        )
        set_target_properties(cirf_lib PROPERTIES
            C_STANDARD 99
            C_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME cirf
        )

        add_executable(cirf ${PROMEKI_CIRF_DIR}/src/main.c)
        target_link_libraries(cirf PRIVATE cirf_lib)
        target_include_directories(cirf PRIVATE ${PROMEKI_CIRF_DIR}/include)
        set_target_properties(cirf PROPERTIES
            C_STANDARD 99
            C_STANDARD_REQUIRED ON
        )
    endif()

    # Bring cirf_add_resources() into scope. CIRFAddResources.cmake
    # is self-contained and looks up the `cirf` / `cirf_runtime` /
    # `cirf_lib` targets by name — which we have just defined.
    include(${PROMEKI_CIRF_DIR}/cmake/CIRFAddResources.cmake)

    set(PROMEKI_CIRF_RUNTIME_TARGET cirf_runtime)
endif()

# --- llhttp (HTTP/1.x parser, needed by the HTTP stack) ---
# llhttp is a small C parser (3 source files + 1 header) maintained by
# the Node.js project.  We build it directly as a static library here
# rather than via add_subdirectory(thirdparty/llhttp) because llhttp's
# own CMakeLists.txt requires CMake 3.25+ (we target 3.22), defaults to
# BUILD_SHARED_LIBS=ON, and unconditionally installs targets — none of
# which we want when consuming it as a sub-dependency.  The vendored
# tarball ships with everything already code-generated, so no extra
# build-time tooling is needed.
if(PROMEKI_ENABLE_HTTP)
    if(PROMEKI_USE_SYSTEM_LLHTTP)
        # llhttp ships a CMake config but also works via pkg-config;
        # prefer the imported target when available.
        find_package(llhttp QUIET)
        if(llhttp_FOUND)
            set(PROMEKI_LLHTTP_TARGET llhttp::llhttp)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(LLHTTP REQUIRED IMPORTED_TARGET libllhttp)
            set(PROMEKI_LLHTTP_TARGET PkgConfig::LLHTTP)
        endif()
    else()
        set(PROMEKI_LLHTTP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/llhttp)
        add_library(promeki_llhttp STATIC
            ${PROMEKI_LLHTTP_DIR}/src/api.c
            ${PROMEKI_LLHTTP_DIR}/src/http.c
            ${PROMEKI_LLHTTP_DIR}/src/llhttp.c
        )
        target_include_directories(promeki_llhttp PUBLIC
            $<BUILD_INTERFACE:${PROMEKI_LLHTTP_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
        )
        set_target_properties(promeki_llhttp PROPERTIES
            C_STANDARD 99
            C_STANDARD_REQUIRED ON
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME promeki_llhttp
        )
        set(PROMEKI_LLHTTP_TARGET promeki_llhttp)
    endif()
endif()

# --- mbedTLS (TLS implementation, needed by SslSocket / HTTPS) ---
# mbedTLS is a permissive (Apache-2.0 / GPL-2.0+ dual-licensed)
# self-contained TLS library — no external dependencies, fits the
# "no transitive surprises" requirement for libpromeki's network
# stack.  We pin to the 3.6.x LTS line and build the static
# libraries only; programs and tests are skipped to keep build time
# and binary size in check.
#
# mbedTLS exports targets named `mbedtls`, `mbedx509`, and either
# `mbedcrypto` (older releases) or `tfpsacrypto` (3.6+ PSA path) —
# we capture the actual crypto target name into PROMEKI_MBEDCRYPTO_TARGET
# so SSL-side code can link against it without caring which.
if(PROMEKI_ENABLE_TLS)
    if(PROMEKI_USE_SYSTEM_MBEDTLS)
        find_package(MbedTLS REQUIRED)
        set(PROMEKI_MBEDTLS_TARGET    MbedTLS::mbedtls)
        set(PROMEKI_MBEDX509_TARGET   MbedTLS::mbedx509)
        set(PROMEKI_MBEDCRYPTO_TARGET MbedTLS::mbedcrypto)
    else()
        set(ENABLE_PROGRAMS                       OFF CACHE BOOL "" FORCE)
        set(ENABLE_TESTING                        OFF CACHE BOOL "" FORCE)
        set(MBEDTLS_FATAL_WARNINGS                OFF CACHE BOOL "" FORCE)
        set(USE_STATIC_MBEDTLS_LIBRARY            ON  CACHE BOOL "" FORCE)
        set(USE_SHARED_MBEDTLS_LIBRARY            OFF CACHE BOOL "" FORCE)
        set(DISABLE_PACKAGE_CONFIG_AND_INSTALL    ON  CACHE BOOL "" FORCE)
        # Append our user-config to both the TLS and the PSA crypto
        # configs.  Enables MBEDTLS_THREADING_C / MBEDTLS_THREADING_PTHREAD
        # so the global PSA crypto state (key slots, CTR_DRBG, AES
        # contexts) is locked when multiple threads use TLS.  Without
        # this, ThreadSanitizer flags dozens of races inside psa_crypto.c
        # and friends on any concurrent TLS workload.
        set(_promeki_mbedtls_user_config "${CMAKE_CURRENT_LIST_DIR}/cmake/promeki_mbedtls_user_config.h")
        set(MBEDTLS_USER_CONFIG_FILE        "${_promeki_mbedtls_user_config}" CACHE FILEPATH "" FORCE)
        set(TF_PSA_CRYPTO_USER_CONFIG_FILE  "${_promeki_mbedtls_user_config}" CACHE FILEPATH "" FORCE)
        # mbedTLS 3.6 builds tfpsacrypto by default; preserve that
        # name when we link, with a fallback to the older
        # `mbedcrypto` target name on releases that still ship it.
        add_subdirectory(thirdparty/mbedtls EXCLUDE_FROM_ALL)
        # mbedTLS's static targets are built with -fPIC by default
        # because USE_STATIC_MBEDTLS_LIBRARY honors
        # CMAKE_POSITION_INDEPENDENT_CODE which we set above.
        set(PROMEKI_MBEDTLS_TARGET    mbedtls)
        set(PROMEKI_MBEDX509_TARGET   mbedx509)
        if(TARGET tfpsacrypto)
            set(PROMEKI_MBEDCRYPTO_TARGET tfpsacrypto)
        else()
            set(PROMEKI_MBEDCRYPTO_TARGET mbedcrypto)
        endif()
    endif()
endif()

# --- libsrt (Haivision SRT, vendored as thirdparty/srt) ---
# SRT 1.5.5 still uses the legacy mbedTLS ≤3.x crypto API
# (mbedtls/aes.h, ctr_drbg.h, …), which mbedTLS 4.x relocates under
# --- Toolchain propagation for ExternalProject_Add ---
#
# Vendored thirdparty libraries built via ExternalProject_Add inherit
# the parent project's source tree but NOT its toolchain unless we
# explicitly forward it.  In a host build this is harmless (sub-build
# falls through to the same default compiler); in a cross-build it's
# critical, otherwise sub-builds silently use the host gcc and produce
# x86_64 .a files that won't link into the aarch64 (or whichever)
# parent.  We capture the cross variables here and append them to
# every ExternalProject_Add CMAKE_ARGS block via @c _PROMEKI_EXTPROJ_CROSS_ARGS.
set(_PROMEKI_EXTPROJ_CROSS_ARGS)
# _PROMEKI_IS_CROSS_BUILD was computed earlier (just before the libvtc
# block) so the cirf section can branch on it too.
if(_PROMEKI_IS_CROSS_BUILD)
    if(CMAKE_TOOLCHAIN_FILE)
        # A toolchain file is re-read by every ExternalProject sub-build and
        # already sets the compilers, sysroot, CMAKE_FIND_ROOT_PATH and the
        # find-root modes itself.  Forward ONLY the toolchain file — do NOT
        # also re-forward those vars explicitly.  CMAKE_FIND_ROOT_PATH is a
        # *list* (sysroot + the multiarch staging dir); passing it as
        # -DCMAKE_FIND_ROOT_PATH=a;b splits on the ';' into two CMAKE_ARGS
        # entries, and the second ("b") lands as a stray positional argument
        # to the sub-build's cmake, which silently drops every -D that
        # follows it (so the sub-build falls back to a default install
        # prefix like /usr/local and BUILD_SHARED_LIBS=OFF).
        list(APPEND _PROMEKI_EXTPROJ_CROSS_ARGS
            -DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE})
    else()
        # No toolchain file (compilers set directly on the command line):
        # forward the cross settings explicitly.  All scalars — note that
        # CMAKE_FIND_ROOT_PATH is deliberately not forwarded here for the
        # same list-separator reason; use a toolchain file if the target
        # needs a custom find-root path.
        list(APPEND _PROMEKI_EXTPROJ_CROSS_ARGS
            -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
            -DCMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    message(STATUS "Forwarding cross-compile toolchain to ExternalProject sub-builds "
                   "(${CMAKE_HOST_SYSTEM_PROCESSOR} -> ${CMAKE_SYSTEM_PROCESSOR})")
endif()

# ---------------------------------------------------------------------------
# Autotools cross-compile plumbing.  The CMAKE_ARGS in
# _PROMEKI_EXTPROJ_CROSS_ARGS only reach the CMake-based sub-builds; the
# autotools sub-builds (mpg123, lame) run a plain ./configure that would
# otherwise pick up the host gcc and emit x86_64 objects that won't link
# into the aarch64 parent.  These fragments give those configures the
# cross toolchain the same way:
#
#   _PROMEKI_AUTOTOOLS_CROSS_HOST  --host=<triplet> arg (flips configure
#                                  into cross mode; empty on a host build)
#   _PROMEKI_AUTOTOOLS_CROSS_ENV   CC / CXX / AR / RANLIB `env` overrides
#   _PROMEKI_AUTOTOOLS_SYSROOT     " --sysroot=<path>" fragment to append
#                                  to CFLAGS / CXXFLAGS / LDFLAGS
#
# All empty on a host build, so the configure lines are unchanged there.
set(_PROMEKI_AUTOTOOLS_CROSS_HOST "")
set(_PROMEKI_AUTOTOOLS_CROSS_ENV  "")
set(_PROMEKI_AUTOTOOLS_SYSROOT    "")
if(_PROMEKI_IS_CROSS_BUILD)
    # Canonical target triplet straight from the compiler — robust across
    # Debian (aarch64-linux-gnu), Yocto (aarch64-poky-linux), etc.
    execute_process(
        COMMAND ${CMAKE_C_COMPILER} -dumpmachine
        OUTPUT_VARIABLE _xc_triplet
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    set(_PROMEKI_AUTOTOOLS_CROSS_HOST "--host=${_xc_triplet}")
    set(_PROMEKI_AUTOTOOLS_CROSS_ENV
        "CC=${CMAKE_C_COMPILER}"
        "CXX=${CMAKE_CXX_COMPILER}"
        "AR=${CMAKE_AR}"
        "RANLIB=${CMAKE_RANLIB}")
    if(PROMEKI_SYSROOT)
        set(_PROMEKI_AUTOTOOLS_SYSROOT " --sysroot=${PROMEKI_SYSROOT}")
    endif()
    message(STATUS "Forwarding cross-compile toolchain to autotools sub-builds "
                   "(--host=${_xc_triplet})")
endif()

# mbedtls/private/ — so SRT cannot ride on the project's main vendored
# mbedTLS 4.x stack.  We solve this by pinning a *second* mbedTLS, the
# 3.6 LTS branch, under thirdparty/srt-mbedtls/, and building both it
# and SRT in a fully isolated prefix via ExternalProject_Add.  After
# the isolated build we partial-link libsrt.a together with the three
# 3.6-mbedtls .a's into a single object, then run `objcopy
# --localize-symbol` on every symbol the mbedTLS-3.6 archives define.
# The result is a single static archive (libpromeki_srt_bundle.a) in
# which:
#   * the public SRT C/C++ API stays GLOBAL (consumers can call srt_*),
#   * every mbedtls_* / psa_* symbol is LOCAL — invisible to the
#     subsequent link of promeki.so, so it cannot collide with the
#     main mbedTLS 4.x copy that the rest of the library uses.
# The bundle is then linked PRIVATE into promeki.so as a normal static
# dep.  No SRT or mbedTLS-3.6 symbol leaks out of libpromeki.so.
if(PROMEKI_ENABLE_SRT)
    if(PROMEKI_USE_SYSTEM_SRT)
        find_package(SRT QUIET CONFIG)
        if(SRT_FOUND)
            set(PROMEKI_SRT_TARGET SRT::srt)
        else()
            find_package(PkgConfig REQUIRED)
            pkg_check_modules(SRT REQUIRED IMPORTED_TARGET srt)
            set(PROMEKI_SRT_TARGET PkgConfig::SRT)
        endif()
    else()
        # Threads::Threads is referenced below; ensure it is found
        # regardless of where elsewhere in the file find_package(Threads)
        # might also be called — find_package is idempotent.
        find_package(Threads REQUIRED)
        # Isolated prefix — never shared with the main thirdparty install
        # tree (which is reserved for libraries the rest of promeki sees
        # at compile time, e.g. zlib-ng, libjpeg-turbo).  All SRT
        # artefacts live here and never escape.
        set(PROMEKI_SRT_PREFIX        ${CMAKE_BINARY_DIR}/srt-prefix)
        set(PROMEKI_SRT_INCLUDE_DIR   ${PROMEKI_SRT_PREFIX}/include)
        set(PROMEKI_SRT_LIBDIR        ${PROMEKI_SRT_PREFIX}/lib)
        # Standard libdir varies by platform / distro (lib vs lib64); we
        # force `lib` via -DCMAKE_INSTALL_LIBDIR for both subprojects so
        # downstream paths stay deterministic.
        set(PROMEKI_SRT_MBEDTLS_LIBA  ${PROMEKI_SRT_LIBDIR}/libmbedtls.a)
        set(PROMEKI_SRT_MBEDX509_LIBA ${PROMEKI_SRT_LIBDIR}/libmbedx509.a)
        set(PROMEKI_SRT_MBEDCRYPTO_LIBA ${PROMEKI_SRT_LIBDIR}/libmbedcrypto.a)
        set(PROMEKI_SRT_LIBA          ${PROMEKI_SRT_LIBDIR}/libsrt.a)
        # The bundle is emitted as a shared object directly into the
        # library output dir so it sits beside libpromeki.so and resolves
        # via $ORIGIN both in the build tree and once installed.  It is a
        # promeki-private artifact (mbedTLS-3.6 sealed inside), so it
        # carries a plain SONAME with no independent ABI versioning.
        set(PROMEKI_SRT_BUNDLE_SONAME libpromeki_srt.so)
        set(PROMEKI_SRT_BUNDLE_SO     ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/${PROMEKI_SRT_BUNDLE_SONAME})

        file(MAKE_DIRECTORY ${PROMEKI_SRT_PREFIX}/include
                            ${PROMEKI_SRT_LIBDIR}
                            ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})

        # Build mbedTLS 3.6 LTS into the isolated prefix.  Compiled with
        # -fvisibility=hidden so even before the symbol-localization
        # pass, none of its symbols leak into the dynsym of any consumer
        # that mistakenly forwards them.  EXCLUDE_FROM_ALL keeps it from
        # being built unless something pulls it in.
        ExternalProject_Add(
            srt_mbedtls_ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/srt-mbedtls
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_SRT_PREFIX}
                -DCMAKE_INSTALL_LIBDIR:PATH=lib
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_C_VISIBILITY_PRESET=hidden
                -DCMAKE_CXX_VISIBILITY_PRESET=hidden
                -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
                -DENABLE_PROGRAMS=OFF
                -DENABLE_TESTING=OFF
                -DMBEDTLS_FATAL_WARNINGS=OFF
                -DUSE_STATIC_MBEDTLS_LIBRARY=ON
                -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
                -DDISABLE_PACKAGE_CONFIG_AND_INSTALL=OFF
            BUILD_BYPRODUCTS
                ${PROMEKI_SRT_MBEDTLS_LIBA}
                ${PROMEKI_SRT_MBEDX509_LIBA}
                ${PROMEKI_SRT_MBEDCRYPTO_LIBA}
        )

        # Build SRT against the isolated mbedTLS 3.6 install above.
        # Trimmed to the library only: no apps, no shared lib, no unit
        # tests, no bonding / heavy-logging.  ENABLE_STDCXX_SYNC=ON to
        # use C++11 chrono / threads instead of pthreads.
        ExternalProject_Add(
            srt_ep
            DEPENDS srt_mbedtls_ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/srt
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_SRT_PREFIX}
                -DCMAKE_INSTALL_LIBDIR:PATH=lib
                -DCMAKE_PREFIX_PATH:PATH=${PROMEKI_SRT_PREFIX}
                -DCMAKE_BUILD_TYPE=Release
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_C_VISIBILITY_PRESET=hidden
                -DCMAKE_CXX_VISIBILITY_PRESET=hidden
                -DCMAKE_VISIBILITY_INLINES_HIDDEN=ON
                -DENABLE_APPS=OFF
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DENABLE_UNITTESTS=OFF
                -DENABLE_BONDING=ON
                -DENABLE_LOGGING=ON
                -DENABLE_HEAVY_LOGGING=OFF
                -DENABLE_ENCRYPTION=ON
                -DENABLE_STDCXX_SYNC=ON
                -DENABLE_MONOTONIC_CLOCK=ON
                -DUSE_ENCLIB=mbedtls
                -DMBEDTLS_PREFIX:PATH=${PROMEKI_SRT_PREFIX}
            BUILD_BYPRODUCTS
                ${PROMEKI_SRT_LIBA}
        )

        # Link libsrt.a + the three isolated mbedTLS-3.6 archives into a
        # single shared object (libpromeki_srt.so) with every mbedTLS-3.6
        # symbol localized via --exclude-libs.  See promeki_srt_bundle.cmake
        # for the details.  We link with the C++ compiler driver (not ld
        # directly) so libstdc++ — SRT is C++ — is pulled in correctly.
        # OBJCOPY is passed only when split-debug is active, so the bundler
        # strips the result and saves its symbols to build/lib-debug.
        if(PROMEKI_SPLIT_DEBUG_ACTIVE)
            set(_promeki_srt_objcopy "${CMAKE_OBJCOPY}")
        else()
            set(_promeki_srt_objcopy "")
        endif()
        add_custom_command(
            OUTPUT ${PROMEKI_SRT_BUNDLE_SO}
            DEPENDS srt_ep srt_mbedtls_ep
                    ${PROMEKI_SRT_LIBA}
                    ${PROMEKI_SRT_MBEDTLS_LIBA}
                    ${PROMEKI_SRT_MBEDX509_LIBA}
                    ${PROMEKI_SRT_MBEDCRYPTO_LIBA}
                    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/promeki_srt_bundle.cmake
            COMMAND ${CMAKE_COMMAND}
                -DBUNDLE_SO=${PROMEKI_SRT_BUNDLE_SO}
                -DBUNDLE_SONAME=${PROMEKI_SRT_BUNDLE_SONAME}
                -DSRT_LIBA=${PROMEKI_SRT_LIBA}
                -DMBEDTLS_LIBA=${PROMEKI_SRT_MBEDTLS_LIBA}
                -DMBEDX509_LIBA=${PROMEKI_SRT_MBEDX509_LIBA}
                -DMBEDCRYPTO_LIBA=${PROMEKI_SRT_MBEDCRYPTO_LIBA}
                -DCXX_TOOL=${CMAKE_CXX_COMPILER}
                -DOBJCOPY=${_promeki_srt_objcopy}
                -DDEBUGDIR=${PROMEKI_DEBUG_OUTPUT_DIRECTORY}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/promeki_srt_bundle.cmake
            COMMENT "Linking SRT + isolated mbedTLS-3.6 into ${PROMEKI_SRT_BUNDLE_SONAME} (mbedtls symbols localized)"
            VERBATIM
        )
        add_custom_target(promeki_srt_bundle_target
            DEPENDS ${PROMEKI_SRT_BUNDLE_SO})

        # Wrap the bundle as an INTERFACE target so the rest of CMake can
        # treat it like any other promeki dep — link via PROMEKI_SRT_TARGET,
        # picks up include dirs automatically.  Linking the .so by path
        # gives promeki.so a DT_NEEDED on libpromeki_srt.so, resolved at
        # runtime via the $ORIGIN RPATH; mbedTLS-3.6 stays sealed inside
        # and never appears as a NEEDED of its own.
        add_library(promeki_srt_bundle INTERFACE)
        add_dependencies(promeki_srt_bundle promeki_srt_bundle_target)
        target_link_libraries(promeki_srt_bundle INTERFACE
            ${PROMEKI_SRT_BUNDLE_SO})
        target_include_directories(promeki_srt_bundle SYSTEM INTERFACE
            ${PROMEKI_SRT_INCLUDE_DIR})
        # SRT references pthread / atomic at link time, even with
        # ENABLE_STDCXX_SYNC=ON (haicrypt and the C++ stdlib still pull
        # them in through libstdc++).
        target_link_libraries(promeki_srt_bundle INTERFACE
            Threads::Threads)
        set(PROMEKI_SRT_TARGET promeki_srt_bundle)
    endif()
endif()

# --- Google Highway (SIMD, needed by CSC) ---
if(PROMEKI_ENABLE_CSC)
    if(PROMEKI_USE_SYSTEM_HIGHWAY)
        find_package(HWY REQUIRED)
        set(PROMEKI_HWY_TARGET hwy::hwy)
    else()
        set(HWY_ENABLE_TESTS OFF CACHE INTERNAL "")
        set(HWY_ENABLE_EXAMPLES OFF CACHE INTERNAL "")
        set(HWY_ENABLE_CONTRIB OFF CACHE INTERNAL "")
        set(HWY_ENABLE_INSTALL OFF CACHE INTERNAL "")
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory(thirdparty/highway EXCLUDE_FROM_ALL)
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED})
        set(PROMEKI_HWY_TARGET hwy)
    endif()
endif()

# --- zlib (vendored as zlib-ng in zlib-compat mode; needed by libspng and freetype) ---
# zlib-ng is a drop-in replacement for stock zlib with significantly faster
# deflate/inflate performance on modern x86_64 / ARM. With ZLIB_COMPAT=ON it
# installs as libz.a with the standard zlib.h ABI, so all consumers
# (libspng, freetype, anything else) link against it unchanged.
if(PROMEKI_ENABLE_PNG OR PROMEKI_ENABLE_FREETYPE)
    if(PROMEKI_USE_SYSTEM_ZLIB)
        find_package(ZLIB REQUIRED)
        set(PROMEKI_ZLIB_TARGET ZLIB::ZLIB)
        set(PROMEKI_ZLIB_INCLUDE ${ZLIB_INCLUDE_DIRS})
        set(PROMEKI_ZLIB_LIBRARY ${ZLIB_LIBRARIES})
    else()
        set(PROMEKI_ZLIB_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/zlib-ng)
        set(PROMEKI_ZLIB_INCLUDE ${PROMEKI_THIRDPARTY_PREFIX}/include)
        set(PROMEKI_ZLIB_LIBRARY ${PROMEKI_THIRDPARTY_PREFIX}/lib/libz.a)
        ExternalProject_Add(
            zlib-ng-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zlib-ng
                ${PROMEKI_ZLIB_SRC_COPY}
            SOURCE_DIR ${PROMEKI_ZLIB_SRC_COPY}
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DZLIB_COMPAT=ON
                -DZLIB_ENABLE_TESTS=OFF
                -DZLIBNG_ENABLE_TESTS=OFF
                -DBUILD_SHARED_LIBS=OFF
                -DWITH_GTEST=OFF
            BUILD_BYPRODUCTS ${PROMEKI_ZLIB_LIBRARY}
        )
        add_library(promeki_zlib_vendored STATIC IMPORTED)
        set_target_properties(promeki_zlib_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_ZLIB_LIBRARY}
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_ZLIB_INCLUDE}
        )
        add_dependencies(promeki_zlib_vendored zlib-ng-ep)
        set(PROMEKI_ZLIB_TARGET promeki_zlib_vendored)
    endif()
endif()

# --- libjpeg-turbo ---
if(PROMEKI_ENABLE_JPEG)
    if(PROMEKI_USE_SYSTEM_LIBJPEG)
        find_package(JPEG REQUIRED)
        set(PROMEKI_JPEG_TARGET JPEG::JPEG)
    else()
        find_program(PROMEKI_NASM_PATH NAMES nasm yasm)
        set(_JPEG_TURBO_EXTRA_ARGS)
        if(PROMEKI_NASM_PATH)
            list(APPEND _JPEG_TURBO_EXTRA_ARGS
                -DCMAKE_ASM_NASM_COMPILER=${PROMEKI_NASM_PATH})
        endif()
        ExternalProject_Add(
            libjpeg-turbo-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libjpeg-turbo
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DWITH_TURBOJPEG=OFF
                ${_JPEG_TURBO_EXTRA_ARGS}
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libjpeg.a
        )
        add_library(promeki_jpeg_vendored STATIC IMPORTED)
        set_target_properties(promeki_jpeg_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libjpeg.a
        )
        add_dependencies(promeki_jpeg_vendored libjpeg-turbo-ep)
        set(PROMEKI_JPEG_TARGET promeki_jpeg_vendored)
    endif()
endif()

# --- SVT-JPEG-XS ---
# Modern low-complexity intra-only codec (ISO/IEC 21122).  Open-source
# Intel implementation, BSD+Patent licensed.  x86-64 only and requires
# nasm/yasm at build time (its entire SIMD stack is hand-written asm).
# We consume a single libSvtJpegxs.a that wraps both encoder and decoder
# under the same C API (SvtJpegxsEnc.h / SvtJpegxsDec.h).  pthread and
# libm are transitive link dependencies — added below on the promeki
# link line alongside the imported target.
if(PROMEKI_ENABLE_JPEGXS)
    if(PROMEKI_USE_SYSTEM_SVT_JPEG_XS)
        find_library(PROMEKI_SYSTEM_SVT_JPEG_XS SvtJpegxs REQUIRED)
        add_library(promeki_jpegxs_system INTERFACE IMPORTED)
        target_link_libraries(promeki_jpegxs_system INTERFACE ${PROMEKI_SYSTEM_SVT_JPEG_XS})
        set(PROMEKI_JPEGXS_TARGET promeki_jpegxs_system)
    else()
        ExternalProject_Add(
            svt-jpeg-xs-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/svt-jpeg-xs
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_APPS=OFF
                -DBUILD_TESTING=OFF
                -DCMAKE_BUILD_TYPE=Release
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libSvtJpegxs.a
        )
        add_library(promeki_jpegxs_vendored STATIC IMPORTED)
        set_target_properties(promeki_jpegxs_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libSvtJpegxs.a
        )
        add_dependencies(promeki_jpegxs_vendored svt-jpeg-xs-ep)
        set(PROMEKI_JPEGXS_TARGET promeki_jpegxs_vendored)
    endif()
endif()

# --- x264 (H.264 software encoder, GPL) ---
# x264 uses its own shell ./configure (not CMake), like lame/mpg123, so it is
# wired as an autotools-style ExternalProject built in-source into a copy.
# It is shipped as a separate shared object (libx264.so) alongside
# libpromeki.so so an end user can drop in their own build — and because the
# GPL makes the substitutability matter.  --bit-depth=all + --chroma-format=all
# yield one runtime-selectable library covering 8/10-bit and 4:2:0 / 4:2:2 /
# 4:4:4 (param.i_bitdepth / i_csp choose per session).
if(PROMEKI_ENABLE_X264)
    if(PROMEKI_USE_SYSTEM_X264)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(X264 REQUIRED IMPORTED_TARGET x264)
        set(PROMEKI_X264_TARGET PkgConfig::X264)
    else()
        # x86-64 assembles its SIMD kernels with nasm/yasm; fall back to
        # --disable-asm when neither is found (slower, but builds).  ARM /
        # aarch64 assembles NEON with the C toolchain and needs no probe.
        set(_PROMEKI_X264_ASM_ARGS)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|AMD64)$")
            find_program(PROMEKI_X264_ASM NAMES nasm yasm)
            if(NOT PROMEKI_X264_ASM)
                set(_PROMEKI_X264_ASM_ARGS --disable-asm)
            endif()
        endif()

        # Translate the cross-build toolchain into x264's autotools vocabulary
        # (--host / --cross-prefix), derived from the C compiler name.
        set(_PROMEKI_X264_CROSS_ARGS)
        if(_PROMEKI_IS_CROSS_BUILD)
            get_filename_component(_x264_cc_name "${CMAKE_C_COMPILER}" NAME)
            string(REGEX REPLACE "-?[gc]cc(-[0-9.]+)?$" "" _x264_triple "${_x264_cc_name}")
            if(_x264_triple)
                list(APPEND _PROMEKI_X264_CROSS_ARGS
                    --host=${_x264_triple} --cross-prefix=${_x264_triple}-)
            endif()
        endif()

        set(PROMEKI_X264_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/x264)
        ExternalProject_Add(
            libx264-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/x264 ${PROMEKI_X264_SRC_COPY}
            SOURCE_DIR ${PROMEKI_X264_SRC_COPY}
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND
                ${CMAKE_COMMAND} -E env "CFLAGS=-fPIC -O2"
                ${PROMEKI_X264_SRC_COPY}/configure
                    --prefix=${PROMEKI_THIRDPARTY_PREFIX}
                    --libdir=${PROMEKI_THIRDPARTY_PREFIX}/lib
                    --enable-shared --disable-static --enable-pic
                    --disable-cli --disable-opencl
                    --bit-depth=all --chroma-format=all
                    ${_PROMEKI_X264_ASM_ARGS}
                    ${_PROMEKI_X264_CROSS_ARGS}
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libx264.so
        )
        # Shared (GPL): shipped alongside libpromeki.so, dynamically linked
        # and user-replaceable.  IMPORTED_LOCATION points at the bare .so dev
        # symlink; the linker reads the real DT_SONAME (libx264.so.NNN) from
        # the file.
        add_library(promeki_x264_vendored SHARED IMPORTED)
        # The interface include dir won't exist until x264's `make install`
        # runs; create it now so the imported target's
        # INTERFACE_INCLUDE_DIRECTORIES passes the generate-time existence
        # check (x264.h lands there during the build).
        file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)
        set_target_properties(promeki_x264_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libx264.so
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_THIRDPARTY_PREFIX}/include
        )
        add_dependencies(promeki_x264_vendored libx264-ep)
        set(PROMEKI_X264_TARGET promeki_x264_vendored)
    endif()
endif()

# --- libspng ---
# libspng is used in place of libpng. It's a single-file PNG codec with a
# clean stream/buffer API and no setjmp/longjmp, sized for media-pipeline
# usage (image sequences, realtime decode/encode).
if(PROMEKI_ENABLE_PNG)
    if(PROMEKI_USE_SYSTEM_LIBSPNG)
        find_package(SPNG REQUIRED)
        set(PROMEKI_SPNG_TARGET spng::spng)
    else()
        ExternalProject_Add(
            libspng-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libspng
            DEPENDS zlib-ng-ep
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_PREFIX_PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_ROOT=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
                -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
                -DSPNG_SHARED=OFF
                -DSPNG_STATIC=ON
                -DBUILD_EXAMPLES=OFF
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libspng_static.a
        )
        add_library(promeki_spng_vendored STATIC IMPORTED)
        set_target_properties(promeki_spng_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libspng_static.a
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_THIRDPARTY_PREFIX}/include
            INTERFACE_COMPILE_DEFINITIONS SPNG_STATIC
        )
        add_dependencies(promeki_spng_vendored libspng-ep)
        set(PROMEKI_SPNG_TARGET promeki_spng_vendored)
    endif()
endif()

# --- libogg ---
# Shared object shared by libFLAC (Ogg-FLAC container), libvorbis, and
# libsndfile.  Built as a .so (rather than absorbed) so promeki can also
# call it directly later and an end user can drop in their own build;
# libogg is a leaf with no other vendored deps, so it needs no RPATH.
if((PROMEKI_ENABLE_FLAC OR PROMEKI_ENABLE_VORBIS) AND NOT PROMEKI_USE_SYSTEM_OGG)
    ExternalProject_Add(
        libogg-ep
        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libogg
        CMAKE_ARGS
            ${_PROMEKI_EXTPROJ_CROSS_ARGS}
            -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DBUILD_SHARED_LIBS=ON
            -DBUILD_TESTING=OFF
            -DINSTALL_DOCS=OFF
            -DINSTALL_PKG_CONFIG_MODULE=ON
            -DINSTALL_CMAKE_PACKAGE_MODULE=ON
        BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libogg.so
    )
    add_library(promeki_ogg_vendored SHARED IMPORTED)
    set_target_properties(promeki_ogg_vendored PROPERTIES
        IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libogg.so
    )
    add_dependencies(promeki_ogg_vendored libogg-ep)
    set(PROMEKI_OGG_TARGET promeki_ogg_vendored)
endif()

# --- libFLAC ---
# Vendored libFLAC; depends on libogg-ep for the FLAC-in-Ogg container.
# libsndfile probes for FLAC::FLAC via its own FindFLAC.cmake which
# honours CMAKE_PREFIX_PATH; the install we drop into
# PROMEKI_THIRDPARTY_PREFIX is therefore picked up automatically when
# libsndfile-ep gets CMAKE_PREFIX_PATH pointing here.
if(PROMEKI_ENABLE_FLAC)
    if(PROMEKI_USE_SYSTEM_FLAC)
        find_package(FLAC REQUIRED)
        set(PROMEKI_FLAC_TARGET FLAC::FLAC)
    else()
        ExternalProject_Add(
            libflac-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libflac
            DEPENDS libogg-ep
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_PREFIX_PATH:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=ON
                # libFLAC.so DT_NEEDEDs libogg.so; $ORIGIN lets it find
                # the sibling wherever the lib dir is relocated to.
                -DCMAKE_INSTALL_RPATH=$ORIGIN
                -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
                -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
                -DBUILD_CXXLIBS=OFF
                -DBUILD_PROGRAMS=OFF
                -DBUILD_EXAMPLES=OFF
                -DBUILD_DOCS=OFF
                -DBUILD_TESTING=OFF
                -DINSTALL_MANPAGES=OFF
                -DINSTALL_PKGCONFIG_MODULES=ON
                -DINSTALL_CMAKE_CONFIG_MODULE=ON
                -DWITH_OGG=ON
                -DWITH_FORTIFY_SOURCE=OFF
                -DWITH_STACK_PROTECTOR=OFF
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libFLAC.so
        )
        add_library(promeki_flac_vendored SHARED IMPORTED)
        set_target_properties(promeki_flac_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libFLAC.so
        )
        add_dependencies(promeki_flac_vendored libflac-ep)
        set(PROMEKI_FLAC_TARGET promeki_flac_vendored)
    endif()
endif()

# --- libvorbis ---
# Vendored libvorbis; depends on libogg-ep.  Builds three archives:
# libvorbis.a (the codec), libvorbisenc.a (encoder API), and
# libvorbisfile.a (Ogg container reader).  All three are needed when
# libsndfile drives Vorbis read+write.
if(PROMEKI_ENABLE_VORBIS)
    if(PROMEKI_USE_SYSTEM_VORBIS)
        find_package(Vorbis REQUIRED)
        set(PROMEKI_VORBIS_TARGET Vorbis::vorbis)
        set(PROMEKI_VORBISENC_TARGET Vorbis::vorbisenc)
        set(PROMEKI_VORBISFILE_TARGET Vorbis::vorbisfile)
    else()
        ExternalProject_Add(
            libvorbis-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvorbis
            DEPENDS libogg-ep
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_PREFIX_PATH:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=ON
                # libvorbis*.so DT_NEEDED libogg.so (and each other);
                # $ORIGIN resolves the siblings after relocation.
                -DCMAKE_INSTALL_RPATH=$ORIGIN
                -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
                -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
                -DINSTALL_CMAKE_PACKAGE_MODULE=ON
            BUILD_BYPRODUCTS
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbis.so
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbisenc.so
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbisfile.so
        )
        add_library(promeki_vorbis_vendored SHARED IMPORTED)
        set_target_properties(promeki_vorbis_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbis.so
        )
        add_dependencies(promeki_vorbis_vendored libvorbis-ep)
        add_library(promeki_vorbisenc_vendored SHARED IMPORTED)
        set_target_properties(promeki_vorbisenc_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbisenc.so
        )
        add_dependencies(promeki_vorbisenc_vendored libvorbis-ep)
        add_library(promeki_vorbisfile_vendored SHARED IMPORTED)
        set_target_properties(promeki_vorbisfile_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libvorbisfile.so
        )
        add_dependencies(promeki_vorbisfile_vendored libvorbis-ep)
        set(PROMEKI_VORBIS_TARGET promeki_vorbis_vendored)
        set(PROMEKI_VORBISENC_TARGET promeki_vorbisenc_vendored)
        set(PROMEKI_VORBISFILE_TARGET promeki_vorbisfile_vendored)
    endif()
endif()

# --- libmpg123 ---
# MP3 decoder backend for libsndfile.  Pure autotools (no upstream CMake
# build); the git submodule does not ship a generated configure script,
# so we run autoreconf -fi first.  Static archive only; runtime audio-
# output modules are off (libmpg123 is the codec, not the player).
if(PROMEKI_ENABLE_MP3)
    if(PROMEKI_USE_SYSTEM_MPG123)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MPG123 REQUIRED IMPORTED_TARGET libmpg123)
        set(PROMEKI_MPG123_TARGET PkgConfig::MPG123)
    else()
        # autotools wants to write generated files alongside its sources, so
        # build in-source.  Copy the submodule into the build tree first so
        # the original checkout stays clean.
        set(PROMEKI_MPG123_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/mpg123)
        ExternalProject_Add(
            libmpg123-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/mpg123
                ${PROMEKI_MPG123_SRC_COPY}
            SOURCE_DIR ${PROMEKI_MPG123_SRC_COPY}
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND
                ${CMAKE_COMMAND} -E chdir ${PROMEKI_MPG123_SRC_COPY}
                    autoreconf -fi
                COMMAND
                ${CMAKE_COMMAND} -E env
                    "CFLAGS=-fPIC -O2${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    "CXXFLAGS=-fPIC -O2${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    "LDFLAGS=${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    ${_PROMEKI_AUTOTOOLS_CROSS_ENV}
                ${PROMEKI_MPG123_SRC_COPY}/configure
                    ${_PROMEKI_AUTOTOOLS_CROSS_HOST}
                    --prefix=${PROMEKI_THIRDPARTY_PREFIX}
                    --libdir=${PROMEKI_THIRDPARTY_PREFIX}/lib
                    --disable-static
                    --enable-shared
                    --disable-debug
                    --disable-modules
                    --with-cpu=generic
                    --with-default-audio=dummy
                    --with-audio=dummy
                    --disable-components
                    --enable-libmpg123
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libmpg123.so
        )
        # Shared (LGPL-2.1): shipped alongside libpromeki.so so an end
        # user can drop in their own libmpg123 build.  IMPORTED_LOCATION
        # points at the bare .so dev symlink; the linker reads the real
        # DT_SONAME (libmpg123.so.0) straight from the file.
        add_library(promeki_mpg123_vendored SHARED IMPORTED)
        set_target_properties(promeki_mpg123_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libmpg123.so
        )
        add_dependencies(promeki_mpg123_vendored libmpg123-ep)
        set(PROMEKI_MPG123_TARGET promeki_mpg123_vendored)
    endif()
endif()

# --- libmp3lame ---
# MP3 encoder backend for libsndfile.  Pure autotools; the upstream
# tarball ships a configure script (lame's checkout already contains
# one), so we skip autoreconf.  Frontend programs and the decoder
# stay off — libsndfile drives the encoder API directly.
if(PROMEKI_ENABLE_MP3)
    if(PROMEKI_USE_SYSTEM_LAME)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(LAME REQUIRED IMPORTED_TARGET mp3lame)
        set(PROMEKI_LAME_TARGET PkgConfig::LAME)
    else()
        set(PROMEKI_LAME_SRC_COPY ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-src/lame)
        ExternalProject_Add(
            liblame-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/lame
                ${PROMEKI_LAME_SRC_COPY}
            SOURCE_DIR ${PROMEKI_LAME_SRC_COPY}
            BUILD_IN_SOURCE 1
            CONFIGURE_COMMAND
                ${CMAKE_COMMAND} -E env
                    "CFLAGS=-fPIC -O2${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    "CXXFLAGS=-fPIC -O2${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    "LDFLAGS=${_PROMEKI_AUTOTOOLS_SYSROOT}"
                    ${_PROMEKI_AUTOTOOLS_CROSS_ENV}
                ${PROMEKI_LAME_SRC_COPY}/configure
                    ${_PROMEKI_AUTOTOOLS_CROSS_HOST}
                    --prefix=${PROMEKI_THIRDPARTY_PREFIX}
                    --libdir=${PROMEKI_THIRDPARTY_PREFIX}/lib
                    --disable-static
                    --enable-shared
                    --disable-frontend
                    --disable-analyzer-hooks
                    --disable-gtktest
                    --disable-debug
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libmp3lame.so
        )
        # Shared (LGPL): shipped alongside libpromeki.so so an end user
        # can drop in their own libmp3lame build.  The shared link pulls
        # in libm itself (the psymodel's sin/cos/log), so consumers no
        # longer need to add -lm by hand the way the static archive did.
        add_library(promeki_lame_vendored SHARED IMPORTED)
        set_target_properties(promeki_lame_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libmp3lame.so
        )
        add_dependencies(promeki_lame_vendored liblame-ep)
        set(PROMEKI_LAME_TARGET promeki_lame_vendored)
    endif()
endif()

# --- libopus ---
# Shipped as a shared object (.so) beside libpromeki.so so promeki can
# call it directly and an end user can drop in their own build.  opus is
# a leaf with no other vendored deps, so it needs no RPATH.  Built with
# all of opus's optional extras (custom modes, programs, testing,
# framework, DRED, OSCE, pkg-config, CMake config) turned off — the codec
# backend only needs the encoder / decoder API and we keep the build small.
#
# Defined ahead of libsndfile-ep because libsndfile links Opus::opus as
# part of its external-libs path and therefore lists libopus-ep in its
# DEPENDS (see below) — the dependency target must already exist.
if(PROMEKI_ENABLE_OPUS)
    if(PROMEKI_USE_SYSTEM_OPUS)
        find_package(Opus REQUIRED)
        set(PROMEKI_OPUS_TARGET Opus::opus)
    else()
        ExternalProject_Add(
            libopus-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/opus
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=ON
                -DOPUS_BUILD_SHARED_LIBRARY=ON
                -DOPUS_BUILD_PROGRAMS=OFF
                -DOPUS_BUILD_TESTING=OFF
                -DOPUS_CUSTOM_MODES=OFF
                -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
                -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF
                -DOPUS_DRED=OFF
                -DOPUS_OSCE=OFF
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libopus.so
        )
        add_library(promeki_opus_vendored SHARED IMPORTED)
        set_target_properties(promeki_opus_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libopus.so
        )
        add_dependencies(promeki_opus_vendored libopus-ep)
        set(PROMEKI_OPUS_TARGET promeki_opus_vendored)
    endif()
endif()

# --- libsndfile ---
# Wraps wav/aiff/flac/vorbis/mpeg in one I/O layer.  ENABLE_EXTERNAL_LIBS
# turns on FLAC + Vorbis + Opus inside libsndfile; ENABLE_MPEG turns on
# MPEG layer-I/II/III via mpg123 (read) + lame (write).  libsndfile's
# CMake probes find_package(FLAC), find_package(Vorbis), and
# find_package(MPG123) / find_package(mp3lame) — all of which honour
# CMAKE_PREFIX_PATH.  We push our vendored install prefix in via
# CMAKE_PREFIX_PATH so libsndfile picks up our static archives instead
# of any system copies.
if(PROMEKI_ENABLE_AUDIO)
    if(PROMEKI_USE_SYSTEM_SNDFILE)
        find_package(SndFile REQUIRED)
        set(PROMEKI_SNDFILE_TARGET SndFile::sndfile)
    else()
        # libsndfile insists on both FLAC and Vorbis being present when
        # ENABLE_EXTERNAL_LIBS is on; if only one is enabled, leaving the
        # other off the build still produces a libsndfile that *does* try
        # find_package() for the missing one and bails.  We treat the
        # XIPH external-libs switch as "FLAC OR Vorbis": one being on is
        # enough to flip it.  libsndfile's CMakeLists is tolerant about a
        # missing component as long as ENABLE_EXTERNAL_LIBS is on.
        set(_promeki_sndfile_extlibs OFF)
        if(PROMEKI_ENABLE_FLAC OR PROMEKI_ENABLE_VORBIS)
            set(_promeki_sndfile_extlibs ON)
        endif()
        set(_promeki_sndfile_mpeg OFF)
        if(PROMEKI_ENABLE_MP3)
            set(_promeki_sndfile_mpeg ON)
        endif()

        # Collect the upstream codec build targets the libsndfile-ep must
        # finish behind, so its FindFLAC / FindVorbis / FindMPG123 /
        # Findmp3lame probes have the install prefix populated.
        set(_promeki_sndfile_deps "")
        if(PROMEKI_ENABLE_FLAC AND NOT PROMEKI_USE_SYSTEM_FLAC)
            list(APPEND _promeki_sndfile_deps libflac-ep)
        endif()
        if(PROMEKI_ENABLE_VORBIS AND NOT PROMEKI_USE_SYSTEM_VORBIS)
            list(APPEND _promeki_sndfile_deps libvorbis-ep)
        endif()
        if((PROMEKI_ENABLE_FLAC OR PROMEKI_ENABLE_VORBIS) AND NOT PROMEKI_USE_SYSTEM_OGG)
            list(APPEND _promeki_sndfile_deps libogg-ep)
        endif()
        if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_MPG123)
            list(APPEND _promeki_sndfile_deps libmpg123-ep)
        endif()
        if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_LAME)
            list(APPEND _promeki_sndfile_deps liblame-ep)
        endif()
        # libsndfile gates Opus on the same flag as FLAC/Vorbis/Ogg and
        # links Opus::opus unconditionally on its external-libs path,
        # assuming all four xiph targets are present.  So libopus must be
        # installed before libsndfile configures, or FindOpus misses it and
        # the configure dies on a missing Opus::opus target.  This raced
        # silently on incremental native builds (opus already installed); a
        # clean cross build surfaced it deterministically.
        if(PROMEKI_ENABLE_OPUS AND NOT PROMEKI_USE_SYSTEM_OPUS)
            list(APPEND _promeki_sndfile_deps libopus-ep)
        endif()

        ExternalProject_Add(
            libsndfile-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile
            DEPENDS ${_promeki_sndfile_deps}
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_PREFIX_PATH:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=ON
                # libsndfile.so absorbs the PIC static codec backends
                # (FLAC / Vorbis / Ogg / Opus) but DT_NEEDEDs the shared
                # LGPL ones (mpg123 / mp3lame); $ORIGIN lets it find those
                # siblings wherever the lib dir is relocated to.
                -DCMAKE_INSTALL_RPATH=$ORIGIN
                -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
                -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=OFF
                -DBUILD_PROGRAMS=OFF
                -DBUILD_EXAMPLES=OFF
                -DBUILD_TESTING=OFF
                -DENABLE_EXTERNAL_LIBS=${_promeki_sndfile_extlibs}
                -DENABLE_MPEG=${_promeki_sndfile_mpeg}
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsndfile.so
        )
        # Shared (LGPL-2.1): shipped so an end user can relink against
        # their own libsndfile.  IMPORTED_LOCATION is the bare .so dev
        # symlink; the linker reads the real DT_SONAME (libsndfile.so.1).
        add_library(promeki_sndfile_vendored SHARED IMPORTED)
        set_target_properties(promeki_sndfile_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsndfile.so
        )
        add_dependencies(promeki_sndfile_vendored libsndfile-ep)
        set(PROMEKI_SNDFILE_TARGET promeki_sndfile_vendored)
    endif()
endif()

# --- fdk-aac ---
# Vendored fdk-aac is a static library installed into the third-party
# prefix.  All optional flags off (no CMake config, no pkg-config) — we
# include the headers and link the static archive directly.
if(PROMEKI_ENABLE_AAC)
    if(PROMEKI_USE_SYSTEM_FDKAAC)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FDKAAC REQUIRED IMPORTED_TARGET fdk-aac)
        set(PROMEKI_FDKAAC_TARGET PkgConfig::FDKAAC)
    else()
        ExternalProject_Add(
            fdkaac-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/fdk-aac
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_INSTALL_LIBDIR:PATH=lib
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=ON
                -DBUILD_PROGRAMS=OFF
                -DFDK_AAC_INSTALL_CMAKE_CONFIG_MODULE=OFF
                -DFDK_AAC_INSTALL_PKGCONFIG_MODULE=OFF
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfdk-aac.so
        )
        # Shared: the Fraunhofer FDK license is treated as copyleft-
        # adjacent, so libfdk-aac ships as a replaceable .so beside
        # libpromeki.so (SONAME libfdk-aac.so.2).
        add_library(promeki_fdkaac_vendored SHARED IMPORTED)
        set_target_properties(promeki_fdkaac_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfdk-aac.so
            INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_THIRDPARTY_PREFIX}/include
        )
        # The header dir doesn't exist until ExternalProject installs;
        # CMake refuses to attach a non-existent INTERFACE_INCLUDE_DIRECTORIES,
        # so create it eagerly (the ExternalProject build will populate it).
        file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)
        add_dependencies(promeki_fdkaac_vendored fdkaac-ep)
        set(PROMEKI_FDKAAC_TARGET promeki_fdkaac_vendored)
    endif()
endif()

# --- FFmpeg (libav*) ---
# Vendored full FFmpeg suite, built shared and shipped beside libpromeki.so.
# Provides the generic FFmpeg audio codec backend (AC-3 / MP3 / FLAC) today
# and is shared foundation for future FFmpeg-backed work.  FFmpeg's configure
# is a bespoke shell script (neither autotools nor CMake), but it accepts the
# familiar --prefix / --libdir / --extra-cflags / --extra-ldflags and supports
# out-of-tree builds, so we build in a dedicated BINARY_DIR and leave the
# submodule checkout pristine.
#
# Licence posture: default LGPL-2.1+.  We deliberately do NOT pass
# --enable-gpl (would be forced by libx264) or --enable-nonfree (would be
# forced by libfdk-aac) — AAC stays on our standalone fdk-aac backend and
# x264 stays standalone, so this build remains redistributable under the
# LGPL.  The only external codec lib we wire in is our vendored libmp3lame
# (MP3 encode); AC-3, MP3 decode and FLAC are native FFmpeg codecs.
if(PROMEKI_ENABLE_FFMPEG)
    if(PROMEKI_USE_SYSTEM_FFMPEG)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
            libavcodec libavutil libswresample libavformat)
        set(PROMEKI_FFMPEG_TARGETS PkgConfig::FFMPEG)
    else()
        # Build FFmpeg with our vendored libmp3lame visible (header + .so) so
        # --enable-libmp3lame's configure-time require check links cleanly.
        set(_promeki_ffmpeg_deps "")
        set(_promeki_ffmpeg_extra_enables "")
        if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_MPG123)
            # libmp3lame lives in the third-party prefix once liblame-ep
            # installs; ffmpeg detects it via -I/-L (lame ships no .pc).
            list(APPEND _promeki_ffmpeg_extra_enables --enable-libmp3lame)
            if(NOT PROMEKI_USE_SYSTEM_LAME)
                list(APPEND _promeki_ffmpeg_deps liblame-ep)
            endif()
        endif()

        # x86 SIMD asm needs nasm/yasm.  Keep it when present (big speed win),
        # but degrade gracefully to a C-only build so a host without an
        # assembler still configures.
        find_program(PROMEKI_NASM_EXE NAMES nasm yasm)
        set(_promeki_ffmpeg_asm "")
        if(NOT PROMEKI_NASM_EXE)
            set(_promeki_ffmpeg_asm --disable-x86asm)
            message(STATUS "FFmpeg: no nasm/yasm found — building without x86 asm")
        endif()

        # Translate the cross-build toolchain into FFmpeg's configure
        # vocabulary.  Like x264, FFmpeg's configure is its own shell script
        # (neither autotools nor CMake) and ignores the toolchain file, so
        # without these it would probe the host gcc and emit host-arch
        # objects that won't link into the cross-built parent.  Feed it
        # --enable-cross-compile + the target arch / OS, the tool prefix for
        # ar / nm / ranlib / strip (--cross-prefix), the explicit cross
        # cc / cxx (the toolchain may carry a version suffix like -14 that
        # --cross-prefix alone would miss — e.g. aarch64-linux-gnu-gcc-14),
        # and the sysroot.  _xc_triplet is the bare target triplet computed
        # once in the autotools cross block above (e.g. aarch64-linux-gnu).
        set(_promeki_ffmpeg_cross "")
        if(_PROMEKI_IS_CROSS_BUILD)
            string(TOLOWER "${CMAKE_SYSTEM_NAME}" _promeki_ffmpeg_target_os)
            list(APPEND _promeki_ffmpeg_cross
                --enable-cross-compile
                --arch=${CMAKE_SYSTEM_PROCESSOR}
                --target-os=${_promeki_ffmpeg_target_os}
                --cross-prefix=${_xc_triplet}-
                --cc=${CMAKE_C_COMPILER}
                --cxx=${CMAKE_CXX_COMPILER})
            if(PROMEKI_SYSROOT)
                list(APPEND _promeki_ffmpeg_cross --sysroot=${PROMEKI_SYSROOT})
            endif()
            message(STATUS "FFmpeg: cross-compiling for "
                           "${CMAKE_SYSTEM_PROCESSOR}-${_promeki_ffmpeg_target_os} "
                           "(--cross-prefix=${_xc_triplet}-)")
        endif()

        ExternalProject_Add(
            ffmpeg-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg
            DEPENDS ${_promeki_ffmpeg_deps}
            BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/thirdparty-build/ffmpeg
            CONFIGURE_COMMAND
                ${CMAKE_COMMAND} -E env
                    "PKG_CONFIG_PATH=${PROMEKI_THIRDPARTY_PREFIX}/lib/pkgconfig"
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/ffmpeg/configure
                    --prefix=${PROMEKI_THIRDPARTY_PREFIX}
                    --libdir=${PROMEKI_THIRDPARTY_PREFIX}/lib
                    --shlibdir=${PROMEKI_THIRDPARTY_PREFIX}/lib
                    --incdir=${PROMEKI_THIRDPARTY_PREFIX}/include
                    --enable-shared
                    --disable-static
                    --enable-pic
                    --disable-programs
                    --disable-doc
                    --disable-autodetect
                    --extra-cflags=-I${PROMEKI_THIRDPARTY_PREFIX}/include
                    --extra-ldflags=-L${PROMEKI_THIRDPARTY_PREFIX}/lib
                    ${_promeki_ffmpeg_asm}
                    ${_promeki_ffmpeg_cross}
                    ${_promeki_ffmpeg_extra_enables}
            BUILD_COMMAND make
            INSTALL_COMMAND make install
            BUILD_BYPRODUCTS
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libavcodec.so
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libavutil.so
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libswresample.so
                ${PROMEKI_THIRDPARTY_PREFIX}/lib/libavformat.so
        )
        file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)
        # The libav* set we link directly.  Each is a replaceable shared
        # object beside libpromeki.so (LGPL-2.1+).  libavformat carries the
        # muxers / demuxers consumed by the FfmpegMediaIO container backend.
        set(PROMEKI_FFMPEG_TARGETS "")
        foreach(_av avcodec avutil swresample avformat)
            add_library(promeki_${_av}_vendored SHARED IMPORTED)
            set_target_properties(promeki_${_av}_vendored PROPERTIES
                IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/lib${_av}.so
                INTERFACE_INCLUDE_DIRECTORIES ${PROMEKI_THIRDPARTY_PREFIX}/include
            )
            add_dependencies(promeki_${_av}_vendored ffmpeg-ep)
            list(APPEND PROMEKI_FFMPEG_TARGETS promeki_${_av}_vendored)
        endforeach()
    endif()
endif()

# --- whisper.cpp ---
# Vendored whisper.cpp + GGML (CPU-only build for the first cut).  The
# build installs libwhisper.a plus three GGML archives (libggml.a is
# the dispatcher, libggml-base.a holds the shared graph/tensor core,
# libggml-cpu.a is the CPU backend).  All linked PRIVATE into
# libpromeki.so — the whisper API surface is an implementation detail
# of the WhisperTranscriptionEngine backend, not part of the public
# promeki API.
#
# Knobs turned off explicitly:
#   * BUILD_SHARED_LIBS=OFF         — static archives, linked into our .so
#   * WHISPER_BUILD_TESTS / EXAMPLES / SERVER=OFF — strip everything
#                                      except the library itself
#   * WHISPER_CURL=OFF              — model fetching is handled by
#                                      promeki-fetch-model, not by
#                                      whisper.cpp's own libcurl path
#   * GGML_BUILD_TESTS=OFF          — same reason
#   * GGML_BUILD_EXAMPLES=OFF       — ditto
#
# Knobs turned on explicitly (perf wins for the CPU backend):
#   * GGML_OPENMP=ON                — use libgomp's thread pool for
#                                      the CPU GEMM kernels.  Faster
#                                      than ggml's hand-rolled
#                                      std::thread pool and the only
#                                      thing that lets whisper scale
#                                      past a handful of cores.
#   * GGML_LLAMAFILE=ON             — enable llamafile's tinyBLAS
#                                      sgemm fast path, which lifts
#                                      the encoder noticeably on
#                                      AVX2 / AVX-512 hosts.
#
# CUDA / Metal / Vulkan backends are intentionally left at their
# defaults (off) on this first cut; PROMEKI_ENABLE_CUDA will pass
# GGML_CUDA=ON through here in a follow-up.
if(PROMEKI_ENABLE_WHISPER)
    if(PROMEKI_USE_SYSTEM_WHISPER)
        find_package(whisper REQUIRED)
        set(PROMEKI_WHISPER_TARGET whisper::whisper)
    else()
        # whisper.cpp installs its archives into lib/ on Linux/macOS, but
        # the static-library filenames depend on the toolchain.  On GCC/
        # Clang they're libwhisper.a / libggml*.a; MSVC uses .lib.  Stick
        # with the .a names since this code path only runs on Unix-like
        # platforms (Windows builds would set PROMEKI_USE_SYSTEM_WHISPER
        # or build differently).
        set(_whisper_lib     ${PROMEKI_THIRDPARTY_PREFIX}/lib/libwhisper.a)
        set(_ggml_lib        ${PROMEKI_THIRDPARTY_PREFIX}/lib/libggml.a)
        set(_ggml_base_lib   ${PROMEKI_THIRDPARTY_PREFIX}/lib/libggml-base.a)
        set(_ggml_cpu_lib    ${PROMEKI_THIRDPARTY_PREFIX}/lib/libggml-cpu.a)
        ExternalProject_Add(
            whisper-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/whisper.cpp
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_INSTALL_LIBDIR:PATH=lib
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DWHISPER_BUILD_TESTS=OFF
                -DWHISPER_BUILD_EXAMPLES=OFF
                -DWHISPER_BUILD_SERVER=OFF
                -DWHISPER_CURL=OFF
                -DWHISPER_SDL2=OFF
                -DWHISPER_COREML=OFF
                -DWHISPER_OPENVINO=OFF
                -DGGML_BUILD_TESTS=OFF
                -DGGML_BUILD_EXAMPLES=OFF
                -DGGML_OPENMP=ON
                -DGGML_LLAMAFILE=ON
            BUILD_BYPRODUCTS
                ${_whisper_lib}
                ${_ggml_lib}
                ${_ggml_base_lib}
                ${_ggml_cpu_lib}
        )
        # Wrap the four static archives in an INTERFACE library so callers
        # link a single target.  Link order matters: whisper depends on
        # ggml (dispatcher), which depends on ggml-cpu (backend), which
        # depends on ggml-base (shared core).  Threads + libm are
        # transitive deps (GGML uses pthread / std::thread + libm math).
        # OpenMP is needed by ggml-cpu because GGML_OPENMP=ON above
        # pulls libgomp symbols into the static archive — the consumer
        # of the archive must link the runtime.
        find_package(Threads REQUIRED)
        find_package(OpenMP REQUIRED)
        add_library(promeki_whisper_lib STATIC IMPORTED)
        set_target_properties(promeki_whisper_lib PROPERTIES
            IMPORTED_LOCATION ${_whisper_lib})
        add_library(promeki_ggml_lib STATIC IMPORTED)
        set_target_properties(promeki_ggml_lib PROPERTIES
            IMPORTED_LOCATION ${_ggml_lib})
        add_library(promeki_ggml_cpu_lib STATIC IMPORTED)
        set_target_properties(promeki_ggml_cpu_lib PROPERTIES
            IMPORTED_LOCATION ${_ggml_cpu_lib})
        add_library(promeki_ggml_base_lib STATIC IMPORTED)
        set_target_properties(promeki_ggml_base_lib PROPERTIES
            IMPORTED_LOCATION ${_ggml_base_lib})
        add_dependencies(promeki_whisper_lib whisper-ep)
        add_dependencies(promeki_ggml_lib whisper-ep)
        add_dependencies(promeki_ggml_cpu_lib whisper-ep)
        add_dependencies(promeki_ggml_base_lib whisper-ep)
        add_library(promeki_whisper_vendored INTERFACE)
        target_link_libraries(promeki_whisper_vendored INTERFACE
            promeki_whisper_lib
            promeki_ggml_lib
            promeki_ggml_cpu_lib
            promeki_ggml_base_lib
            Threads::Threads
            OpenMP::OpenMP_CXX
            m
            ${CMAKE_DL_LIBS}
        )
        # whisper.cpp installs its public headers into include/ at the
        # top level (whisper.h plus ggml*.h alongside).  Create the dir
        # eagerly so the imported target can advertise it before the
        # ExternalProject runs.
        file(MAKE_DIRECTORY ${PROMEKI_THIRDPARTY_PREFIX}/include)
        target_include_directories(promeki_whisper_vendored SYSTEM INTERFACE
            ${PROMEKI_THIRDPARTY_PREFIX}/include)
        set(PROMEKI_WHISPER_TARGET promeki_whisper_vendored)
        unset(_whisper_lib)
        unset(_ggml_lib)
        unset(_ggml_base_lib)
        unset(_ggml_cpu_lib)
    endif()
endif()

# --- libsamplerate ---
if(PROMEKI_ENABLE_SRC)
    if(PROMEKI_USE_SYSTEM_SAMPLERATE)
        find_package(SampleRate REQUIRED)
        set(PROMEKI_SAMPLERATE_TARGET SampleRate::samplerate)
    else()
        ExternalProject_Add(
            libsamplerate-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsamplerate
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_TESTING=OFF
                -DLIBSAMPLERATE_EXAMPLES=OFF
                -DLIBSAMPLERATE_INSTALL=ON
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsamplerate.a
        )
        add_library(promeki_samplerate_vendored STATIC IMPORTED)
        set_target_properties(promeki_samplerate_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsamplerate.a
        )
        add_dependencies(promeki_samplerate_vendored libsamplerate-ep)
        set(PROMEKI_SAMPLERATE_TARGET promeki_samplerate_vendored)
    endif()
endif()

# --- freetype ---
if(PROMEKI_ENABLE_FREETYPE)
    if(PROMEKI_USE_SYSTEM_FREETYPE)
        find_package(Freetype REQUIRED)
        set(PROMEKI_FREETYPE_TARGET Freetype::Freetype)
    else()
        ExternalProject_Add(
            freetype-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/freetype
            DEPENDS zlib-ng-ep
            CMAKE_ARGS
                ${_PROMEKI_EXTPROJ_CROSS_ARGS}
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
                -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
                -DBUILD_SHARED_LIBS=OFF
                -DFT_DISABLE_BZIP2=ON
                -DFT_DISABLE_BROTLI=ON
                -DFT_DISABLE_HARFBUZZ=ON
                -DFT_DISABLE_PNG=ON
            BUILD_BYPRODUCTS ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfreetype.a
        )
        add_library(promeki_freetype_vendored STATIC IMPORTED)
        set_target_properties(promeki_freetype_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfreetype.a
        )
        add_dependencies(promeki_freetype_vendored freetype-ep)
        set(PROMEKI_FREETYPE_TARGET promeki_freetype_vendored)
    endif()
endif()

# --- AJA NTV2 (libajantv2, vendored as thirdparty/libajantv2) ---
# libajantv2 is AJA Video Systems' open-source SDK for their NTV2-family
# capture / playout boards (Kona, Corvid, IO, T-TAP, ...).  We bring it
# in as a submodule pinned to the ntv2_18_0_0 release tag and build it
# as a static library that gets linked PRIVATE into libpromeki.so.
#
# Build configuration:
#   * AJANTV2_BUILD_SHARED=OFF       — static, linked into promeki.so
#   * AJANTV2_DISABLE_DEMOS=ON       — skip sample apps
#   * AJANTV2_DISABLE_DRIVER=ON      — skip the Linux kernel driver
#                                      (out-of-tree module, distributed
#                                      separately and not built here)
#   * AJANTV2_DISABLE_TESTS=ON       — skip libajantv2's own unit tests
#   * AJANTV2_DISABLE_TOOLS=ON       — skip command-line tools
#   * AJANTV2_DISABLE_PLUGIN_LOAD=ON — disable libajantv2's runtime plugin
#                                      loader, which otherwise pulls in
#                                      its own bundled mbedTLS and would
#                                      collide with the mbedTLS we already
#                                      vendor for PROMEKI_ENABLE_TLS.
#   * AJANTV2_DISABLE_RDMA=ON        — no NVIDIA GPUDirect RDMA support
#
# Linux platform link deps (rt, pthread, dl, udev) come through the
# `ajantv2` INTERFACE link list automatically — installing libudev-dev
# is a prerequisite for enabling PROMEKI_ENABLE_NTV2 on Linux.
if(PROMEKI_ENABLE_NTV2)
    if(PROMEKI_USE_SYSTEM_NTV2)
        find_package(libajantv2 REQUIRED)
        set(PROMEKI_NTV2_TARGET libajantv2::ajantv2)
    else()
        set(AJANTV2_BUILD_OPENSOURCE    ON  CACHE INTERNAL "")
        set(AJANTV2_BUILD_SHARED        OFF CACHE INTERNAL "")
        set(AJANTV2_DISABLE_DEMOS       ON  CACHE INTERNAL "")
        set(AJANTV2_DISABLE_DRIVER      ON  CACHE INTERNAL "")
        set(AJANTV2_DISABLE_TESTS       ON  CACHE INTERNAL "")
        set(AJANTV2_DISABLE_TOOLS       ON  CACHE INTERNAL "")
        set(AJANTV2_DISABLE_PLUGIN_LOAD ON  CACHE INTERNAL "")
        set(AJANTV2_DISABLE_RDMA        ON  CACHE INTERNAL "")
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)
        add_subdirectory(thirdparty/libajantv2 EXCLUDE_FROM_ALL)
        set(PROMEKI_NTV2_TARGET ajantv2)
    endif()
endif()

# Main Library: promeki ########################################################

set(PROMEKI_HEADERS
    include/promeki/algorithm.h
    include/promeki/amf0.h
    # Network address / SDP value types — core types, not NETWORK-gated.
    # See the matching block in PROMEKI_SOURCES for the rationale.
    include/promeki/ipv4address.h
    include/promeki/ipv6address.h
    include/promeki/eui64.h
    include/promeki/macaddress.h
    include/promeki/sdpsession.h
    include/promeki/ansistream.h
    include/promeki/application.h
    include/promeki/array.h
    include/promeki/asyncbufferqueue.h
    include/promeki/atomic.h
    include/promeki/audiolevel.h
    include/promeki/benchmark.h
    include/promeki/benchmarkrunner.h
    include/promeki/buffer.h
    include/promeki/bufferallocator.h
    include/promeki/bufferediodevice.h
    include/promeki/bufferiodevice.h
    include/promeki/bufferpool.h
    include/promeki/bufferview.h
    include/promeki/buildinfo.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/buildident.h
    include/promeki/bytearray.h
    include/promeki/cadence.h
    include/promeki/char.h
    include/promeki/ciepoint.h
    include/promeki/ciewavelengthtable.h
    include/promeki/audiochannelmap.h
    include/promeki/audiocodec.h
    include/promeki/audioformat.h
    include/promeki/audiostreamdesc.h
    include/promeki/backendweight.h
    include/promeki/base64.h
    include/promeki/cmdlineparser.h
    include/promeki/color.h
    include/promeki/colormodel.h
    include/promeki/configoption.h
    include/promeki/cpumonitor.h
    include/promeki/cuda.h
    include/promeki/datetime.h
    include/promeki/dir.h
    include/promeki/duration.h
    include/promeki/elapsedtimer.h
    include/promeki/enum.h
    include/promeki/enums_anc.h
    include/promeki/enums_audio.h
    include/promeki/enums_clock.h
    include/promeki/enums_codec.h
    include/promeki/enums_color.h
    include/promeki/enums_dns.h
    include/promeki/enums_jxs.h
    include/promeki/enums_mediaio.h
    include/promeki/enums_ndi.h
    include/promeki/enums_network.h
    include/promeki/enums_rtmp.h
    include/promeki/enums_rtp.h
    include/promeki/enums_st2110.h
    include/promeki/enums_subtitle.h
    include/promeki/enums_timecode.h
    include/promeki/enums_tpg.h
    include/promeki/enums_transcription.h
    include/promeki/enums_video.h
    include/promeki/env.h
    include/promeki/error.h
    include/promeki/event.h
    include/promeki/eventloop.h
    include/promeki/file.h
    include/promeki/fileformatfactory.h
    include/promeki/fileinfo.h
    include/promeki/filepath.h
    include/promeki/fnv1a.h
    include/promeki/fourcc.h
    include/promeki/framecount.h
    include/promeki/framenumber.h
    include/promeki/framerate.h
    include/promeki/function.h
    include/promeki/pixelaspect.h
    include/promeki/future.h
    include/promeki/h264bitstream.h
    include/promeki/hdmisignalconfig.h
    include/promeki/hevcbitstream.h
    include/promeki/hexdump.h
    include/promeki/hmac.h
    include/promeki/iodevice.h
    include/promeki/json.h
    include/promeki/keyevent.h
    include/promeki/layout.h
    include/promeki/line.h
    include/promeki/list.h
    include/promeki/logger.h
    include/promeki/map.h
    include/promeki/matrix.h
    include/promeki/matrix3x3.h
    include/promeki/md5.h
    include/promeki/mediaduration.h
    include/promeki/mempool.h
    include/promeki/dmabufbufferimpl.h
    include/promeki/dmaheap.h
    include/promeki/memfdbufferimpl.h
    include/promeki/memfdregion.h
    include/promeki/memspace.h
    include/promeki/numa.h
    include/promeki/numahostbufferimpl.h
    include/promeki/pinnedhostbufferimpl.h
    include/promeki/metadata.h
    include/promeki/mouseevent.h
    include/promeki/mutex.h
    include/promeki/namespace.h
    include/promeki/numname.h
    include/promeki/numnameseq.h
    include/promeki/objectbase.h
    include/promeki/obfuscatedstring.h
    include/promeki/once.h
    include/promeki/optional.h
    include/promeki/pidcontroller.h
    include/promeki/pixelformat.h
    include/promeki/pixelmemlayout.h
    include/promeki/platform.h
    include/promeki/point.h
    include/promeki/process.h
    include/promeki/promise.h
    include/promeki/queue.h
    include/promeki/random.h
    include/promeki/rational.h
    include/promeki/ratetracker.h
    include/promeki/readwritelock.h
    include/promeki/rect.h
    include/promeki/regex.h
    include/promeki/resource.h
    include/promeki/sdioutputfanoutconfig.h
    include/promeki/sdisignalconfig.h
    include/promeki/sdistandards.h
    include/promeki/securemem.h
    include/promeki/selfpipe.h
    include/promeki/set.h
    include/promeki/sha1.h
    include/promeki/sha2.h
    include/promeki/sharedptr.h
    include/promeki/signal.h
    include/promeki/signalhandler.h
    include/promeki/size2d.h
    include/promeki/slot.h
    include/promeki/statsaccumulator.h
    include/promeki/streamstring.h
    include/promeki/string.h
    include/promeki/stringdata.h
    include/promeki/stringlist.h
    include/promeki/stringregistry.h
    include/promeki/structdatabase.h
    include/promeki/system.h
    include/promeki/terminal.h
    include/promeki/strand.h
    include/promeki/thread.h
    include/promeki/threadpool.h
    include/promeki/timecode.h
    include/promeki/timecodegenerator.h
    include/promeki/timerevent.h
    include/promeki/timestamp.h
    include/promeki/util.h
    include/promeki/umid.h
    include/promeki/uniqueptr.h
    include/promeki/units.h
    include/promeki/url.h
    include/promeki/uuid.h
    include/promeki/variant.h
    include/promeki/variantdatabase.h
    include/promeki/variantlookup.h
    include/promeki/variantspec.h
    include/promeki/videocodec.h
    include/promeki/videoformat.h
    include/promeki/videoformatdetails.h
    include/promeki/videoportref.h
    include/promeki/videoreferenceconfig.h
    include/promeki/waitcondition.h
    include/promeki/widget.h
    include/promeki/xml.h
    include/promeki/xyzcolor.h
)

set(PROMEKI_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
    # Network address / SDP value types.  These live under src/network/ but
    # have no networking (socket) dependency — they are plain value types and
    # text-format (de)serializers wired into the core Variant/DataType system
    # (their headers are guarded #if PROMEKI_ENABLE_CORE) and referenced by
    # core (Variant specs) and pro A/V (MediaConfig keys, AudioDesc/ImageDesc/
    # AncDesc::fromSdp/toSdp) regardless of PROMEKI_ENABLE_NETWORK.  They must
    # therefore build with the core library, not behind the NETWORK gate.
    src/network/ipv4address.cpp
    src/network/ipv6address.cpp
    src/network/eui64.cpp
    src/network/macaddress.cpp
    src/network/sdpsession.cpp
    src/core/ansistream.cpp
    src/core/application.cpp
    src/core/array.cpp
    src/core/asyncbufferqueue.cpp
    src/core/basicthread.cpp
    src/core/benchmark.cpp
    src/core/benchmarkrunner.cpp
    src/core/buffer.cpp
    src/core/bufferallocator.cpp
    src/core/buffercommand.cpp
    src/core/bufferfactory.cpp
    src/core/bufferview.cpp
    src/core/bufferediodevice.cpp
    src/core/bufferiodevice.cpp
    src/core/bufferpool.cpp
    src/core/hostbufferimpl.cpp
    src/core/cadence.cpp
    src/core/char.cpp
    src/core/clock.cpp
    src/core/clockdomain.cpp
    src/core/ciepoint.cpp
    src/core/cmdlineparser.cpp
    src/core/color.cpp
    src/core/colormodel.cpp
    src/core/contentlightlevel.cpp
    src/core/cpumonitor.cpp
    src/core/masteringdisplay.cpp
    src/core/xyzcolor.cpp
    src/core/base64.cpp
    src/core/hexdump.cpp
    src/core/crashhandler.cpp
    src/core/crc.cpp
    src/core/cuda.cpp
    src/core/cudabufferimpl.cpp
    src/core/datastream.cpp
    src/core/datatype.cpp
    src/core/datetime.cpp
    src/core/dir.cpp
    src/core/duration.cpp
    src/core/enum.cpp
    src/core/enumlist.cpp
    src/core/env.cpp
    src/core/error.cpp
    src/core/event.cpp
    src/core/eventloop.cpp
    src/core/file.cpp
    src/core/fileinfo.cpp
    src/core/fileiodevice.cpp
    src/core/filepath.cpp
    src/core/fourcc.cpp
    src/core/framecount.cpp
    src/core/framenumber.cpp
    src/core/framerate.cpp
    src/core/pixelaspect.cpp
    src/core/hdmisignalconfig.cpp
    src/core/histogram.cpp
    src/core/hmac.cpp
    src/core/iodevice.cpp
    src/core/json.cpp
    src/core/keyevent.cpp
    src/core/layout.cpp
    src/core/line.cpp
    src/core/list.cpp
    src/core/libraryoptions.cpp
    src/core/logger.cpp
    src/core/map.cpp
    src/core/matrix.cpp
    src/core/matrix3x3.cpp
    src/core/mediaduration.cpp
    src/core/mediatimestamp.cpp
    src/core/md5.cpp
    src/core/dmabufbufferimpl.cpp
    src/core/dmaheap.cpp
    src/core/memfdbufferimpl.cpp
    src/core/memfdregion.cpp
    src/core/numa.cpp
    src/core/numahostbufferimpl.cpp
    src/core/pinnedhostbufferimpl.cpp
    src/core/mempool.cpp
    src/core/memdomain.cpp
    src/core/memspace.cpp
    src/core/metadata.cpp
    src/core/mouseevent.cpp
    src/core/windowfocusevent.cpp
    src/core/numname.cpp
    src/core/numnameseq.cpp
    src/core/objectbase.cpp
    src/core/pacinggate.cpp
    src/core/periodiccallback.cpp
    src/core/pidcontroller.cpp
    src/core/point.cpp
    src/core/process.cpp
    src/core/random.cpp
    src/core/rational.cpp
    src/core/ratetracker.cpp
    src/core/rect.cpp
    src/core/regex.cpp
    src/core/resource.cpp
    src/core/sdioutputfanoutconfig.cpp
    src/core/sdisignalconfig.cpp
    src/core/selfpipe.cpp
    src/core/set.cpp
    src/core/sha1.cpp
    src/core/sha2.cpp
    src/core/securemem.cpp
    src/core/sharedmemory.cpp
    src/core/sharedptr.cpp
    src/core/signal.cpp
    src/core/signalhandler.cpp
    src/core/size2d.cpp
    src/core/slot.cpp
    src/core/string.cpp
    src/core/stringdata.cpp
    src/core/stringiodevice.cpp
    src/core/stringlist.cpp
    src/core/stringregistry.cpp
    src/core/syntheticclock.cpp
    src/core/system.cpp
    src/core/terminal.cpp
    src/core/textstream.cpp
    src/core/textwrap.cpp
    src/core/thread.cpp
    src/core/threadpool.cpp
    src/core/timecode.cpp
    src/core/timecodegenerator.cpp
    src/core/timecodeuserbits.cpp
    src/core/timestamp.cpp
    src/core/umid.cpp
    src/core/uniqueptr.cpp
    src/core/units.cpp
    src/core/url.cpp
    src/core/util.cpp
    src/core/uuid.cpp
    src/core/variant.cpp
    src/core/variantquery.cpp
    src/core/variantspec.cpp
    src/core/videoportref.cpp
    src/core/videoreferenceconfig.cpp
    src/core/widget.cpp
    src/core/windowedstat.cpp
    src/core/xml.cpp
)

# Network sources
if(PROMEKI_ENABLE_NETWORK)
    list(APPEND PROMEKI_HEADERS
        include/promeki/abstractsocket.h
        include/promeki/dnscache.h
        include/promeki/dnsconfig.h
        include/promeki/dnsname.h
        include/promeki/dnspacket.h
        include/promeki/dnsrecord.h
        include/promeki/dnsresolver.h
        include/promeki/ipv4subnet.h
        include/promeki/ipv6subnet.h
        include/promeki/klvframe.h
        include/promeki/localserver.h
        include/promeki/localsocket.h
        include/promeki/loopbacktransport.h
        include/promeki/multicastmanager.h
        include/promeki/multicastreceiver.h
        include/promeki/networkaddress.h
        include/promeki/networkinterface.h
        include/promeki/networkinterfacebackend.h
        include/promeki/networkinterfaceimpl.h
        include/promeki/enums_pcap.h
        include/promeki/pcapreader.h
        include/promeki/packetdemux.h
        include/promeki/pcapsdpmap.h
        include/promeki/pcapflowrouter.h
        include/promeki/packettransport.h
        include/promeki/prioritysocket.h
        include/promeki/ntptime.h
        include/promeki/rawsocket.h
        include/promeki/rtcppacket.h
        include/promeki/rtcpscheduler.h
        include/promeki/rtmpchunkstream.h
        include/promeki/rtmpclient.h
        include/promeki/rtmphandshake.h
        include/promeki/rtmpmessage.h
        include/promeki/rtmpsession.h
        include/promeki/rtpaggregatorthread.h
        include/promeki/rtpancdepacketizerthread.h
        include/promeki/rtpancpacketizerthread.h
        include/promeki/rtpaudiodepacketizerthread.h
        include/promeki/rtpaudiopacketizerthread.h
        include/promeki/rtpaudiotxthread.h
        include/promeki/rtpdatadepacketizerthread.h
        include/promeki/rtpdepacketizerthread.h
        include/promeki/rtppacket.h
        include/promeki/rtppacketbatch.h
        include/promeki/rtppacketizerthread.h
        include/promeki/rtppayload.h
        include/promeki/rtppayloadanc.h
        include/promeki/rtppayloadrawvideo.h
        include/promeki/jxsmarker.h
        include/promeki/st2110tx.h
        include/promeki/st2110trafficcalc.h
        include/promeki/st2110video.h
        include/promeki/st2110vrxmonitor.h
        include/promeki/rtpseqreorderbuffer.h
        include/promeki/rtpseqtracker.h
        include/promeki/rtpsession.h
        include/promeki/rtpmediaclock.h
        include/promeki/rtpstreamclock.h
        include/promeki/rtptxthread.h
        include/promeki/rtpvideodepacketizerthread.h
        include/promeki/rxpayloadbundle.h
        include/promeki/rfc7273refclk.h
        include/promeki/packetscheduler.h
        include/promeki/burstpacketscheduler.h
        include/promeki/cadencepacketscheduler.h
        include/promeki/kernelfqpacketscheduler.h
        include/promeki/phcclock.h
        include/promeki/txtimepacketscheduler.h
        include/promeki/rtpmediaio.h
        include/promeki/rtmpmediaio.h
        include/promeki/framebridge.h
        include/promeki/framebridgemediaio.h
        include/promeki/socketaddress.h
        include/promeki/sslcontext.h
        include/promeki/tcpserver.h
        include/promeki/tcpsocket.h
        include/promeki/udpsocket.h
        include/promeki/udpsockettransport.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/abstractsocket.cpp
        src/network/dnscache.cpp
        src/network/dnsconfig.cpp
        src/network/dnsname.cpp
        src/network/dnspacket.cpp
        src/network/dnsrecord.cpp
        src/network/dnsresolver.cpp
        src/network/sslcontext.cpp
        src/network/ipv4subnet.cpp
        src/network/ipv6subnet.cpp
        src/network/klvframe.cpp
        src/network/localserver.cpp
        src/network/localsocket.cpp
        src/network/loopbacktransport.cpp
        src/network/multicastmanager.cpp
        src/network/multicastreceiver.cpp
        src/network/networkaddress.cpp
        src/network/emscriptennetworkinterfacebackend.cpp
        src/network/linuxnetworkinterfacemonitor.cpp
        src/network/networkinterface.cpp
        src/network/networkinterfacebackend.cpp
        src/network/networkinterfacemonitor.cpp
        src/network/networkrouting.cpp
        src/network/pcapreader.cpp
        src/network/packetdemux.cpp
        src/network/pcapsdpmap.cpp
        src/network/pcapflowrouter.cpp
        src/network/packettransport.cpp
        src/network/posixnetworkinterfacebackend.cpp
        src/network/posixnetworkrouting.cpp
        src/network/prioritysocket.cpp
        src/network/rawsocket.cpp
        src/network/rtcppacket.cpp
        src/network/rtcpscheduler.cpp
        src/network/rtmpchunkstream.cpp
        src/network/rtmpclient.cpp
        src/network/rtmphandshake.cpp
        src/network/rtmpsession.cpp
        src/network/rtpaggregatorthread.cpp
        src/network/rtpancdepacketizerthread.cpp
        src/network/rtpancpacketizerthread.cpp
        src/network/rtpaudiodepacketizerthread.cpp
        src/network/rtpaudiopacketizerthread.cpp
        src/network/rtpaudiotxthread.cpp
        src/network/rtpdatadepacketizerthread.cpp
        src/network/rtpdepacketizerthread.cpp
        src/network/rtppacketizerthread.cpp
        src/network/rtppayload.cpp
        src/network/rtppayloadanc.cpp
        src/network/rtppayloadrawvideo.cpp
        src/network/jxsmarker.cpp
        src/network/st2110tx.cpp
        src/network/st2110trafficcalc.cpp
        src/network/st2110video.cpp
        src/network/st2110vrxmonitor.cpp
        src/network/rtpseqreorderbuffer.cpp
        src/network/rtpseqtracker.cpp
        src/network/rfc7273refclk.cpp
        src/network/packetscheduler.cpp
        src/network/burstpacketscheduler.cpp
        src/network/cadencepacketscheduler.cpp
        src/network/kernelfqpacketscheduler.cpp
        src/network/phcclock.cpp
        src/network/txtimepacketscheduler.cpp
        src/network/rtpsession.cpp
        src/network/rtpmediaclock.cpp
        src/network/rtpstreamclock.cpp
        src/network/rtptxthread.cpp
        src/network/rtpvideodepacketizerthread.cpp
        src/proav/rtpmediaio.cpp
        src/proav/rtmpmediaio.cpp
        src/proav/framebridge.cpp
        src/proav/framebridgemediaio.cpp
        src/network/socketaddress.cpp
        src/network/tcpserver.cpp
        src/network/tcpsocket.cpp
        src/network/udpsocket.cpp
        src/network/udpsockettransport.cpp
    )
endif()

# TLS sources (subset of network; vendored mbedTLS).  @c sslcontext is
# always shipped — its header declaration is needed for the public
# @ref HttpClient / @ref HttpServer / @ref WebSocket API to stay
# shape-stable across feature configurations.  In a TLS-disabled build
# the @c .cpp compiles to a stub that returns @c Error::NotSupported.
# @c sslsocket stays gated since it is purely an internal transport.
if(PROMEKI_ENABLE_TLS)
    list(APPEND PROMEKI_HEADERS
        include/promeki/sslsocket.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/sslsocket.cpp
        src/network/mbedtlsdebug.cpp
    )
endif()

# SRT sources (subset of network; vendored libsrt + isolated mbedTLS-3.6).
# Headers reference an opaque @c int handle so the public surface does
# not pull in @c <srt/srt.h>; only the @c .cpp files do.
if(PROMEKI_ENABLE_SRT)
    list(APPEND PROMEKI_HEADERS
        include/promeki/srtsocket.h
        include/promeki/srtserver.h
        include/promeki/srtsockettransport.h
        include/promeki/srtepoll.h
        include/promeki/srtgroup.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/srtsocket.cpp
        src/network/srtserver.cpp
        src/network/srtsockettransport.cpp
        src/network/srtepoll.cpp
        src/network/srtgroup.cpp
    )
endif()

# mDNS / DNS-SD sources (subset of network; vendored mjansson/mdns).
# Step 2 lands the pure value types — @ref MdnsServiceType,
# @ref MdnsTxtRecord, @ref MdnsServiceInstance — plus the abstract
# Bundled mDNS / DNS-SD implementation on top of the vendored
# mjansson/mdns packet parser.  The @c promeki_mdns_vendor target is
# wired up alongside the other vendored deps near the top of this
# file.  Public headers expose value types (MdnsServiceType,
# MdnsTxtRecord, MdnsServiceInstance, MdnsRecord) and the engine
# classes (MdnsManager, MdnsBrowser, MdnsResolver, MdnsPublisher).
if(PROMEKI_ENABLE_MDNS)
    list(APPEND PROMEKI_HEADERS
        include/promeki/mdnsbrowser.h
        include/promeki/mdnsmanager.h
        include/promeki/mdnspacket.h
        include/promeki/mdnspublisher.h
        include/promeki/mdnsrecord.h
        include/promeki/mdnsresolver.h
        include/promeki/mdnsname.h
        include/promeki/mdnsservicetype.h
        include/promeki/mdnstxtrecord.h
        include/promeki/mdnsserviceinstance.h
        include/promeki/mdnstypebrowser.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/mdnsbrowser.cpp
        src/network/mdnsmanager.cpp
        src/network/mdnsname.cpp
        src/network/mdnspacket.cpp
        src/network/mdnspublisher.cpp
        src/network/mdnsrecord.cpp
        src/network/mdnsresolver.cpp
        src/network/mdnsservicetype.cpp
        src/network/mdnstxtrecord.cpp
        src/network/mdnsserviceinstance.cpp
        src/network/mdnstypebrowser.cpp
    )
endif()

# HTTP sources (subset of network; vendored llhttp parser)
if(PROMEKI_ENABLE_HTTP)
    list(APPEND PROMEKI_HEADERS
        include/promeki/debugserver.h
        include/promeki/httpapi.h
        include/promeki/httpclient.h
        include/promeki/httpconnection.h
        include/promeki/httpfilehandler.h
        include/promeki/httphandler.h
        include/promeki/httpheaders.h
        include/promeki/httpmethod.h
        include/promeki/httprequest.h
        include/promeki/httpresponse.h
        include/promeki/httprouter.h
        include/promeki/httpserver.h
        include/promeki/httpstatus.h
        include/promeki/websocket.h
        include/promeki/mjpegstreammediaio.h
    )
    list(APPEND PROMEKI_SOURCES
        src/proav/mjpegstreammediaio.cpp
        src/network/debugmodules.cpp
        src/network/debugserver.cpp
        src/network/httpapi.cpp
        src/network/httpclient.cpp
        src/network/httpconnection.cpp
        src/network/httpfilehandler.cpp
        src/network/httphandler.cpp
        src/network/httpheaders.cpp
        src/network/httpmethod.cpp
        src/network/httprequest.cpp
        src/network/httpresponse.cpp
        src/network/httprouter.cpp
        src/network/httpserver.cpp
        src/network/httpstatus.cpp
        src/network/websocket.cpp
    )
endif()

# ProAV sources
if(PROMEKI_ENABLE_PROAV)
    list(APPEND PROMEKI_HEADERS
        include/promeki/ancafd.h
        include/promeki/ancatc.h
        include/promeki/ancdesc.h
        include/promeki/ancformat.h
        include/promeki/ancmeta.h
        include/promeki/ancop47sdp.h
        include/promeki/ancpacket.h
        include/promeki/ancpayload.h
        include/promeki/ancst2020audio.h
        include/promeki/anctranslateconfig.h
        include/promeki/anctranslator.h
        include/promeki/ancframesync.h
        include/promeki/framesyncdisposition.h
        include/promeki/captiondecoder.h
        include/promeki/captionencoder.h
        include/promeki/cea608.h
        include/promeki/cea608decoder.h
        include/promeki/cea608encoder.h
        include/promeki/cea608ext.h
        include/promeki/cea608packet.h
        include/promeki/cea608xds.h
        include/promeki/cea608xdsinjector.h
        include/promeki/cea708cdp.h
        include/promeki/cea708ext.h
        include/promeki/hdmiinfoframe.h
        include/promeki/hdrdynamic2094_40.h
        include/promeki/hdrstaticmetadata.h
        include/promeki/st291packet.h
        include/promeki/subrip.h
        include/promeki/subtitle.h
        include/promeki/subtitlecuebuilder.h
        include/promeki/transcript.h
        include/promeki/transcriptionengine.h
        include/promeki/audioblock.h
        include/promeki/audiobuffer.h
        include/promeki/audiodecoder.h
        include/promeki/audiodesc.h
        include/promeki/audiometer.h
        include/promeki/audioencoder.h
        include/promeki/audiofile.h
        include/promeki/audiofilefactory.h
        include/promeki/audiogen.h
        include/promeki/aacbitstream.h
        include/promeki/audiotestpattern.h
        include/promeki/basicfont.h
        include/promeki/videoencoder.h
        include/promeki/videodecoder.h
        include/promeki/videoencodersei.h
        include/promeki/h264profilelevel.h
        include/promeki/x264videoencoder.h
        include/promeki/encodeddesc.h
        include/promeki/fastfont.h
        include/promeki/flvtag.h
        include/promeki/font.h
        include/promeki/frame.h
        include/promeki/imagedesc.h
        include/promeki/imagefile.h
        include/promeki/imagefileio.h
        include/promeki/imgseq.h
        include/promeki/jpeggeometryprobe.h
        include/promeki/ltcdecoder.h
        include/promeki/ltcencoder.h
        include/promeki/mediaio.h
        include/promeki/mediaioallocator.h
        include/promeki/mediaioport.h
        include/promeki/mediaiosink.h
        include/promeki/mediaiosource.h
        include/promeki/mediaioportgroup.h
        include/promeki/mediaioportconnection.h
        include/promeki/mediapipeline.h
        include/promeki/mediapipelineconfig.h
        include/promeki/mediapipelineplanner.h
        include/promeki/mediapipelinestats.h
        include/promeki/mediapipelinetrigger.h
        include/promeki/pipelineevent.h
        include/promeki/mediapayload.h
        include/promeki/videopayload.h
        include/promeki/uncompressedvideopayload.h
        include/promeki/compressedvideopayload.h
        include/promeki/audiopayload.h
        include/promeki/pcmaudiopayload.h
        include/promeki/pcmsilencefiller.h
        include/promeki/compressedaudiopayload.h
        include/promeki/mpegts.h
        include/promeki/mpegtsmuxer.h
        include/promeki/mpegtsdemuxer.h
        include/promeki/mpegtsframer.h
        include/promeki/mpegtsfilemediaio.h
        include/promeki/smpte302m.h
        include/promeki/jpegxsbitstream.h
        include/promeki/nvencvideoencoder.h
        include/promeki/nvdecvideodecoder.h
        include/promeki/imagefilemediaio.h
        include/promeki/quicktimemediaio.h
        include/promeki/ffmpegmediaio.h
        include/promeki/paintengine.h
        include/promeki/quicktime.h
        include/promeki/mediadesc.h
        include/promeki/mediaiodescription.h
        include/promeki/sdivpid.h
        include/promeki/sdiwireinference.h
        include/promeki/videotestpattern.h
        include/promeki/motionband.h
    )
    list(APPEND PROMEKI_SOURCES
        src/proav/aacbitstream.cpp
        src/proav/amf0.cpp
        src/proav/audiochannelmap.cpp
        src/proav/audiocodec.cpp
        src/proav/audioformat.cpp
        src/proav/audiolevel.cpp
        src/proav/h264bitstream.cpp
        src/proav/h264profilelevel.cpp
        src/proav/hevcbitstream.cpp
        src/proav/videoencodersei.cpp
        src/proav/pixelformat.cpp
        src/proav/pixelmemlayout.cpp
        src/proav/videocodec.cpp
        src/proav/videoformat.cpp
        src/proav/videoformatdetails.cpp
        src/proav/ancafd.cpp
        src/proav/ancatc.cpp
        src/proav/anccodec_afd.cpp
        src/proav/anccodec_atc.cpp
        src/proav/anccodec_atc_hfrtc.cpp
        src/proav/anccodec_cea608.cpp
        src/proav/anccodec_cea708.cpp
        src/proav/anccodec_cea708_hlssei.cpp
        src/proav/anccodec_hdrdynamic2094_40.cpp
        src/proav/anccodec_hdrdynamic2094_40_st291.cpp
        src/proav/anccodec_hdrstatic.cpp
        src/proav/anccodec_hdrstatic_st291.cpp
        src/proav/anccodec_op47sdp.cpp
        src/proav/anccodec_st2020audio.cpp
        src/proav/anccodec_vpid.cpp
        src/proav/ancdesc.cpp
        src/proav/ancdetails.cpp
        src/proav/ancformat.cpp
        src/proav/ancop47sdp.cpp
        src/proav/ancpacket.cpp
        src/proav/ancpayload.cpp
        src/proav/ancst2020audio.cpp
        src/proav/anctranslator.cpp
        src/proav/ancframesync.cpp
        src/proav/captiondecoder.cpp
        src/proav/captionencoder.cpp
        src/proav/cea608.cpp
        src/proav/cea608decoder.cpp
        src/proav/cea608encoder.cpp
        src/proav/cea608ext.cpp
        src/proav/cea608packet.cpp
        src/proav/cea608xds.cpp
        src/proav/cea608xdsinjector.cpp
        src/proav/cea708cdp.cpp
        src/proav/cea708decoder.cpp
        src/proav/cea708encoder.cpp
        src/proav/cea708ext.cpp
        src/proav/cea708service.cpp
        src/proav/cea708windowstate.cpp
        src/proav/hdmiinfoframe.cpp
        src/proav/hdrdynamic2094_40.cpp
        src/proav/hdrstaticmetadata.cpp
        src/proav/scc.cpp
        src/proav/st291packet.cpp
        src/proav/subrip.cpp
        src/proav/subtitle.cpp
        src/proav/subtitleburnmediaio.cpp
        src/proav/subtitlecuebuilder.cpp
        src/proav/subtitlerenderer.cpp
        src/proav/transcript.cpp
        src/proav/transcriptionengine.cpp
        src/proav/audioblock.cpp
        src/proav/audiobuffer.cpp
        src/proav/audiodecoder.cpp
        src/proav/audiodesc.cpp
        src/proav/audioencoder.cpp
        src/proav/audiofile.cpp
        src/proav/audiofilefactory.cpp
        src/proav/audiogen.cpp
        src/proav/audiotestpattern.cpp
        src/proav/videoencoder.cpp
        src/proav/videodecoder.cpp
        src/proav/encodeddesc.cpp
        src/proav/flvtag.cpp
        src/proav/frame.cpp
        src/proav/framesync.cpp
        src/proav/debugmediafile.cpp
        src/proav/debugmediamediaio.cpp
        src/proav/audiodataencoder.cpp
        src/proav/audiodatadecoder.cpp
        src/proav/imagedataencoder.cpp
        src/proav/imagedatadecoder.cpp
        src/proav/imagedesc.cpp
        src/proav/imagefile.cpp
        src/proav/imagefileio.cpp
        src/proav/imgseq.cpp
        src/proav/jpeggeometryprobe.cpp
        src/proav/imagefileio_rawyuv.cpp
        src/proav/imagefileio_dpx.cpp
        src/proav/imagefileio_cin.cpp
        src/proav/imagefileio_tga.cpp
        src/proav/imagefileio_sgi.cpp
        src/proav/imagefileio_pnm.cpp
        src/proav/ltcdecoder.cpp
        src/proav/ltcencoder.cpp
        src/proav/mediaio.cpp
        src/proav/mediaioallocator.cpp
        src/proav/mediaiofactory.cpp
        src/proav/commandmediaio.cpp
        src/proav/inlinemediaio.cpp
        src/proav/sharedthreadmediaio.cpp
        src/proav/dedicatedthreadmediaio.cpp
        src/proav/mediaiocommand.cpp
        src/proav/mediaioreadcache.cpp
        src/proav/mediaiostats.cpp
        src/proav/mediaiostatscollector.cpp
        src/proav/mediaioclock.cpp
        src/proav/mediaiodescription.cpp
        src/proav/mediaioport.cpp
        src/proav/mediaiosink.cpp
        src/proav/mediaiosource.cpp
        src/proav/mediaioportgroup.cpp
        src/proav/mediaioportconnection.cpp
        src/proav/mediapipeline.cpp
        src/proav/mediapipelineconfig.cpp
        src/proav/mediapipelinetrigger.cpp
        src/proav/mediapipelineplanner.cpp
        src/proav/mediapipelinestats.cpp
        src/proav/windowedstatsbundle.cpp
        src/proav/pipelineevent.cpp
        src/proav/mediapayload.cpp
        src/proav/uncompressedvideopayload.cpp
        src/proav/pcmaudiopayload.cpp
        src/proav/pcmsilencefiller.cpp
        src/proav/srcmediaio.cpp
        src/proav/burnmediaio.cpp
        src/proav/framesyncmediaio.cpp
        src/proav/videoencodermediaio.cpp
        src/proav/videodecodermediaio.cpp
        src/proav/audioencodermediaio.cpp
        src/proav/audiodecodermediaio.cpp
        src/proav/mpegts.cpp
        src/proav/mpegtsmuxer.cpp
        src/proav/mpegtsdemuxer.cpp
        src/proav/mpegtsframer.cpp
        src/proav/mpegtsfilemediaio.cpp
        src/proav/smpte302m.cpp
        src/proav/jpegxsbitstream.cpp
        src/proav/rawbitstreammediaio.cpp
        src/proav/imagefilemediaio.cpp
        src/proav/inspectormediaio.cpp
        src/proav/nullpacingmediaio.cpp
        src/proav/quicktimemediaio.cpp
        src/proav/tpgmediaio.cpp
        src/proav/paintengine.cpp
        src/proav/paintengine_422.cpp
        src/proav/paintengine_interleaved.cpp
        src/proav/paintengine_multiplane.cpp
        src/proav/paintengine_packed.cpp
        src/proav/quicktime.cpp
        src/proav/quicktime_atom.cpp
        src/proav/quicktime_reader.cpp
        src/proav/quicktime_writer.cpp
        src/proav/st436m.cpp
        src/proav/qtclosedcaption.cpp
        src/proav/mediadesc.cpp
        src/proav/sdivpid.cpp
        src/proav/sdiwireinference.cpp
        src/proav/videotestpattern.cpp
        src/proav/motionband.cpp
    )

    # Feature-gated proav sources
    if(PROMEKI_ENABLE_JPEG)
        # jpegvideocodec.cpp #includes <jpeglib.h> and calls libjpeg-turbo
        # directly, so the whole file (and its header) is gated on
        # PROMEKI_ENABLE_JPEG — there is no fallback without libjpeg.  The
        # codec self-registers into the VideoEncoder/VideoDecoder registries,
        # so when JPEG is off it is simply absent at runtime; consumers that
        # go through VideoCodec(VideoCodec::JPEG) (e.g. MjpegStreamMediaIO)
        # get a clean createEncoder failure rather than a link error.
        list(APPEND PROMEKI_HEADERS include/promeki/jpegvideocodec.h)
        list(APPEND PROMEKI_SOURCES
            src/proav/jpegvideocodec.cpp
            src/proav/imagefileio_jpeg.cpp
        )
    endif()
    if(PROMEKI_ENABLE_JPEGXS)
        # JPEG XS has no fallback — the entire file is gated, not just
        # the external linkage, because SvtJpegxs.h is only available
        # when the thirdparty library was configured in.
        list(APPEND PROMEKI_HEADERS
            include/promeki/jpegxsvideocodec.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/jpegxsvideocodec.cpp
            src/proav/imagefileio_jpegxs.cpp
        )
    endif()
    if(PROMEKI_ENABLE_NVENC)
        # NVENC backend includes the NVIDIA Video Codec SDK header
        # directly, so the whole file is gated — stubs are not useful
        # when nvEncodeAPI.h is missing.
        list(APPEND PROMEKI_SOURCES src/proav/nvencvideoencoder.cpp)
    endif()
    if(PROMEKI_ENABLE_X264)
        # libx264 (GPL) software H.264 encoder backend.  The whole file is
        # gated because it includes <x264.h> directly; without the vendored
        # or system libx264 there is no header to include.
        list(APPEND PROMEKI_SOURCES src/proav/x264videoencoder.cpp)
    endif()
    if(PROMEKI_ENABLE_OPUS)
        # Native libopus AudioEncoder / AudioDecoder backend.  Gated on
        # PROMEKI_ENABLE_OPUS because the file includes <opus/opus.h>
        # directly; without the vendored or system libopus there is no
        # header to include.
        list(APPEND PROMEKI_SOURCES src/proav/opusaudiocodec.cpp)
    endif()
    if(PROMEKI_ENABLE_AAC)
        # Native fdk-aac AudioEncoder / AudioDecoder backend.  Gated on
        # PROMEKI_ENABLE_AAC for the same reason as the Opus backend —
        # the file includes <fdk-aac/aacenc_lib.h> / <fdk-aac/aacdecoder_lib.h>
        # directly.
        list(APPEND PROMEKI_SOURCES src/proav/fdkaaccodec.cpp)
    endif()
    if(PROMEKI_ENABLE_FFMPEG)
        # Shared FFmpeg plumbing (av_log → promeki Logger bridge, AVERROR
        # formatter) used by every FFmpeg-backed backend.
        list(APPEND PROMEKI_SOURCES src/proav/ffmpegsupport.cpp)
        # Generic FFmpeg AudioEncoder / AudioDecoder backend (AC-3 / MP3 /
        # FLAC).  Gated on PROMEKI_ENABLE_FFMPEG because the file includes
        # <libavcodec/avcodec.h> etc. directly.
        list(APPEND PROMEKI_SOURCES src/proav/ffmpegaudiocodec.cpp)
        # Generic FFmpeg VideoEncoder / VideoDecoder backend: decode for
        # H.264 / HEVC / AV1 / VP9 / ProRes (the software "fall back to
        # FFmpeg" path) plus ProRes / MJPEG encode (LGPL-clean encoders).
        list(APPEND PROMEKI_SOURCES src/proav/ffmpegvideocodec.cpp)
        # Generic FFmpeg container MediaIO backend (libavformat muxers /
        # demuxers).  Native-first fallback: claims only formats no native
        # backend handles and is reachable explicitly via Type=FFmpeg.
        list(APPEND PROMEKI_SOURCES src/proav/ffmpegmediaio.cpp)
    endif()
    if(PROMEKI_ENABLE_WHISPER)
        # Vendored whisper.cpp TranscriptionEngine backend.  Includes
        # <whisper.h> directly, so the file is gated on
        # PROMEKI_ENABLE_WHISPER.
        list(APPEND PROMEKI_SOURCES src/proav/whispertranscriptionengine.cpp)
    endif()
    if(PROMEKI_ENABLE_NVDEC)
        # Same story as NVENC — the backend compiles only when the
        # Video Codec SDK headers (nvcuvid.h + cuviddec.h) are found.
        list(APPEND PROMEKI_SOURCES src/proav/nvdecvideodecoder.cpp)
    endif()
    if(PROMEKI_ENABLE_PNG)
        list(APPEND PROMEKI_SOURCES src/proav/imagefileio_png.cpp)
    endif()
    if(PROMEKI_ENABLE_FREETYPE)
        list(APPEND PROMEKI_SOURCES
            src/proav/font.cpp
            src/proav/fastfont.cpp
            src/proav/basicfont.cpp
        )
    endif()
    if(PROMEKI_ENABLE_AUDIO)
        list(APPEND PROMEKI_HEADERS include/promeki/audiofilemediaio.h)
        list(APPEND PROMEKI_SOURCES
            src/proav/audiofile_libsndfile.cpp
            src/proav/audiofilemediaio.cpp
        )
    endif()
    if(PROMEKI_ENABLE_SRC)
        list(APPEND PROMEKI_HEADERS include/promeki/audioresampler.h)
        list(APPEND PROMEKI_SOURCES src/proav/audioresampler.cpp)
    endif()
    if(PROMEKI_ENABLE_V4L2)
        list(APPEND PROMEKI_HEADERS
            include/promeki/v4l2mediaio.h
            include/promeki/v4l2m2mcodec.h
            include/promeki/v4l2rawformat.h
            include/promeki/v4l2codecparams.h
            include/promeki/v4l2captionsei.h
            include/promeki/v4l2videoencoder.h
            include/promeki/v4l2videodecoder.h)
        list(APPEND PROMEKI_SOURCES
            src/proav/v4l2mediaio.cpp
            src/proav/v4l2m2mcodec.cpp
            src/proav/v4l2rawformat.cpp
            src/proav/v4l2codecparams.cpp
            src/proav/v4l2captionsei.cpp
            src/proav/v4l2videoencoder.cpp
            src/proav/v4l2videodecoder.cpp)
    endif()
    if(PROMEKI_ENABLE_SRT)
        # SrtMediaIO: composes MpegTsFramer with SrtSocketTransport so
        # the same single-program MPEG-TS pipeline that drives
        # MpegTsFileMediaIO also rides over an SRT connection.
        list(APPEND PROMEKI_HEADERS include/promeki/srtmediaio.h)
        list(APPEND PROMEKI_SOURCES src/proav/srtmediaio.cpp)
    endif()
    if(PROMEKI_ENABLE_NDI)
        # NDI MediaIO backend: see docs/ndi.md.  The NDI SDK is loaded
        # at runtime via the dynamic-load shim in
        # Processing.NDI.DynamicLoad.h, so the build-time dependency
        # is headers-only (already wired into the promeki target).
        list(APPEND PROMEKI_HEADERS
            include/promeki/ndilib.h
            include/promeki/ndiformat.h
            include/promeki/ndidiscovery.h
            include/promeki/ndiclock.h
            include/promeki/ndimediaio.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/ndilib.cpp
            src/proav/ndiformat.cpp
            src/proav/ndidiscovery.cpp
            src/proav/ndiclock.cpp
            src/proav/ndimediaio.cpp
        )
    endif()
    if(PROMEKI_ENABLE_NTV2)
        # AJA NTV2 MediaIO backend: see devplan/proav/ntv2.md.  The
        # libajantv2 SDK is built as a vendored static archive
        # (thirdparty/libajantv2) and linked into promeki — see the
        # PROMEKI_ENABLE_NTV2 block earlier in this file.  Phase 1
        # provides single-channel SDI source capture.
        list(APPEND PROMEKI_HEADERS
            include/promeki/ntv2anc.h
            include/promeki/ntv2capabilities.h
            include/promeki/ntv2clock.h
            include/promeki/ntv2device.h
            include/promeki/ntv2format.h
            include/promeki/ntv2mediaio.h
            include/promeki/ntv2routing.h
            include/promeki/ntv2vpid.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/ntv2anc.cpp
            src/proav/ntv2capabilities.cpp
            src/proav/ntv2clock.cpp
            src/proav/ntv2device.cpp
            src/proav/ntv2format.cpp
            src/proav/ntv2mediaio.cpp
            src/proav/ntv2routing.cpp
            src/proav/ntv2vpid.cpp
        )
    endif()
    if(PROMEKI_ENABLE_CSC)
        list(APPEND PROMEKI_HEADERS
            include/promeki/cscpipeline.h
            include/promeki/csccontext.h
            include/promeki/cscregistry.h
            include/promeki/cscmediaio.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/csc/cscpipeline.cpp
            src/proav/csc/csccontext.cpp
            src/proav/csc/cscregistry.cpp
            src/proav/csc/unpack.cpp
            src/proav/csc/pack.cpp
            src/proav/csc/transfer.cpp
            src/proav/csc/hdrtransfer.cpp
            src/proav/csc/tonemap.cpp
            src/proav/csc/matrix.cpp
            src/proav/csc/chroma.cpp
            src/proav/csc/alpha.cpp
            src/proav/csc/fastpath.cpp
            src/proav/csc/st2110.cpp
            src/proav/cscmediaio.cpp
        )
    endif()
endif()

# Music sources
if(PROMEKI_ENABLE_MUSIC)
    list(APPEND PROMEKI_HEADERS
        include/promeki/midinote.h
        include/promeki/midinotenames.h
        include/promeki/musicalnote.h
        include/promeki/musicalscale.h
        include/promeki/notesequenceparser.h
    )
    list(APPEND PROMEKI_SOURCES
        src/music/midinote.cpp
        src/music/midinotenames.cpp
        src/music/musicalscale.cpp
        src/music/notesequenceparser.cpp
    )
endif()

add_library(promeki SHARED ${PROMEKI_SOURCES})
add_library(promeki::promeki ALIAS promeki)
target_link_libraries(promeki PRIVATE promeki::warnings)
set_target_properties(promeki PROPERTIES
    VERSION ${APP_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME promeki
    EXPORT_NAME promeki
)
promeki_split_debug(promeki)

# Vendored shared dependencies shipped beside libpromeki.so ###################
#
# The list of glob patterns naming the vendored .so families we build and
# ship (the LGPL trio + fdk-aac + the FLAC/Ogg/Vorbis/Opus codecs).  Built
# once here and reused for both (a) staging into the build tree's lib dir
# and (b) the install() rules further down.  Each is included only when
# built from vendored source — a system copy contributes nothing.
set(PROMEKI_SHIPPED_SO_PATTERNS "")
if(PROMEKI_ENABLE_AUDIO AND NOT PROMEKI_USE_SYSTEM_SNDFILE)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libsndfile.so*")
endif()
if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_MPG123)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libmpg123.so*")
endif()
if(PROMEKI_ENABLE_MP3 AND NOT PROMEKI_USE_SYSTEM_LAME)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libmp3lame.so*")
endif()
if(PROMEKI_ENABLE_AAC AND NOT PROMEKI_USE_SYSTEM_FDKAAC)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libfdk-aac.so*")
endif()
if(PROMEKI_ENABLE_FLAC AND NOT PROMEKI_USE_SYSTEM_FLAC)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libFLAC.so*")
endif()
if(PROMEKI_ENABLE_VORBIS AND NOT PROMEKI_USE_SYSTEM_VORBIS)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS
        "libvorbis.so*" "libvorbisenc.so*" "libvorbisfile.so*")
endif()
if((PROMEKI_ENABLE_FLAC OR PROMEKI_ENABLE_VORBIS) AND NOT PROMEKI_USE_SYSTEM_OGG)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libogg.so*")
endif()
if(PROMEKI_ENABLE_OPUS AND NOT PROMEKI_USE_SYSTEM_OPUS)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libopus.so*")
endif()
if(PROMEKI_ENABLE_FFMPEG AND NOT PROMEKI_USE_SYSTEM_FFMPEG)
    # Full vendored FFmpeg suite ships beside libpromeki.so (LGPL-2.1+).
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS
        "libavcodec.so*" "libavformat.so*" "libavutil.so*"
        "libavfilter.so*" "libavdevice.so*"
        "libswscale.so*" "libswresample.so*" "libpostproc.so*")
endif()
if(PROMEKI_ENABLE_X264 AND NOT PROMEKI_USE_SYSTEM_X264)
    list(APPEND PROMEKI_SHIPPED_SO_PATTERNS "libx264.so*")
endif()

# Stage those shared deps into build/lib after promeki links so the build
# tree mirrors the install layout — apps in build/bin then resolve the
# whole chain via $ORIGIN/../lib without reaching into the thirdparty
# prefix.  The EPs that produce these are upstream of promeki in the
# build graph (promeki links libsndfile/opus/fdk-aac, which depend on the
# rest), so the files exist by the time this POST_BUILD runs.  The SRT
# bundle is emitted into build/lib directly and so needs no staging.
#
# When split-debug is active, OBJCOPY + DEBUGDIR are passed too, so the
# stager strips each staged .so (--strip-unneeded) and saves its symbols
# into build/lib-debug.  When inactive they are empty and the stager just
# copies.
if(PROMEKI_SHIPPED_SO_PATTERNS)
    if(PROMEKI_SPLIT_DEBUG_ACTIVE)
        set(_promeki_stage_objcopy "${CMAKE_OBJCOPY}")
    else()
        set(_promeki_stage_objcopy "")
    endif()
    add_custom_command(TARGET promeki POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            -DSRC=${PROMEKI_THIRDPARTY_PREFIX}/lib
            -DDST=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}
            "-DPATTERNS=${PROMEKI_SHIPPED_SO_PATTERNS}"
            -DOBJCOPY=${_promeki_stage_objcopy}
            -DDEBUGDIR=${PROMEKI_DEBUG_OUTPUT_DIRECTORY}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/promeki_stage_shared_deps.cmake
        COMMENT "Staging vendored shared dependencies into ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
        VERBATIM)
endif()

# Include directories
target_include_directories(promeki PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# nlohmann-json include paths (headers use #include <nlohmann/json.hpp>)
if(PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    target_link_libraries(promeki PUBLIC nlohmann_json::nlohmann_json)
else()
    target_include_directories(promeki PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
endif()

# pugixml include paths (headers use #include <pugixml.hpp>)
# xml.h embeds pugi::xml_document by value inside XmlData, so consumers
# need the pugi headers on their include path. The static archive
# carries pugi's symbols into libpromeki; consumers do not link pugixml
# separately.
if(PROMEKI_USE_SYSTEM_PUGIXML)
    target_link_libraries(promeki PUBLIC ${PROMEKI_PUGIXML_TARGET})
else()
    target_include_directories(promeki PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pugixml/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
    target_link_libraries(promeki PRIVATE ${PROMEKI_PUGIXML_TARGET})
endif()

# libvtc include paths
if(PROMEKI_USE_SYSTEM_VTC)
    target_link_libraries(promeki PRIVATE ${PROMEKI_VTC_TARGET})
else()
    target_include_directories(promeki PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvtc/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty>
    )
    target_link_libraries(promeki PRIVATE ${PROMEKI_VTC_TARGET})
endif()

# cirf is a purely internal implementation detail: resource.h
# forward-declares the cirf types it exposes, so consumers of
# libpromeki do not need cirf headers on their include path and
# the cirf_runtime static archive does not need to participate
# in the promeki export set. Both links stay PRIVATE.
target_link_libraries(promeki PRIVATE ${PROMEKI_CIRF_RUNTIME_TARGET})

# mjansson/mdns is a header-only library used internally by the
# mDNS packet parser (src/network/mdnspacket.cpp); the include
# directory carried by the INTERFACE target reaches that one TU and
# never leaks into the promeki public surface.  Link stays PRIVATE.
if(PROMEKI_ENABLE_MDNS)
    target_link_libraries(promeki PRIVATE ${PROMEKI_MDNS_TARGET})
endif()

# Built-in :/.PROMEKI/ resource set. cirf_add_resources() was brought
# into scope above. The static library it creates is whole-archive
# linked into promeki via the explicit reference to
# &promeki_resources_root in builtinresources.cpp, which keeps the
# .o (and therefore the resource data) reachable.
if(PROMEKI_ENABLE_CIRF)
    cirf_add_resources(promeki_resources
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/res/resources.json
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/promeki_resources)
    # In a cross build the cirf generator is built as a host ExternalProject
    # (cirf_host); cirf_add_resources() orders every resource library after it
    # automatically via the CIRF_HOST_TARGET we set above — no per-call-site
    # add_dependencies() needed here or in the demos/utils.
    target_link_libraries(promeki PRIVATE promeki_resources)
    target_include_directories(promeki PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/promeki_resources)
    target_compile_definitions(promeki PRIVATE PROMEKI_HAVE_BUILTIN_RESOURCES)
    target_sources(promeki PRIVATE src/core/builtinresources.cpp)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
    target_link_libraries(promeki PUBLIC stdc++fs)
endif()
target_link_libraries(promeki PRIVATE m)
target_compile_definitions(promeki PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)

# Precompiled headers — front-load the hot stable headers so each incremental
# compile skips re-parsing them.  Limited to infrastructure that rarely
# changes: touching any input here invalidates the PCH and cascades to every
# TU in the target.  Deliberately excludes variant.h / metadata.h / etc.
# (changed often) and feature-area headers (changed when that feature is in
# active development).
set(PROMEKI_PCH_HEADERS
    # Standard library — widely used, expensive to parse, ABI-stable
    <cstdint>
    <cstddef>
    <cstring>
    <memory>
    <string>
    <string_view>
    <vector>
    <map>
    <functional>
    <chrono>
    <atomic>
    <type_traits>
    <utility>
    # promeki infrastructure — widely included, stable
    <promeki/namespace.h>
    <promeki/platform.h>
    <promeki/error.h>
    <promeki/util.h>
    <promeki/char.h>
    <promeki/string.h>
    <promeki/sharedptr.h>
    <promeki/uniqueptr.h>
    <promeki/list.h>
    <promeki/result.h>
    <promeki/pair.h>
)
if(PROMEKI_USE_PCH)
    target_precompile_headers(promeki PRIVATE ${PROMEKI_PCH_HEADERS})
    # Bust ccache when the baked-in config.h changes — see PROMEKI_CONFIG_SIG.
    target_compile_definitions(promeki PRIVATE PROMEKI_CONFIG_SIG=0x${PROMEKI_CONFIG_SIG})
endif()

# Vendored thirdparty include paths and link dependencies
if(PROMEKI_ENABLE_PROAV)
    # When using vendored deps, we need their include paths for compilation
    if(NOT PROMEKI_USE_SYSTEM_LIBJPEG AND PROMEKI_ENABLE_JPEG)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_SVT_JPEG_XS AND PROMEKI_ENABLE_JPEGXS)
        # SVT-JPEG-XS installs its public headers under include/svt-jpegxs/
        target_include_directories(promeki PRIVATE
            ${PROMEKI_THIRDPARTY_PREFIX}/include
            ${PROMEKI_THIRDPARTY_PREFIX}/include/svt-jpegxs
        )
    endif()
    if(NOT PROMEKI_USE_SYSTEM_LIBSPNG AND PROMEKI_ENABLE_PNG)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_SNDFILE AND PROMEKI_ENABLE_AUDIO)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_OPUS AND PROMEKI_ENABLE_OPUS)
        # Vendored opus installs its public headers under include/opus/.
        target_include_directories(promeki PRIVATE
            ${PROMEKI_THIRDPARTY_PREFIX}/include
            ${PROMEKI_THIRDPARTY_PREFIX}/include/opus
        )
    endif()
    if(NOT PROMEKI_USE_SYSTEM_SAMPLERATE AND PROMEKI_ENABLE_SRC)
        target_include_directories(promeki PRIVATE ${PROMEKI_THIRDPARTY_PREFIX}/include)
    endif()
    if(NOT PROMEKI_USE_SYSTEM_FREETYPE AND PROMEKI_ENABLE_FREETYPE)
        target_include_directories(promeki PRIVATE
            ${PROMEKI_THIRDPARTY_PREFIX}/include
            ${PROMEKI_THIRDPARTY_PREFIX}/include/freetype2
        )
    endif()

    if(PROMEKI_ENABLE_JPEG)
        target_link_libraries(promeki PRIVATE ${PROMEKI_JPEG_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_JPEG)
    endif()
    if(PROMEKI_ENABLE_JPEGXS)
        # SVT-JPEG-XS needs pthread + libm transitively; the imported
        # static target doesn't carry these so add them explicitly.
        find_package(Threads REQUIRED)
        target_link_libraries(promeki PRIVATE
            ${PROMEKI_JPEGXS_TARGET}
            Threads::Threads
            m
        )
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_JPEGXS)
    endif()
    if(PROMEKI_ENABLE_X264)
        # libx264 is a shared object shipped beside libpromeki.so (GPL).
        # Linking the imported target makes libx264-ep a build dependency so
        # the .so is produced and staged even before a consumer TU exists
        # (the encoder backend lands in a follow-up milestone).
        target_link_libraries(promeki PRIVATE ${PROMEKI_X264_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_X264)
    endif()
    if(PROMEKI_ENABLE_PNG)
        target_link_libraries(promeki PRIVATE ${PROMEKI_SPNG_TARGET} ${PROMEKI_ZLIB_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_PNG SPNG_STATIC)
    endif()
    if(PROMEKI_ENABLE_FREETYPE)
        target_link_libraries(promeki PRIVATE ${PROMEKI_FREETYPE_TARGET} ${PROMEKI_ZLIB_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_FREETYPE)
    endif()
    if(PROMEKI_ENABLE_AUDIO)
        target_link_libraries(promeki PRIVATE ${PROMEKI_SNDFILE_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_SNDFILE)
        # libsndfile is now a shared object and every codec backend it
        # uses (libFLAC, libogg, libvorbis*, libmpg123, libmp3lame) is
        # itself a shared object carried as a DT_NEEDED of libsndfile.so,
        # so they all reach libpromeki.so transitively — we link only
        # libsndfile here.  (This also drops the old explicit -lm:
        # libmp3lame's own shared link now pulls in the libm symbols its
        # psymodel needs.)  Should promeki ever call FLAC / Ogg / Vorbis
        # directly, add the corresponding PROMEKI_*_TARGET to this link
        # then; for now the feature flags just gate promeki code paths.
        if(PROMEKI_ENABLE_FLAC)
            target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_FLAC)
        endif()
        if(PROMEKI_ENABLE_VORBIS)
            target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_VORBIS)
        endif()
        if(PROMEKI_ENABLE_MP3)
            target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_MP3)
        endif()
    endif()
    if(PROMEKI_ENABLE_OPUS)
        target_link_libraries(promeki PRIVATE ${PROMEKI_OPUS_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_OPUS)
    endif()
    if(PROMEKI_ENABLE_AAC)
        target_link_libraries(promeki PRIVATE ${PROMEKI_FDKAAC_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_AAC)
    endif()
    if(PROMEKI_ENABLE_FFMPEG)
        target_link_libraries(promeki PRIVATE ${PROMEKI_FFMPEG_TARGETS})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_FFMPEG)
    endif()
    if(PROMEKI_ENABLE_WHISPER)
        # Vendored whisper.cpp installs its public headers at
        # ${PROMEKI_THIRDPARTY_PREFIX}/include (whisper.h + ggml*.h);
        # the include path is already attached to PROMEKI_WHISPER_TARGET
        # via SYSTEM INTERFACE.  Define PROMEKI_ENABLE_WHISPER on the
        # promeki TU compile line for the implementation file's #if
        # guard (it's also set globally via config.h, but a direct
        # define keeps the source file independent of generated config).
        target_link_libraries(promeki PRIVATE ${PROMEKI_WHISPER_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_WHISPER)
    endif()
    if(PROMEKI_ENABLE_SRC)
        target_link_libraries(promeki PRIVATE ${PROMEKI_SAMPLERATE_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_SRC)
    endif()
    if(PROMEKI_ENABLE_V4L2)
        target_link_libraries(promeki PRIVATE ALSA::ALSA)
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_V4L2)
    endif()
    if(PROMEKI_ENABLE_CUDA)
        # CUDA::cudart is the runtime API (dynamic link).  Every future
        # CUDA-gated file (nvenc backend, GPU CSC kernels, ...) pulls in
        # the same target, so we keep it PRIVATE to the promeki library.
        target_link_libraries(promeki PRIVATE CUDA::cudart)
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_CUDA)
    endif()
    if(PROMEKI_ENABLE_NVENC)
        # NVENC needs the CUDA driver API (for CUcontext) and dlopen
        # to pull libnvidia-encode.so at runtime.  The NVENC SDK is a
        # headers-only dependency; there is no link library involved.
        target_include_directories(promeki PRIVATE ${PROMEKI_NVENC_INCLUDE_DIR})
        target_link_libraries(promeki PRIVATE CUDA::cuda_driver ${CMAKE_DL_LIBS})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_NVENC)
    endif()
    if(PROMEKI_ENABLE_NVDEC)
        # Same story: headers come from the Video Codec SDK directory,
        # libnvcuvid.so.1 is loaded at runtime via dlopen.
        target_include_directories(promeki PRIVATE ${PROMEKI_NVENC_INCLUDE_DIR})
        target_link_libraries(promeki PRIVATE CUDA::cuda_driver ${CMAKE_DL_LIBS})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_NVDEC)
    endif()
    if(PROMEKI_ENABLE_NDI)
        # NDI ships a dynamic-load shim (Processing.NDI.DynamicLoad.h)
        # that pulls libndi.so / libndi_advanced.so at runtime, so the
        # SDK is a headers-only build dependency.  dl is needed for the
        # dlopen calls inside the shim.
        target_include_directories(promeki PRIVATE ${PROMEKI_NDI_INCLUDE_DIR})
        target_link_libraries(promeki PRIVATE ${CMAKE_DL_LIBS})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_NDI)
        # When the SDK probe found a runtime tree under the SDK dir,
        # pass the absolute path through so NdiLib::instance() can
        # dlopen it directly — saves users from having to set
        # LD_LIBRARY_PATH for dev builds against an extracted SDK
        # that hasn't been installed system-wide.
        if(PROMEKI_NDI_RUNTIME_DIR)
            target_compile_definitions(promeki PRIVATE
                PROMEKI_NDI_RUNTIME_DIR_STR="${PROMEKI_NDI_RUNTIME_DIR}")
        endif()
    endif()
    if(PROMEKI_ENABLE_NTV2)
        # libajantv2 is a fully static, in-tree dependency — public
        # includes (ajaanc/includes, ajabase, ajantv2/includes, ...) and
        # platform link deps (rt, pthread, dl, udev on Linux) ride on the
        # `ajantv2` target's INTERFACE properties, so nothing extra needs
        # to be wired up here.
        target_link_libraries(promeki PRIVATE ${PROMEKI_NTV2_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_NTV2)
    endif()
    if(PROMEKI_ENABLE_CSC)
        target_link_libraries(promeki PRIVATE ${PROMEKI_HWY_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_CSC)
        # Highway foreach_target needs to resolve includes relative to source root
        target_include_directories(promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
endif()

# HTTP stack — depends only on PROMEKI_ENABLE_NETWORK, so it lives
# outside the PROAV-conditional block above.  llhttp is linked PRIVATE
# (it's an implementation-detail parser; the public surface is purely
# our own HttpRequest / HttpResponse / HttpServer types).
if(PROMEKI_ENABLE_HTTP)
    target_link_libraries(promeki PRIVATE ${PROMEKI_LLHTTP_TARGET})
    target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_HTTP)
endif()

# TLS stack — same shape as HTTP (network-only, PRIVATE link).
# mbedTLS: link the SSL/TLS layer last so the linker resolves
# x509 / crypto symbols that the SSL layer references.
if(PROMEKI_ENABLE_TLS)
    target_link_libraries(promeki PRIVATE
        ${PROMEKI_MBEDTLS_TARGET}
        ${PROMEKI_MBEDX509_TARGET}
        ${PROMEKI_MBEDCRYPTO_TARGET}
    )
    target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_TLS)
endif()

# SRT stack — depends only on PROMEKI_ENABLE_NETWORK + _TLS.  The
# vendored bundle is now a standalone shared object (libpromeki_srt.so)
# with its private mbedTLS-3.6 sealed inside via --exclude-libs (see
# promeki_srt_bundle.cmake).  promeki links it PRIVATE, gaining a
# DT_NEEDED on libpromeki_srt.so resolved at runtime through the
# $ORIGIN RPATH — SRT is an implementation detail of our network
# module, not part of the public API.  No --exclude-libs is needed on
# *this* link any more: the bundle is no longer absorbed as an archive
# into libpromeki.so, so neither srt_* nor mbedTLS-3.6 symbols can
# reach libpromeki.so's own dynsym in the first place.
if(PROMEKI_ENABLE_SRT)
    target_link_libraries(promeki PRIVATE ${PROMEKI_SRT_TARGET})
    target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_SRT)
endif()

# TUI Library ##################################################################

if(PROMEKI_BUILD_TUI)
    set(PROMEKI_TUI_HEADERS
        include/promeki/tui/tuisubsystem.h
        include/promeki/tui/button.h
        include/promeki/tui/checkbox.h
        include/promeki/tui/frame.h
        include/promeki/tui/inputparser.h
        include/promeki/tui/label.h
        include/promeki/tui/layout.h
        include/promeki/tui/lineedit.h
        include/promeki/tui/listview.h
        include/promeki/tui/menu.h
        include/promeki/tui/painter.h
        include/promeki/tui/palette.h
        include/promeki/tui/progressbar.h
        include/promeki/tui/screen.h
        include/promeki/tui/scrollarea.h
        include/promeki/tui/splitter.h
        include/promeki/tui/statusbar.h
        include/promeki/tui/style.h
        include/promeki/tui/tabwidget.h
        include/promeki/tui/textarea.h
        include/promeki/tui/widget.h
    )

    set(PROMEKI_TUI_SOURCES
        src/tui/tuisubsystem.cpp
        src/tui/button.cpp
        src/tui/checkbox.cpp
        src/tui/frame.cpp
        src/tui/inputparser.cpp
        src/tui/label.cpp
        src/tui/lineedit.cpp
        src/tui/listview.cpp
        src/tui/menu.cpp
        src/tui/painter.cpp
        src/tui/palette.cpp
        src/tui/progressbar.cpp
        src/tui/screen.cpp
        src/tui/scrollarea.cpp
        src/tui/splitter.cpp
        src/tui/statusbar.cpp
        src/tui/style.cpp
        src/tui/tabwidget.cpp
        src/tui/textarea.cpp
        src/tui/widget.cpp
    )

    add_library(promeki-tui SHARED ${PROMEKI_TUI_SOURCES})
    add_library(promeki::tui ALIAS promeki-tui)
    set_target_properties(promeki-tui PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-tui
        EXPORT_NAME tui
    )
    target_include_directories(promeki-tui PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(promeki-tui PUBLIC promeki)
    target_link_libraries(promeki-tui PRIVATE promeki::warnings)
    target_compile_definitions(promeki-tui PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
    promeki_split_debug(promeki-tui)
endif()

# SDL Library ##################################################################

if(PROMEKI_BUILD_SDL)
    find_package(SDL3 REQUIRED)

    set(PROMEKI_SDL_HEADERS
        include/promeki/sdl/sdlsubsystem.h
        include/promeki/sdl/sdlaudioclock.h
        include/promeki/sdl/sdlaudiooutput.h
        include/promeki/sdl/sdleventpump.h
        include/promeki/sdl/sdlplayer.h
        include/promeki/sdl/sdlplayerwidget.h
        include/promeki/sdl/sdlvideowidget.h
        include/promeki/sdl/sdlwindow.h
    )

    set(PROMEKI_SDL_SOURCES
        src/sdl/sdlsubsystem.cpp
        src/sdl/sdlaudiooutput.cpp
        src/sdl/sdlaudioclock.cpp
        src/sdl/sdleventpump.cpp
        src/sdl/sdlplayer.cpp
        src/sdl/sdlplayerwidget.cpp
        src/sdl/sdlvideowidget.cpp
        src/sdl/sdlwindow.cpp
    )

    add_library(promeki-sdl SHARED ${PROMEKI_SDL_SOURCES})
    add_library(promeki::sdl ALIAS promeki-sdl)
    set_target_properties(promeki-sdl PROPERTIES
        VERSION ${APP_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        OUTPUT_NAME promeki-sdl
        EXPORT_NAME sdl
    )
    target_include_directories(promeki-sdl PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(promeki-sdl PUBLIC promeki SDL3::SDL3)
    target_link_libraries(promeki-sdl PRIVATE promeki::warnings)
    target_compile_definitions(promeki-sdl PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
    promeki_split_debug(promeki-sdl)
endif()

# Utility Applications #########################################################

if(PROMEKI_BUILD_UTILS)
    add_subdirectory(utils)
endif()

# Demonstration Applications ###################################################

if(PROMEKI_BUILD_DEMOS)
    add_subdirectory(demos)
endif()

# Functional Tests #############################################################
#
# Always configured, never part of the default build.  Targets inside
# tests/func/ inherit EXCLUDE_FROM_ALL from this add_subdirectory call,
# so `cmake --build <dir>` leaves them alone but
# `cmake --build <dir> --target <name>-functest` still builds one on
# demand.  See tests/func/CMakeLists.txt for the full rationale.
add_subdirectory(tests/func EXCLUDE_FROM_ALL)

# Install ######################################################################

# License and attribution notices
install(FILES
    LICENSE
    THIRD-PARTY-LICENSES
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

# Main library
install(TARGETS promeki
    EXPORT promeki-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Vendored shared dependencies that ship beside libpromeki.so ##################
#
# The LGPL trio (libsndfile / libmpg123 / libmp3lame) and libfdk-aac are
# built as their own shared objects so an end user can drop in a modified
# build and relink — satisfying the LGPL / FDK relink expectations that a
# statically-fused copy could not.  They live in the same lib dir as
# libpromeki.so and are found at runtime through its $ORIGIN RPATH.
#
# These are IMPORTED targets (not built by install(TARGETS)), so we copy
# them with install(DIRECTORY ... PATTERN), which preserves the real-file →
# SONAME → linker-name symlink chain.  PROMEKI_SHIPPED_SO_PATTERNS is the
# same list used to stage them into build/lib (built where the promeki
# target is defined); a library is shipped only when built from vendored
# source, so for a system copy the source dir simply has nothing matching
# and that pattern is a no-op.
#
# Source dir: when split-debug is active we install the already-stripped
# staged copies from build/lib (so installed vendored libs are stripped too,
# and their .debug files ship via the debug component below); otherwise we
# take the pristine originals straight from the thirdparty prefix.  Both
# directories carry the identical symlink chain.
if(PROMEKI_SPLIT_DEBUG_ACTIVE)
    set(_promeki_shipped_so_src ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
else()
    set(_promeki_shipped_so_src ${PROMEKI_THIRDPARTY_PREFIX}/lib)
endif()
foreach(_pat ${PROMEKI_SHIPPED_SO_PATTERNS})
    install(DIRECTORY ${_promeki_shipped_so_src}/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        FILES_MATCHING PATTERN "${_pat}")
endforeach()

# The SRT bundle is a single plain-SONAME shared object emitted straight
# into the library output dir (no symlink chain), so install it directly.
if(PROMEKI_ENABLE_SRT AND NOT PROMEKI_USE_SYSTEM_SRT)
    install(FILES ${PROMEKI_SRT_BUNDLE_SO}
        DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Split-out debug symbols (build/lib-debug/*.debug).  Every installed
# library — promeki itself, the vendored deps, and the SRT bundle — is
# already stripped (install(TARGETS) copies the post-build .so; the vendored
# / SRT installs source the stripped build/lib copies above).  Ship the
# matching .debug files into <libdir>/debug as a separate "debug" component
# that is EXCLUDE_FROM_ALL — a default `cmake --install` stays lean;
# `cmake --install . --component debug` pulls them in when wanted.
if(PROMEKI_SPLIT_DEBUG_ACTIVE)
    install(DIRECTORY ${PROMEKI_DEBUG_OUTPUT_DIRECTORY}/
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/debug
        COMPONENT debug
        EXCLUDE_FROM_ALL
        FILES_MATCHING PATTERN "*.debug")
endif()

# Install headers (flat under promeki/, excluding tui and sdl subdirs)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
    FILES_MATCHING PATTERN "*.h"
    PATTERN "tui" EXCLUDE
    PATTERN "sdl" EXCLUDE
    PATTERN "*.h.in" EXCLUDE
)

# Install generated headers.  These live under the build tree, not the
# source tree, so the source-dir glob above does not see them — they must
# be installed explicitly.  buildident.h carries PROMEKI_BUILD_IDENT and
# the PROMEKI_VERIFY_BUILD_IDENT_OR_ABORT() macro that downstream apps
# include to detect stale binaries, so it has to ship with the package.
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/buildident.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
)

# Install vendored nlohmann headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include/nlohmann
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
    )
endif()

# Install vendored pugixml headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_PUGIXML)
    install(FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pugixml/src/pugixml.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pugixml/src/pugiconfig.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
    )
endif()

# Install vendored libvtc headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_VTC)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libvtc/include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
    )
endif()

# Install doctest headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/doctest
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/thirdparty
)

# Export main targets
install(EXPORT promeki-targets
    FILE promeki-targets.cmake
    NAMESPACE promeki::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

# TUI library
if(PROMEKI_BUILD_TUI)
    install(TARGETS promeki-tui
        EXPORT promeki-tui-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_TUI_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/tui
    )
    install(EXPORT promeki-tui-targets
        FILE promeki-tui-targets.cmake
        NAMESPACE promeki::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
    )
endif()

# SDL library
if(PROMEKI_BUILD_SDL)
    install(TARGETS promeki-sdl
        EXPORT promeki-sdl-targets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(FILES ${PROMEKI_SDL_HEADERS}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki/sdl
    )
    install(EXPORT promeki-sdl-targets
        FILE promeki-sdl-targets.cmake
        NAMESPACE promeki::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
    )
endif()

# Package config files
configure_package_config_file(
    cmake/promekiConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/promekiConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/promeki
)

# Unit Tests ###################################################################

if(PROMEKI_BUILD_TESTS)
    set(UNITTEST_SOURCES
        tests/unit/doctest_main.cpp
        tests/unit/algorithm.cpp
        tests/unit/ansistream.cpp
        tests/unit/application.cpp
        tests/unit/array.cpp
        tests/unit/asyncbufferqueue.cpp
        tests/unit/atomic.cpp
        tests/unit/backendweight.cpp
        tests/unit/base64.cpp
        tests/unit/basicthread.cpp
        tests/unit/benchmarkrunner.cpp
        tests/unit/buffer.cpp
        tests/unit/bufferallocator.cpp
        tests/unit/buffercommand.cpp
        tests/unit/buffercopy.cpp
        tests/unit/bufferediodevice.cpp
        tests/unit/bufferiodevice.cpp
        tests/unit/buffermap.cpp
        tests/unit/bufferpool.cpp
        tests/unit/bufferview.cpp
        tests/unit/memdomain.cpp
        tests/unit/buildinfo.cpp
        tests/unit/bytearray.cpp
        tests/unit/ciepoint.cpp
        tests/unit/ciewavelengthtable.cpp
        tests/unit/cmdlineparser.cpp
        tests/unit/color.cpp
        tests/unit/colormodel.cpp
        tests/unit/cadence.cpp
        tests/unit/configoption.cpp
        tests/unit/crashhandler.cpp
        tests/unit/clock.cpp
        tests/unit/clockdomain.cpp
        tests/unit/cpumonitor.cpp
        tests/unit/crc.cpp
        tests/unit/cuda.cpp
        # tests/unit/datatype.cpp — exercises the legacy PROMEKI_DECLARE/
        # IMPLEMENT_DATATYPE macros which were replaced by the new
        # PROMEKI_DATATYPE in-class macro.  Coverage moved to
        # tests/unit/datastream_promeki_datatype.cpp; the legacy file is
        # left in-tree for the migration window so its assertions can be
        # ported as time allows.
        tests/unit/datastream_promeki_datatype.cpp
        tests/unit/datetime.cpp
        tests/unit/deque.cpp
        tests/unit/dir.cpp
        tests/unit/duration.cpp
        tests/unit/elapsedtimer.cpp
        tests/unit/enum.cpp
        tests/unit/enumlist.cpp
        tests/unit/env.cpp
        tests/unit/error.cpp
        tests/unit/event.cpp
        tests/unit/eventloop.cpp
        tests/unit/eventloop_stats.cpp
        tests/unit/file.cpp
        tests/unit/fileformatfactory.cpp
        tests/unit/fileinfo.cpp
        tests/unit/fileiodevice.cpp
        tests/unit/filepath.cpp
        tests/unit/fnv1a.cpp
        tests/unit/fourcc.cpp
        tests/unit/framecount.cpp
        tests/unit/framenumber.cpp
        tests/unit/framerate.cpp
        tests/unit/function.cpp
        tests/unit/future.cpp
        tests/unit/gate.cpp
        tests/unit/hashmap.cpp
        tests/unit/hashset.cpp
        tests/unit/hexdump.cpp
        tests/unit/histogram.cpp
        tests/unit/hmac.cpp
        tests/unit/iodevice.cpp
        tests/unit/json.cpp
        tests/unit/line.cpp
        tests/unit/libraryoptions.cpp
        tests/unit/list.cpp
        tests/unit/logger.cpp
        tests/unit/map.cpp
        tests/unit/matrix.cpp
        tests/unit/matrix3x3.cpp
        tests/unit/mediaduration.cpp
        tests/unit/mediatimestamp.cpp
        tests/unit/md5.cpp
        tests/unit/dmabufbufferimpl.cpp
        tests/unit/dmaheap.cpp
        tests/unit/memfdbufferimpl.cpp
        tests/unit/memfdregion.cpp
        tests/unit/mempool.cpp
        tests/unit/memspace.cpp
        tests/unit/metadata.cpp
        tests/unit/numa.cpp
        tests/unit/numahostbufferimpl.cpp
        tests/unit/pinnedhostbufferimpl.cpp
        tests/unit/mutex.cpp
        tests/unit/numname.cpp
        tests/unit/obfuscatedstring.cpp
        tests/unit/objectbase.cpp
        tests/unit/objectbase2.cpp
        tests/unit/objectbasepointer_race.cpp
        tests/unit/once.cpp
        tests/unit/optional.cpp
        tests/unit/periodiccallback.cpp
        tests/unit/pair.cpp
        tests/unit/pidcontroller.cpp
        tests/unit/point.cpp
        tests/unit/priorityqueue.cpp
        tests/unit/process.cpp
        tests/unit/queue.cpp
        tests/unit/random.cpp
        tests/unit/rational.cpp
        tests/unit/ratetracker.cpp
        tests/unit/readwritelock.cpp
        tests/unit/rect.cpp
        tests/unit/regex.cpp
        tests/unit/resource.cpp
        tests/unit/result.cpp
        tests/unit/securebuffer.cpp
        tests/unit/selfpipe.cpp
        tests/unit/set.cpp
        tests/unit/sha1.cpp
        tests/unit/sha2.cpp
        tests/unit/sharedmemory.cpp
        tests/unit/sharedptr.cpp
        tests/unit/signal.cpp
        tests/unit/signalhandler.cpp
        tests/unit/size2d.cpp
        tests/unit/span.cpp
        tests/unit/stack.cpp
        tests/unit/statsaccumulator.cpp
        tests/unit/streamstring.cpp
        tests/unit/string.cpp
        tests/unit/stringiodevice.cpp
        tests/unit/stringlist.cpp
        tests/unit/stringregistry.cpp
        tests/unit/strand.cpp
        tests/unit/structdatabase.cpp
        tests/unit/system.cpp
        tests/unit/terminal.cpp
        tests/unit/textstream.cpp
        tests/unit/textwrap.cpp
        tests/unit/thread.cpp
        tests/unit/threadpool.cpp
        tests/unit/timecode.cpp
        tests/unit/timecodegenerator.cpp
        tests/unit/timecodeuserbits.cpp
        tests/unit/timestamp.cpp
        tests/unit/umid.cpp
        tests/unit/uniqueptr.cpp
        tests/unit/units.cpp
        tests/unit/url.cpp
        tests/unit/uuid.cpp
        tests/unit/hdmisignalconfig.cpp
        tests/unit/sdioutputfanoutconfig.cpp
        tests/unit/sdisignalconfig.cpp
        tests/unit/sdistandards.cpp
        tests/unit/variantlookup.cpp
        tests/unit/variantquery.cpp
        tests/unit/videoportref.cpp
        tests/unit/videoreferenceconfig.cpp
        tests/unit/waitcondition.cpp
        tests/unit/windowedstat.cpp
        tests/unit/xml.cpp
        tests/unit/xyzcolor.cpp
    )

    if(PROMEKI_ENABLE_NETWORK)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/abstractsocket.cpp
            tests/unit/network/dnscache.cpp
            tests/unit/network/dnsconfig.cpp
            tests/unit/network/dnsname.cpp
            tests/unit/network/dnspacket.cpp
            tests/unit/network/dnsresolver.cpp
            tests/unit/network/ipv4address.cpp
            tests/unit/network/ipv6address.cpp
            tests/unit/network/eui64.cpp
            tests/unit/network/klvframe.cpp
            tests/unit/network/localsocket.cpp
            tests/unit/network/loopbacktransport.cpp
            tests/unit/network/macaddress.cpp
            tests/unit/network/multicastmanager.cpp
            tests/unit/network/multicastreceiver.cpp
            tests/unit/network/networkaddress.cpp
            tests/unit/network/networkinterface.cpp
            tests/unit/network/networkinterfacemonitor.cpp
            tests/unit/network/networkrouting.cpp
            tests/unit/network/prioritysocket.cpp
            tests/unit/network/packetscheduler.cpp
            tests/unit/network/packetdemux.cpp
            tests/unit/network/pcapsdpmap.cpp
            tests/unit/network/pcapflowrouter.cpp
            tests/unit/network/pcapreader.cpp
            tests/unit/network/rawsocket.cpp
            tests/unit/network/rfc7273refclk.cpp
            tests/unit/network/rtcppacket.cpp
            tests/unit/network/rtcpscheduler.cpp
            tests/unit/network/rtmpchunkstream.cpp
            tests/unit/network/rtmpclient.cpp
            tests/unit/network/rtmphandshake.cpp
            tests/unit/network/rtmpmediaio.cpp
            tests/unit/network/rtmpsession.cpp
            tests/unit/network/rtpaggregatorthread.cpp
            tests/unit/network/rtpancdepacketizerthread.cpp
            tests/unit/network/rtpancpacketizerthread.cpp
            tests/unit/network/rtpaudiodepacketizerthread.cpp
            tests/unit/network/rtpaudiopacketizerthread.cpp
            tests/unit/network/rtpaudiotxthread.cpp
            tests/unit/network/rtpdatadepacketizerthread.cpp
            tests/unit/network/rtpmediaio.cpp
            tests/unit/network/rtppacket.cpp
            tests/unit/network/rtppacketbatch.cpp
            tests/unit/network/rtppayload.cpp
            tests/unit/network/rtppayloadanc.cpp
            tests/unit/network/rtppayloadrawvideo.cpp
            tests/unit/network/jxsmarker.cpp
            tests/unit/network/st2110tx.cpp
            tests/unit/network/st2110trafficcalc.cpp
            tests/unit/network/st2110video.cpp
            tests/unit/network/st2110vrxmonitor.cpp
            tests/unit/network/phcclock.cpp
            tests/unit/network/rtpseqreorderbuffer.cpp
            tests/unit/network/rtpseqtracker.cpp
            tests/unit/network/rtpmediaclock.cpp
            tests/unit/jxsenums.cpp
            tests/unit/rtpmediaclkmode.cpp
            tests/unit/network/rtpsession.cpp
            tests/unit/network/rtpstreamclock.cpp
            tests/unit/network/rtpvideodepacketizerthread.cpp
            tests/unit/network/rxpayloadbundle.cpp
            tests/unit/network/sdpsession.cpp
            tests/unit/network/socketaddress.cpp
            tests/unit/network/tcpserver.cpp
            tests/unit/network/tcpsocket.cpp
            tests/unit/network/udpsocket.cpp
            tests/unit/network/udpsockettransport.cpp
        )
    endif()

    if(PROMEKI_ENABLE_TLS)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/sslcontext.cpp
            tests/unit/network/sslsocket.cpp
        )
        if(PROMEKI_ENABLE_HTTP)
            list(APPEND UNITTEST_SOURCES
                tests/unit/network/httptls.cpp
            )
        endif()
    endif()

    if(PROMEKI_ENABLE_SRT)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/srtsocket.cpp
        )
    endif()

    if(PROMEKI_ENABLE_MDNS)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/mdnsbrowser.cpp
            tests/unit/network/mdnsmanager.cpp
            tests/unit/network/mdnsname.cpp
            tests/unit/network/mdnspacket.cpp
            tests/unit/network/mdnspublisher.cpp
            tests/unit/network/mdnsrecord.cpp
            tests/unit/network/mdnsresolver.cpp
            tests/unit/network/mdnsservicetype.cpp
            tests/unit/network/mdnstxtrecord.cpp
            tests/unit/network/mdnsserviceinstance.cpp
            tests/unit/network/mdnstypebrowser.cpp
        )
    endif()

    if(PROMEKI_ENABLE_HTTP)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/debugserver.cpp
            tests/unit/network/httpapi.cpp
            tests/unit/network/httpclient.cpp
            tests/unit/network/httpconnection_async.cpp
            tests/unit/network/httpheaders.cpp
            tests/unit/network/httpmethod.cpp
            tests/unit/network/httpstatus.cpp
            tests/unit/network/httprequest.cpp
            tests/unit/network/httpresponse.cpp
            tests/unit/network/httprouter.cpp
            tests/unit/network/httpserver.cpp
            tests/unit/network/httpfilehandler.cpp
            tests/unit/network/websocket.cpp
        )
    endif()

    if(PROMEKI_ENABLE_PROAV)
        list(APPEND UNITTEST_SOURCES
            tests/unit/codectesthelpers.cpp
            tests/unit/datastream.cpp
            tests/unit/datastream_include_chain.cpp
            # tests/unit/datatype_roundtrip.cpp — exercises the legacy
            # PROMEKI_IMPLEMENT_DATATYPE macros which were replaced.
            # New coverage in tests/unit/datastream_promeki_datatype.cpp.
            tests/unit/typeregistry_isolation.cpp
            tests/unit/variant.cpp
            tests/unit/variantdatabase.cpp
            tests/unit/variantspec.cpp
            tests/unit/amf0.cpp
            tests/unit/audiocodec.cpp
            tests/unit/audioformat.cpp
            tests/unit/audiolevel.cpp
            tests/unit/audiomarker.cpp
            tests/unit/h264bitstream.cpp
            tests/unit/h264profilelevel.cpp
            tests/unit/hevcbitstream.cpp
            tests/unit/videoencodersei.cpp
            tests/unit/pixelmemlayout.cpp
            tests/unit/videoformat.cpp
            tests/unit/videoformatdetails.cpp
            tests/unit/anccodec_afd.cpp
            tests/unit/anccodec_atc.cpp
            tests/unit/anccodec_atc_hfrtc.cpp
            tests/unit/anccodec_cea608.cpp
            tests/unit/anccodec_cea708.cpp
            tests/unit/anccodec_cea708_hlssei.cpp
            tests/unit/anccodec_hdrdynamic2094_40.cpp
            tests/unit/anccodec_hdrdynamic2094_40_st291.cpp
            tests/unit/anccodec_hdrstatic.cpp
            tests/unit/anccodec_hdrstatic_st291.cpp
            tests/unit/anccodec_op47sdp.cpp
            tests/unit/anccodec_st2020audio.cpp
            tests/unit/anccodec_vpid.cpp
            tests/unit/ancdesc.cpp
            tests/unit/ancdetails.cpp
            tests/unit/ancformat.cpp
            tests/unit/ancframesync.cpp
            tests/unit/ancpacket.cpp
            tests/unit/ancpayload.cpp
            tests/unit/anc_roundtrip_property.cpp
            tests/unit/anctranslateconfig.cpp
            tests/unit/anctranslator.cpp
            tests/unit/anctranslator_syncpolicy.cpp
            tests/unit/cea608.cpp
            tests/unit/cea608decoder.cpp
            tests/unit/cea608encoder.cpp
            tests/unit/cea608ext.cpp
            tests/unit/cea608packet.cpp
            tests/unit/cea608xds.cpp
            tests/unit/cea608xdsinjector.cpp
            tests/unit/cea708cdp.cpp
            tests/unit/cea708decoder.cpp
            tests/unit/cea708encoder.cpp
            tests/unit/cea708ext.cpp
            tests/unit/cea708service.cpp
            tests/unit/cea708_subrip_roundtrip.cpp
            tests/unit/cea708windowstate.cpp
            tests/unit/framesync.cpp
            tests/unit/framesyncdisposition.cpp
            tests/unit/hdmiinfoframe.cpp
            tests/unit/hdrdynamic2094_40.cpp
            tests/unit/hdrstaticmetadata.cpp
            tests/unit/inspector_ancdata.cpp
            tests/unit/scc.cpp
            tests/unit/st291packet.cpp
            tests/unit/st436m.cpp
            tests/unit/qtclosedcaption.cpp
            tests/unit/subrip.cpp
            tests/unit/subtitleburnmediaio.cpp
            tests/unit/subtitlecuebuilder.cpp
            tests/unit/subtitlerenderer.cpp
            tests/unit/transcriptionengine.cpp
            tests/unit/tpg_anc_captions.cpp
            tests/unit/windowedstatsbundle.cpp
            tests/unit/audioblock.cpp
            tests/unit/audiobuffer.cpp
            tests/unit/audiochannelmap.cpp
            tests/unit/audioconformancelevel.cpp
            tests/unit/audiodesc.cpp
            tests/unit/audioencoder.cpp
            tests/unit/aacbitstream.cpp
            tests/unit/audiofilefactory.cpp
            $<$<BOOL:${PROMEKI_ENABLE_AUDIO}>:tests/unit/audiofile_codecs.cpp>
            tests/unit/audiotestpattern.cpp
            tests/unit/pcmsilencefiller.cpp
            $<$<BOOL:${PROMEKI_ENABLE_OPUS}>:tests/unit/opusaudiocodec.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_AAC}>:tests/unit/fdkaaccodec.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_FFMPEG}>:tests/unit/ffmpegaudiocodec.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_FFMPEG}>:tests/unit/ffmpegvideocodec.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_FFMPEG}>:tests/unit/ffmpegmediaio.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_WHISPER}>:tests/unit/whispertranscriptionengine.cpp>
            tests/unit/encodeddesc.cpp
            tests/unit/flvtag.cpp
            $<$<BOOL:${PROMEKI_ENABLE_FREETYPE}>:tests/unit/fastfont.cpp>
            $<$<BOOL:${PROMEKI_ENABLE_FREETYPE}>:tests/unit/basicfont.cpp>
            tests/unit/debugmediafile.cpp
            tests/unit/frame.cpp
            tests/unit/framebridge.cpp
            tests/unit/audiodataencoder.cpp
            tests/unit/audiodatadecoder.cpp
            tests/unit/imagedataencoder.cpp
            tests/unit/imagedatadecoder.cpp
            tests/unit/imagedesc.cpp
            tests/unit/imagefile.cpp
            tests/unit/imagefileio.cpp
            tests/unit/imagefileio_dpx.cpp
            tests/unit/imagefileio_cin.cpp
            tests/unit/imagefileio_tga.cpp
            tests/unit/imagefileio_sgi.cpp
            tests/unit/imagefileio_pnm.cpp
            tests/unit/imgseq.cpp
            tests/unit/jpeggeometryprobe.cpp
            tests/unit/ltcdecoder.cpp
            tests/unit/ltcencoder.cpp
            tests/unit/mediadesc.cpp
            tests/unit/mediaio.cpp
            tests/unit/mediaioallocator.cpp
            tests/unit/mediaioclock.cpp
            tests/unit/mediaiodescription.cpp
            tests/unit/mediaioparams.cpp
            tests/unit/mediaioportgroup.cpp
            tests/unit/mediaiorequest.cpp
            tests/unit/mediaio_bridges.cpp
            tests/unit/mediaio_identifiers.cpp
            tests/unit/mediaio_negotiation.cpp
            tests/unit/mediaio_setclock.cpp
            tests/unit/pacinggate.cpp
            tests/unit/mediaioportconnection.cpp
            tests/unit/mediaioreadcache.cpp
            tests/unit/mediaiosink.cpp
            tests/unit/mediaiostatscollector.cpp
            tests/unit/mediaiosource.cpp
            tests/unit/mediaio_overrides.cpp
            tests/unit/mediapayload.cpp
            tests/unit/mediapipelineconfig.cpp
            tests/unit/mediapipelineplanner.cpp
            tests/unit/mediapipelinestats.cpp
            tests/unit/mediapipeline.cpp
            tests/unit/pipelineevent.cpp
            tests/unit/mediaiotask_csc.cpp
            tests/unit/mediaiotask_src.cpp
            tests/unit/mediaiotask_burn.cpp
            tests/unit/mediaiotask_debugmedia.cpp
            tests/unit/mediaiotask_rawbitstream.cpp
            tests/unit/mpegts.cpp
            tests/unit/mpegtsmuxer.cpp
            tests/unit/mpegtsframer.cpp
            tests/unit/mediaiotask_mpegtsfile.cpp
            tests/unit/smpte302m.cpp
            tests/unit/jpegxsbitstream.cpp
            $<$<BOOL:${PROMEKI_ENABLE_SRT}>:tests/unit/srtmediaio.cpp>
            tests/unit/mediaiotask_framebridge.cpp
            tests/unit/mediaiotask_framesync.cpp
            tests/unit/mediaiotask_videoencoder.cpp
            tests/unit/mediaiotask_videodecoder.cpp
            tests/unit/mediaiotask_audioencoder.cpp
            tests/unit/mediaiotask_audiodecoder.cpp
            tests/unit/mediaiotask_inspector.cpp
            tests/unit/mediaiotask_mjpegstream.cpp
            tests/unit/mediaiotask_nullpacing.cpp
            tests/unit/mediaiotask_quicktime.cpp
            tests/unit/mediaiotask_rtp.cpp
            tests/unit/paintengine.cpp
            tests/unit/pixelformat.cpp
            tests/unit/sdivpid.cpp
            tests/unit/sdiwireinference.cpp
            tests/unit/videocodec.cpp
            tests/unit/videocodec_registry.cpp
            tests/unit/quicktime.cpp
            tests/unit/videotestpattern.cpp
            tests/unit/motionband.cpp
        )
        if(PROMEKI_ENABLE_CSC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/cscpipeline.cpp
                tests/unit/cscmediaio.cpp
                tests/unit/csc_st2110.cpp
            )
        endif()
        if(PROMEKI_ENABLE_PNG)
            list(APPEND UNITTEST_SOURCES
                tests/unit/imagefileio_png.cpp
            )
        endif()
        if(PROMEKI_ENABLE_JPEG)
            list(APPEND UNITTEST_SOURCES
                tests/unit/jpegvideocodec.cpp
                tests/unit/imagefileio_jpeg.cpp
            )
        endif()
        if(PROMEKI_ENABLE_JPEGXS)
            list(APPEND UNITTEST_SOURCES
                tests/unit/jpegxsvideocodec.cpp
                tests/unit/imagefileio_jpegxs.cpp
            )
        endif()
        if(PROMEKI_ENABLE_V4L2)
            list(APPEND UNITTEST_SOURCES
                tests/unit/v4l2mediaio.cpp
                tests/unit/v4l2m2mcodec.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NVENC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/nvencvideoencoder.cpp
            )
        endif()
        if(PROMEKI_ENABLE_X264)
            list(APPEND UNITTEST_SOURCES
                tests/unit/x264videoencoder.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NVDEC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/nvdecvideodecoder.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NDI)
            list(APPEND UNITTEST_SOURCES
                tests/unit/ndilib.cpp
                tests/unit/ndiformat.cpp
                tests/unit/ndidiscovery.cpp
                tests/unit/ndiclock.cpp
                tests/unit/ndimediaio.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NTV2)
            # NTV2 unit tests cover only the hardware-free units
            # (format mappings, clock wrap extension, sink-mode
            # negotiation).  The functional capture/playout tests live
            # under tests/func/ and are SKIPPED when no AJA card is
            # present.
            list(APPEND UNITTEST_SOURCES
                tests/unit/ntv2anc.cpp
                tests/unit/ntv2device.cpp
                tests/unit/ntv2format.cpp
                tests/unit/ntv2clock.cpp
                tests/unit/ntv2mediaio.cpp
                tests/unit/ntv2routing.cpp
                tests/unit/ntv2vpid.cpp
            )
        endif()
        if(PROMEKI_ENABLE_SRC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/audioresampler.cpp
            )
        endif()
    endif()

    if(PROMEKI_ENABLE_MUSIC)
        list(APPEND UNITTEST_SOURCES
            tests/unit/midinote.cpp
            tests/unit/midinotenames.cpp
            tests/unit/musicalnote.cpp
            tests/unit/musicalscale.cpp
            tests/unit/notesequenceparser.cpp
        )
    endif()

    # The pipeline demo's REST/WS test depends on the static-library
    # split in demos/promeki-pipeline.  Phase 18 re-enables it after
    # rooting out the multipart-MJPEG preview symptoms that surfaced
    # during Phase 14b.
    if(PROMEKI_BUILD_DEMOS AND PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_PROAV)
        list(APPEND UNITTEST_SOURCES
            tests/unit/promeki_pipeline_api.cpp
        )
    endif()

    add_executable(unittest-promeki EXCLUDE_FROM_ALL ${UNITTEST_SOURCES})
    target_compile_definitions(unittest-promeki PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>
        PROMEKI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(unittest-promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
    target_link_libraries(unittest-promeki PRIVATE promeki promeki::warnings)
    if(PROMEKI_BUILD_DEMOS AND PROMEKI_ENABLE_NETWORK AND PROMEKI_ENABLE_PROAV)
        target_link_libraries(unittest-promeki PRIVATE promeki-pipeline-core)
    endif()
    if(PROMEKI_ENABLE_NTV2)
        # Routing-helper tests anchor against AJA's own crosspoint id
        # helpers so the expected connections track the SDK's idea of
        # which (input, output) crosspoint ids go together — pulls
        # in the libajantv2 include set transitively.
        target_link_libraries(unittest-promeki PRIVATE ${PROMEKI_NTV2_TARGET})
    endif()
    # Precompile the largest test-side common headers so each of the ~250
    # unit-test TUs doesn't re-parse them at -O3 + debug-info, which was
    # pushing per-TU peak resident memory across the build script's
    # BUILD_PER_JOB_MB threshold and OOM-killing the build.  doctest's
    # main TU (tests/unit/doctest_main.cpp) is excluded because it
    # defines DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN before including
    # doctest.h, which is incompatible with a PCH that already pulled
    # doctest.h in.
    if(PROMEKI_USE_PCH)
        target_precompile_headers(unittest-promeki PRIVATE
            ${PROMEKI_PCH_HEADERS}
            <doctest/doctest.h>
            <promeki/mediapayload.h>
            <promeki/frame.h>
            <promeki/mediaconfig.h>
            <promeki/audiodesc.h>
            <promeki/imagedesc.h>
            <promeki/pixelformat.h>
            <promeki/metadata.h>
        )
        # Bust ccache when the baked-in config.h changes — see PROMEKI_CONFIG_SIG.
        target_compile_definitions(unittest-promeki PRIVATE PROMEKI_CONFIG_SIG=0x${PROMEKI_CONFIG_SIG})
        set_source_files_properties(tests/unit/doctest_main.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endif()
    add_test(NAME unittest-promeki COMMAND unittest-promeki)
    set_tests_properties(unittest-promeki PROPERTIES TIMEOUT 180)

    if(PROMEKI_BUILD_TUI)
        set(UNITTEST_TUI_SOURCES
            tests/unit/doctest_main.cpp
            tests/unit/tui/button.cpp
            tests/unit/tui/checkbox.cpp
            tests/unit/tui/frame.cpp
            tests/unit/tui/inputparser.cpp
            tests/unit/tui/label.cpp
            tests/unit/tui/layout.cpp
            tests/unit/tui/lineedit.cpp
            tests/unit/tui/listview.cpp
            tests/unit/tui/menu.cpp
            tests/unit/tui/painter.cpp
            tests/unit/tui/palette.cpp
            tests/unit/tui/progressbar.cpp
            tests/unit/tui/screen.cpp
            tests/unit/tui/scrollarea.cpp
            tests/unit/tui/splitter.cpp
            tests/unit/tui/statusbar.cpp
            tests/unit/tui/tabwidget.cpp
            tests/unit/tui/textarea.cpp
            tests/unit/tui/tuisubsystem.cpp
            tests/unit/tui/widget.cpp
        )
        add_executable(unittest-tui EXCLUDE_FROM_ALL ${UNITTEST_TUI_SOURCES})
        target_compile_definitions(unittest-tui PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-tui PRIVATE promeki-tui promeki::warnings)
        add_test(NAME unittest-tui COMMAND unittest-tui)
        set_tests_properties(unittest-tui PROPERTIES TIMEOUT 60)
    endif()

    if(PROMEKI_BUILD_SDL)
        set(UNITTEST_SDL_SOURCES
            tests/unit/doctest_main.cpp
            tests/unit/sdl/sdlaudioclock.cpp
            tests/unit/sdl/sdlrenderer.cpp
            tests/unit/sdl/sdlsubsystem.cpp
        )
        add_executable(unittest-sdl EXCLUDE_FROM_ALL ${UNITTEST_SDL_SOURCES})
        target_compile_definitions(unittest-sdl PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-sdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-sdl PRIVATE promeki-sdl promeki::warnings)
        add_test(NAME unittest-sdl COMMAND unittest-sdl)
        set_tests_properties(unittest-sdl PROPERTIES TIMEOUT 60)
    endif()

    # Umbrella targets so test executables (which are EXCLUDE_FROM_ALL,
    # i.e. not part of the default `cmake --build` run) can be built and
    # run with one command:
    #
    #   cmake --build <dir> --target tests   # build all unit-test execs
    #   cmake --build <dir> --target check   # build them, then ctest
    #
    # `check` runs ctest serially (--parallel 1) — many tests reach for
    # network / FS / thread state that misbehaves under parallel ctest.
    set(PROMEKI_TEST_TARGETS unittest-promeki)
    if(PROMEKI_BUILD_TUI)
        list(APPEND PROMEKI_TEST_TARGETS unittest-tui)
    endif()
    if(PROMEKI_BUILD_SDL)
        list(APPEND PROMEKI_TEST_TARGETS unittest-sdl)
    endif()

    add_custom_target(tests DEPENDS ${PROMEKI_TEST_TARGETS})

    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel 1
        DEPENDS ${PROMEKI_TEST_TARGETS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
        VERBATIM
    )
endif()

# Custom Clean Targets #########################################################

set(PROMEKI_CLEAN_DIRS
    ${CMAKE_BINARY_DIR}/CMakeFiles/promeki.dir
    ${CMAKE_BINARY_DIR}/CMakeFiles/libpromeki_invalidate_build_info.dir
    ${CMAKE_BINARY_DIR}/buildinfo.cpp
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki.*
    ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-*.so*
)

if(PROMEKI_BUILD_TESTS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-promeki.dir
        ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-promeki
    )
endif()

if(PROMEKI_BUILD_TUI)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-tui.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-tui*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-tui.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-tui
        )
    endif()
endif()

if(PROMEKI_BUILD_SDL)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/CMakeFiles/promeki-sdl.dir
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY}/libpromeki-sdl*
    )
    if(PROMEKI_BUILD_TESTS)
        list(APPEND PROMEKI_CLEAN_DIRS
            ${CMAKE_BINARY_DIR}/CMakeFiles/unittest-sdl.dir
            ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unittest-sdl
        )
    endif()
endif()

if(PROMEKI_BUILD_UTILS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/utils
    )
endif()

if(PROMEKI_BUILD_DEMOS)
    list(APPEND PROMEKI_CLEAN_DIRS
        ${CMAKE_BINARY_DIR}/demos
    )
endif()

add_custom_target(libclean
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${PROMEKI_CLEAN_DIRS}
    COMMENT "Cleaning promeki objects (preserving third-party builds)"
)

# Code Formatting ##############################################################
#
# `format-tree` rewrites the source tree in place; `format-check-tree`
# is the read-only gate (used by scripts/precommit.sh).  Both run a
# single clang-format invocation across every owned .h / .cpp under
# src/, include/promeki/, tests/{unit,func}/, demos/, utils/.  Vendored
# code under thirdparty/ is intentionally excluded.
#
# Naming note: the bare names `format` / `format-check` are already
# claimed by the vendored libvtc and cirf submodules' CMakeLists, which
# format only their own subdirs.  The `-tree` suffix makes clear this
# variant covers the whole project source tree and dodges the collision.
#
# CONFIGURE_DEPENDS makes the glob refresh on each build invocation,
# so new files are picked up without an explicit reconfigure.  The
# style itself is governed by .clang-format at the repo root.

find_program(PROMEKI_CLANG_FORMAT NAMES clang-format)
if(PROMEKI_CLANG_FORMAT)
    file(GLOB_RECURSE PROMEKI_FORMAT_FILES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/func/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/func/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/demos/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/demos/*.h
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/utils/*.h
    )

    add_custom_target(format-tree
        COMMAND ${PROMEKI_CLANG_FORMAT} -i ${PROMEKI_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Reformatting source tree (clang-format -i)"
        VERBATIM
    )

    add_custom_target(format-check-tree
        COMMAND ${PROMEKI_CLANG_FORMAT} --dry-run --Werror ${PROMEKI_FORMAT_FILES}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking source tree formatting (clang-format --dry-run --Werror)"
        VERBATIM
    )
else()
    message(STATUS "clang-format not found on PATH; `format-tree` / `format-check-tree` targets disabled")
endif()

# Documentation ################################################################
#
# The actual docs target wiring lives in cmake/PromekiDocsTarget.cmake so the
# same setup is reachable from both the normal build and the early-return
# PROMEKI_DOCS_ONLY path above.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/PromekiDocsTarget.cmake)
promeki_setup_docs_target()
