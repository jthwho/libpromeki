cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

set(APP_NAME "promeki")
set(APP_VENDOR "HOWARDLOGIC")
set(APP_HUMAN_NAME "libpromeki")
set(APP_HUMAN_VENDOR "Howard Logic")
set(APP_VERSION "1.0.0")
set(APP_VERSION_RC 0) # If this is a release candidate, set this value to the release candidate version (non zero)
set(APP_VERSION_BETA 0) # If this is a beta, set this value to the beta version (non-zero)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# DevRelease build type --------------------------------------------------------
# Release-level optimisations with developer features: debug symbols for
# useful crash logs / core dumps, and promekiDebug() logging compiled in.
# Must be set before project() so CMake picks up the _INIT flags.
set(CMAKE_C_FLAGS_DEVRELEASE_INIT            "-O3 -g -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEVRELEASE_INIT          "-O3 -g -DNDEBUG")
set(CMAKE_EXE_LINKER_FLAGS_DEVRELEASE_INIT   "")
set(CMAKE_SHARED_LINKER_FLAGS_DEVRELEASE_INIT "")

# Default to DevRelease when no build type is specified (single-config generators only).
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

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(ExternalProject)

# Build options ################################################################

option(PROMEKI_BUILD_TUI        "Build the promeki-tui library"    ON)
# Default ON when SDL3 is available, OFF otherwise.
find_package(SDL3 QUIET)
option(PROMEKI_BUILD_SDL        "Build the promeki-sdl library"    ${SDL3_FOUND})
option(PROMEKI_BUILD_UTILS      "Build utility applications"       ON)
option(PROMEKI_BUILD_DEMOS      "Build demonstration applications" ON)
option(PROMEKI_BUILD_TESTS      "Build unit tests"                 ON)
option(PROMEKI_BUILD_DOCS       "Build documentation"              OFF)
option(PROMEKI_BUILD_BENCHMARKS "Build the promeki-bench utility"  ON)

# Feature flags for the main promeki library
# NOTE: When adding a new PROMEKI_ENABLE_* flag, also update
# src/core/buildinfo.cpp.in (buildFeatureString) and
# include/promeki/config.h.in so the flag appears in build-info output.
option(PROMEKI_ENABLE_NETWORK  "Include networking support"             ON)
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
option(PROMEKI_ENABLE_FREETYPE "Include FreeType font rendering"        ON)
option(PROMEKI_ENABLE_AUDIO    "Include audio file I/O (libsndfile)"    ON)
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
# ON when the SDK is located via PROMEKI_NVENC_SDK_DIR (or the
# NVENC_SDK_DIR environment variable), silently OFF elsewhere.
# Requires PROMEKI_ENABLE_CUDA.
set(PROMEKI_NVENC_SDK_DIR "" CACHE PATH
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

# Vendored vs system dependency options
option(PROMEKI_USE_SYSTEM_ZLIB          "Use system zlib instead of vendored zlib-ng"   OFF)
option(PROMEKI_USE_SYSTEM_LIBSPNG       "Use system libspng instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_LIBJPEG       "Use system libjpeg instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_SVT_JPEG_XS   "Use system SVT-JPEG-XS instead of vendored"    OFF)
option(PROMEKI_USE_SYSTEM_FREETYPE      "Use system freetype instead of vendored"       OFF)
option(PROMEKI_USE_SYSTEM_SNDFILE       "Use system libsndfile instead of vendored"     OFF)
option(PROMEKI_USE_SYSTEM_SAMPLERATE   "Use system libsamplerate instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_NLOHMANN_JSON "Use system nlohmann_json instead of vendored"  OFF)
option(PROMEKI_USE_SYSTEM_VTC           "Use system libvtc instead of vendored"         OFF)
option(PROMEKI_USE_SYSTEM_HIGHWAY      "Use system Highway instead of vendored"        OFF)
option(PROMEKI_USE_SYSTEM_CIRF          "Use system cirf instead of vendored"           OFF)

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
if(PROMEKI_ENABLE_FREETYPE AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_FREETYPE requires PROMEKI_ENABLE_PROAV")
endif()
if(PROMEKI_ENABLE_AUDIO AND NOT PROMEKI_ENABLE_PROAV)
    message(FATAL_ERROR "PROMEKI_ENABLE_AUDIO requires PROMEKI_ENABLE_PROAV")
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

set_property(GLOBAL APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/.git/index"
)

if(NOT ${APP_VERSION_RC} EQUAL "0")
    set(BUILD_INFO_VERSION "${APP_VERSION}-rc${APP_VERSION_RC}")
elseif(NOT ${APP_VERSION_BETA} EQUAL "0")
    set(BUILD_INFO_VERSION "${APP_VERSION}-beta${APP_VERSION_BETA}")
else()
    set(BUILD_INFO_VERSION "${APP_VERSION}")
endif()
set(BUILD_INFO_VERSION_BETA "${APP_VERSION_BETA}")
set(BUILD_INFO_VERSION_RC "${APP_VERSION_RC}")
set(BUILD_INFO_NAME "${APP_HUMAN_NAME}")
string(TIMESTAMP BUILD_INFO_DATE "%Y-%m-%d")
string(TIMESTAMP BUILD_INFO_TIME "%H:%M:%S")
set(BUILD_INFO_TYPE "${CMAKE_BUILD_TYPE}")
cmake_host_system_information(RESULT BUILD_INFO_HOSTNAME QUERY HOSTNAME)
configure_file(src/core/buildinfo.cpp.in buildinfo.cpp)

# Touch the .in file every build so configure_file re-generates buildinfo.cpp
# with a fresh timestamp and git hash.  The generated file also #includes
# config.h and platform.h for buildPlatformString(), buildFeatureString(),
# etc. — if you add a new PROMEKI_ENABLE_* flag or platform macro that
# should appear in build-info output, update buildinfo.cpp.in as well.
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

# Generate config.h
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
)

# Output directories ###########################################################

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin CACHE STRING "Runtime directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Library directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib CACHE STRING "Archive directory")

# Third Party Dependencies ####################################################

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
# The three targets we need are:
#
#   cirf_runtime  — static lib, single file, linked into promeki
#   cirf_lib      — static lib, backing the code-generator binary
#   cirf          — host executable, runs at build time to emit the
#                   generated resource C source from a JSON config
#
# CIRFAddResources.cmake is included directly so the
# `cirf_add_resources()` function is in scope for the call below.
if(PROMEKI_USE_SYSTEM_CIRF)
    find_package(CIRF REQUIRED)
    set(PROMEKI_CIRF_RUNTIME_TARGET CIRF::cirf_runtime)
else()
    set(PROMEKI_CIRF_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/cirf)

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

    # Bring cirf_add_resources() into scope. CIRFAddResources.cmake
    # is self-contained and looks up the `cirf` / `cirf_runtime` /
    # `cirf_lib` targets by name — which we have just defined.
    include(${PROMEKI_CIRF_DIR}/cmake/CIRFAddResources.cmake)

    set(PROMEKI_CIRF_RUNTIME_TARGET cirf_runtime)
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
        ExternalProject_Add(
            zlib-ng-ep
            DOWNLOAD_COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/zlib-ng
                ${PROMEKI_ZLIB_SRC_COPY}
            SOURCE_DIR ${PROMEKI_ZLIB_SRC_COPY}
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DZLIB_COMPAT=ON
                -DZLIB_ENABLE_TESTS=OFF
                -DZLIBNG_ENABLE_TESTS=OFF
                -DBUILD_SHARED_LIBS=OFF
                -DWITH_GTEST=OFF
        )
        set(PROMEKI_ZLIB_INCLUDE ${PROMEKI_THIRDPARTY_PREFIX}/include)
        set(PROMEKI_ZLIB_LIBRARY ${PROMEKI_THIRDPARTY_PREFIX}/lib/libz.a)
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
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DENABLE_SHARED=OFF
                -DENABLE_STATIC=ON
                -DWITH_TURBOJPEG=OFF
                ${_JPEG_TURBO_EXTRA_ARGS}
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
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_APPS=OFF
                -DBUILD_TESTING=OFF
                -DCMAKE_BUILD_TYPE=Release
        )
        add_library(promeki_jpegxs_vendored STATIC IMPORTED)
        set_target_properties(promeki_jpegxs_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libSvtJpegxs.a
        )
        add_dependencies(promeki_jpegxs_vendored svt-jpeg-xs-ep)
        set(PROMEKI_JPEGXS_TARGET promeki_jpegxs_vendored)
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
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DCMAKE_PREFIX_PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_ROOT=${PROMEKI_THIRDPARTY_PREFIX}
                -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
                -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
                -DSPNG_SHARED=OFF
                -DSPNG_STATIC=ON
                -DBUILD_EXAMPLES=OFF
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

# --- libsndfile ---
if(PROMEKI_ENABLE_AUDIO)
    if(PROMEKI_USE_SYSTEM_SNDFILE)
        find_package(SndFile REQUIRED)
        set(PROMEKI_SNDFILE_TARGET SndFile::sndfile)
    else()
        ExternalProject_Add(
            libsndfile-ep
            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile
            CMAKE_ARGS
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_PROGRAMS=OFF
                -DBUILD_EXAMPLES=OFF
                -DBUILD_TESTING=OFF
                -DENABLE_EXTERNAL_LIBS=OFF
                -DENABLE_MPEG=OFF
        )
        add_library(promeki_sndfile_vendored STATIC IMPORTED)
        set_target_properties(promeki_sndfile_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libsndfile.a
        )
        add_dependencies(promeki_sndfile_vendored libsndfile-ep)
        set(PROMEKI_SNDFILE_TARGET promeki_sndfile_vendored)
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
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DBUILD_SHARED_LIBS=OFF
                -DBUILD_TESTING=OFF
                -DLIBSAMPLERATE_EXAMPLES=OFF
                -DLIBSAMPLERATE_INSTALL=ON
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
                -DCMAKE_INSTALL_PREFIX:PATH=${PROMEKI_THIRDPARTY_PREFIX}
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON
                -DZLIB_INCLUDE_DIR=${PROMEKI_ZLIB_INCLUDE}
                -DZLIB_LIBRARY=${PROMEKI_ZLIB_LIBRARY}
                -DBUILD_SHARED_LIBS=OFF
                -DFT_DISABLE_BZIP2=ON
                -DFT_DISABLE_BROTLI=ON
                -DFT_DISABLE_HARFBUZZ=ON
                -DFT_DISABLE_PNG=ON
        )
        add_library(promeki_freetype_vendored STATIC IMPORTED)
        set_target_properties(promeki_freetype_vendored PROPERTIES
            IMPORTED_LOCATION ${PROMEKI_THIRDPARTY_PREFIX}/lib/libfreetype.a
        )
        add_dependencies(promeki_freetype_vendored freetype-ep)
        set(PROMEKI_FREETYPE_TARGET promeki_freetype_vendored)
    endif()
endif()

# Main Library: promeki ########################################################

set(PROMEKI_HEADERS
    include/promeki/algorithm.h
    include/promeki/ansistream.h
    include/promeki/application.h
    include/promeki/array.h
    include/promeki/atomic.h
    include/promeki/audiolevel.h
    include/promeki/benchmark.h
    include/promeki/benchmarkrunner.h
    include/promeki/buffer.h
    include/promeki/bufferediodevice.h
    include/promeki/bufferiodevice.h
    include/promeki/bufferpool.h
    include/promeki/bufferview.h
    include/promeki/buildinfo.h
    include/promeki/bytearray.h
    include/promeki/char.h
    include/promeki/ciepoint.h
    include/promeki/ciewavelengthtable.h
    include/promeki/audiocodec.h
    include/promeki/cmdlineparser.h
    include/promeki/color.h
    include/promeki/colormodel.h
    include/promeki/configoption.h
    include/promeki/cuda.h
    include/promeki/datetime.h
    include/promeki/dir.h
    include/promeki/duration.h
    include/promeki/elapsedtimer.h
    include/promeki/enum.h
    include/promeki/enums.h
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
    include/promeki/framerate.h
    include/promeki/future.h
    include/promeki/h264bitstream.h
    include/promeki/hevcbitstream.h
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
    include/promeki/mempool.h
    include/promeki/memspace.h
    include/promeki/metadata.h
    include/promeki/mouseevent.h
    include/promeki/mutex.h
    include/promeki/namespace.h
    include/promeki/numname.h
    include/promeki/numnameseq.h
    include/promeki/objectbase.h
    include/promeki/obfuscatedstring.h
    include/promeki/pidcontroller.h
    include/promeki/pixeldesc.h
    include/promeki/pixelformat.h
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
    include/promeki/securemem.h
    include/promeki/selfpipe.h
    include/promeki/set.h
    include/promeki/sha1.h
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
    include/promeki/units.h
    include/promeki/uuid.h
    include/promeki/variant.h
    include/promeki/variantdatabase.h
    include/promeki/variantspec.h
    include/promeki/videocodec.h
    include/promeki/videoformat.h
    include/promeki/waitcondition.h
    include/promeki/widget.h
    include/promeki/xyzcolor.h
)

set(PROMEKI_SOURCES
    ${CMAKE_CURRENT_BINARY_DIR}/buildinfo.cpp
    src/core/ansistream.cpp
    src/core/application.cpp
    src/core/array.cpp
    src/core/audiolevel.cpp
    src/core/benchmark.cpp
    src/core/benchmarkrunner.cpp
    src/core/buffer.cpp
    src/core/bufferediodevice.cpp
    src/core/bufferiodevice.cpp
    src/core/bufferpool.cpp
    src/core/char.cpp
    src/core/ciepoint.cpp
    src/core/clock.cpp
    src/core/clockdomain.cpp
    src/core/cmdlineparser.cpp
    src/core/color.cpp
    src/core/colormodel.cpp
    src/core/contentlightlevel.cpp
    src/core/masteringdisplay.cpp
    src/core/audiocodec.cpp
    src/core/crashhandler.cpp
    src/core/crc.cpp
    src/core/cuda.cpp
    src/core/videocodec.cpp
    src/core/datastream.cpp
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
    src/core/fourcc.cpp
    src/core/framepacer.cpp
    src/core/framerate.cpp
    src/core/h264bitstream.cpp
    src/core/hevcbitstream.cpp
    src/core/histogram.cpp
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
    src/core/mediatimestamp.cpp
    src/core/md5.cpp
    src/core/mempool.cpp
    src/core/memspace.cpp
    src/core/metadata.cpp
    src/core/mouseevent.cpp
    src/core/numname.cpp
    src/core/numnameseq.cpp
    src/core/objectbase.cpp
    src/core/periodiccallback.cpp
    src/core/pidcontroller.cpp
    src/core/pixeldesc.cpp
    src/core/pixelformat.cpp
    src/core/point.cpp
    src/core/process.cpp
    src/core/random.cpp
    src/core/rational.cpp
    src/core/ratetracker.cpp
    src/core/rect.cpp
    src/core/regex.cpp
    src/core/resource.cpp
    src/core/selfpipe.cpp
    src/core/set.cpp
    src/core/sha1.cpp
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
    src/core/thread.cpp
    src/core/threadpool.cpp
    src/core/timecode.cpp
    src/core/timecodegenerator.cpp
    src/core/timestamp.cpp
    src/core/umid.cpp
    src/core/units.cpp
    src/core/util.cpp
    src/core/uuid.cpp
    src/core/variant.cpp
    src/core/variantspec.cpp
    src/core/videoformat.cpp
    src/core/widget.cpp
    src/core/xyzcolor.cpp
)

# Network sources
if(PROMEKI_ENABLE_NETWORK)
    list(APPEND PROMEKI_HEADERS
        include/promeki/abstractsocket.h
        include/promeki/ipv4address.h
        include/promeki/ipv6address.h
        include/promeki/klvframe.h
        include/promeki/localserver.h
        include/promeki/localsocket.h
        include/promeki/loopbacktransport.h
        include/promeki/macaddress.h
        include/promeki/multicastmanager.h
        include/promeki/multicastreceiver.h
        include/promeki/networkaddress.h
        include/promeki/packettransport.h
        include/promeki/prioritysocket.h
        include/promeki/rawsocket.h
        include/promeki/rtppacket.h
        include/promeki/rtppayload.h
        include/promeki/rtpsession.h
        include/promeki/sdpsession.h
        include/promeki/socketaddress.h
        include/promeki/tcpserver.h
        include/promeki/tcpsocket.h
        include/promeki/udpsocket.h
        include/promeki/udpsockettransport.h
    )
    list(APPEND PROMEKI_SOURCES
        src/network/abstractsocket.cpp
        src/network/ipv4address.cpp
        src/network/ipv6address.cpp
        src/network/eui64.cpp
        src/network/klvframe.cpp
        src/network/localserver.cpp
        src/network/localsocket.cpp
        src/network/loopbacktransport.cpp
        src/network/macaddress.cpp
        src/network/multicastmanager.cpp
        src/network/multicastreceiver.cpp
        src/network/networkaddress.cpp
        src/network/packettransport.cpp
        src/network/prioritysocket.cpp
        src/network/rawsocket.cpp
        src/network/rtppayload.cpp
        src/network/rtpsession.cpp
        src/network/sdpsession.cpp
        src/network/socketaddress.cpp
        src/network/tcpserver.cpp
        src/network/tcpsocket.cpp
        src/network/udpsocket.cpp
        src/network/udpsockettransport.cpp
    )
endif()

# ProAV sources
if(PROMEKI_ENABLE_PROAV)
    list(APPEND PROMEKI_HEADERS
        include/promeki/audio.h
        include/promeki/audioblock.h
        include/promeki/audiobuffer.h
        include/promeki/audiodesc.h
        include/promeki/audiofile.h
        include/promeki/audiofilefactory.h
        include/promeki/audiogen.h
        include/promeki/audiotestpattern.h
        include/promeki/basicfont.h
        include/promeki/benchmarkreporter.h
        include/promeki/codec.h
        include/promeki/encodeddesc.h
        include/promeki/fastfont.h
        include/promeki/font.h
        include/promeki/frame.h
        include/promeki/framebridge.h
        include/promeki/mediaiotask_framebridge.h
        include/promeki/image.h
        include/promeki/imagedesc.h
        include/promeki/imagefile.h
        include/promeki/imagefileio.h
        include/promeki/imgseq.h
        include/promeki/jpegimagecodec.h
        include/promeki/jpegvideocodec.h
        include/promeki/jpegxsimagecodec.h
        include/promeki/ltcdecoder.h
        include/promeki/ltcencoder.h
        include/promeki/mediaio.h
        include/promeki/mediapipeline.h
        include/promeki/mediapipelineconfig.h
        include/promeki/mediapipelineplanner.h
        include/promeki/mediapipelinestats.h
        include/promeki/pipelinestats.h
        include/promeki/mediapacket.h
        include/promeki/mediaiotask.h
        include/promeki/nvencvideoencoder.h
        include/promeki/nvdecvideodecoder.h
        include/promeki/mediaiotask_csc.h
        include/promeki/mediaiotask_src.h
        include/promeki/mediaiotask_burn.h
        include/promeki/mediaiotask_framesync.h
        include/promeki/mediaiotask_videoencoder.h
        include/promeki/mediaiotask_videodecoder.h
        include/promeki/mediaiotask_rawbitstream.h
        include/promeki/mediaiotask_imagefile.h
        include/promeki/mediaiotask_quicktime.h
        include/promeki/mediaiotask_rtp.h
        include/promeki/mediaiotask_tpg.h
        include/promeki/paintengine.h
        include/promeki/quicktime.h
        include/promeki/mediadesc.h
        include/promeki/mediaiodescription.h
        include/promeki/videotestpattern.h
    )
    list(APPEND PROMEKI_SOURCES
        src/proav/audio.cpp
        src/proav/audioblock.cpp
        src/proav/audiobuffer.cpp
        src/proav/audiodesc.cpp
        src/proav/audiofile.cpp
        src/proav/audiofilefactory.cpp
        src/proav/audiogen.cpp
        src/proav/audiotestpattern.cpp
        src/proav/benchmarkreporter.cpp
        src/proav/codec.cpp
        src/proav/encodeddesc.cpp
        src/proav/frame.cpp
        src/proav/framebridge.cpp
        src/proav/framesync.cpp
        src/proav/image.cpp
        src/proav/imagedataencoder.cpp
        src/proav/imagedatadecoder.cpp
        src/proav/imagedesc.cpp
        src/proav/imagefile.cpp
        src/proav/imagefileio.cpp
        src/proav/imgseq.cpp
        src/proav/imagefileio_rawyuv.cpp
        src/proav/imagefileio_dpx.cpp
        src/proav/imagefileio_cin.cpp
        src/proav/imagefileio_tga.cpp
        src/proav/imagefileio_sgi.cpp
        src/proav/imagefileio_pnm.cpp
        src/proav/jpegimagecodec.cpp
        src/proav/jpegvideocodec.cpp
        src/proav/ltcdecoder.cpp
        src/proav/ltcencoder.cpp
        src/proav/mediaio.cpp
        src/proav/mediaiodescription.cpp
        src/proav/mediapipeline.cpp
        src/proav/mediapipelineconfig.cpp
        src/proav/mediapipelineplanner.cpp
        src/proav/mediapipelinestats.cpp
        src/proav/pipelinestats.cpp
        src/proav/mediapacket.cpp
        src/proav/mediaiotask.cpp
        src/proav/mediaiotask_csc.cpp
        src/proav/mediaiotask_src.cpp
        src/proav/mediaiotask_burn.cpp
        src/proav/mediaiotask_framebridge.cpp
        src/proav/mediaiotask_framesync.cpp
        src/proav/mediaiotask_videoencoder.cpp
        src/proav/mediaiotask_videodecoder.cpp
        src/proav/mediaiotask_rawbitstream.cpp
        src/proav/mediaiotask_imagefile.cpp
        src/proav/mediaiotask_inspector.cpp
        src/proav/mediaiotask_quicktime.cpp
        src/proav/mediaiotask_rtp.cpp
        src/proav/mediaiotask_tpg.cpp
        src/proav/paintengine.cpp
        src/proav/paintengine_422.cpp
        src/proav/paintengine_interleaved.cpp
        src/proav/paintengine_multiplane.cpp
        src/proav/paintengine_packed.cpp
        src/proav/quicktime.cpp
        src/proav/quicktime_atom.cpp
        src/proav/quicktime_reader.cpp
        src/proav/quicktime_writer.cpp
        src/proav/mediadesc.cpp
        src/proav/videotestpattern.cpp
    )

    # Feature-gated proav sources
    if(PROMEKI_ENABLE_JPEG)
        # jpegimagecodec is always included when proav is on; this gate
        # controls linking against libjpeg (codec uses it internally)
        list(APPEND PROMEKI_SOURCES src/proav/imagefileio_jpeg.cpp)
    endif()
    if(PROMEKI_ENABLE_JPEGXS)
        # JPEG XS has no fallback — the entire file is gated, not just
        # the external linkage, because SvtJpegxs.h is only available
        # when the thirdparty library was configured in.
        list(APPEND PROMEKI_HEADERS
            include/promeki/jpegxsvideocodec.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/jpegxsimagecodec.cpp
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
        list(APPEND PROMEKI_HEADERS include/promeki/mediaiotask_audiofile.h)
        list(APPEND PROMEKI_SOURCES
            src/proav/audiofile_libsndfile.cpp
            src/proav/mediaiotask_audiofile.cpp
        )
    endif()
    if(PROMEKI_ENABLE_SRC)
        list(APPEND PROMEKI_HEADERS include/promeki/audioresampler.h)
        list(APPEND PROMEKI_SOURCES src/proav/audioresampler.cpp)
    endif()
    if(PROMEKI_ENABLE_V4L2)
        list(APPEND PROMEKI_HEADERS include/promeki/mediaiotask_v4l2.h)
        list(APPEND PROMEKI_SOURCES src/proav/mediaiotask_v4l2.cpp)
    endif()
    if(PROMEKI_ENABLE_CSC)
        list(APPEND PROMEKI_HEADERS
            include/promeki/cscpipeline.h
            include/promeki/csccontext.h
            include/promeki/cscregistry.h
        )
        list(APPEND PROMEKI_SOURCES
            src/proav/csc/cscpipeline.cpp
            src/proav/csc/csccontext.cpp
            src/proav/csc/cscregistry.cpp
            src/proav/csc/unpack.cpp
            src/proav/csc/pack.cpp
            src/proav/csc/transfer.cpp
            src/proav/csc/matrix.cpp
            src/proav/csc/chroma.cpp
            src/proav/csc/alpha.cpp
            src/proav/csc/fastpath.cpp
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
set_target_properties(promeki PROPERTIES
    VERSION ${APP_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME promeki
    EXPORT_NAME promeki
)

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

# Built-in :/.PROMEKI/ resource set. cirf_add_resources() was brought
# into scope above. The static library it creates is whole-archive
# linked into promeki via the explicit reference to
# &promeki_resources_root in builtinresources.cpp, which keeps the
# .o (and therefore the resource data) reachable.
if(PROMEKI_ENABLE_CIRF)
    cirf_add_resources(promeki_resources
        CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/res/resources.json
        OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/promeki_resources)
    target_link_libraries(promeki PRIVATE promeki_resources)
    target_include_directories(promeki PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/promeki_resources)
    target_compile_definitions(promeki PRIVATE PROMEKI_HAVE_BUILTIN_RESOURCES)
    target_sources(promeki PRIVATE src/core/builtinresources.cpp)
endif()

target_link_libraries(promeki PUBLIC stdc++fs)
target_link_libraries(promeki PRIVATE m)
target_compile_definitions(promeki PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)

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
    if(PROMEKI_ENABLE_CSC)
        target_link_libraries(promeki PRIVATE ${PROMEKI_HWY_TARGET})
        target_compile_definitions(promeki PRIVATE PROMEKI_ENABLE_CSC)
        # Highway foreach_target needs to resolve includes relative to source root
        target_include_directories(promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    endif()
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
    target_compile_definitions(promeki-tui PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
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
        include/promeki/sdl/sdlplayerold.h
        include/promeki/sdl/sdlvideowidget.h
        include/promeki/sdl/sdlwindow.h
    )

    set(PROMEKI_SDL_SOURCES
        src/sdl/sdlsubsystem.cpp
        src/sdl/sdlaudiooutput.cpp
        src/sdl/sdlaudioclock.cpp
        src/sdl/sdleventpump.cpp
        src/sdl/sdlplayer.cpp
        src/sdl/sdlplayerold.cpp
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
    target_compile_definitions(promeki-sdl PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
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

# Install headers (flat under promeki/, excluding tui and sdl subdirs)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/promeki/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
    FILES_MATCHING PATTERN "*.h"
    PATTERN "tui" EXCLUDE
    PATTERN "sdl" EXCLUDE
    PATTERN "*.h.in" EXCLUDE
)

# Install generated config.h
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/promeki/config.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/promeki
)

# Install vendored nlohmann headers (needed by consumers) unless using system
if(NOT PROMEKI_USE_SYSTEM_NLOHMANN_JSON)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/nlohmann-json/include/nlohmann
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
        tests/unit/atomic.cpp
        tests/unit/audiocodec.cpp
        tests/unit/audiolevel.cpp
        tests/unit/typeregistry_isolation.cpp
        tests/unit/benchmarkrunner.cpp
        tests/unit/buffer.cpp
        tests/unit/bufferediodevice.cpp
        tests/unit/bufferiodevice.cpp
        tests/unit/bufferpool.cpp
        tests/unit/bufferview.cpp
        tests/unit/buildinfo.cpp
        tests/unit/bytearray.cpp
        tests/unit/ciepoint.cpp
        tests/unit/ciewavelengthtable.cpp
        tests/unit/cmdlineparser.cpp
        tests/unit/color.cpp
        tests/unit/colormodel.cpp
        tests/unit/configoption.cpp
        tests/unit/crashhandler.cpp
        tests/unit/clockdomain.cpp
        tests/unit/crc.cpp
        tests/unit/cuda.cpp
        tests/unit/datastream.cpp
        tests/unit/datastream_include_chain.cpp
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
        tests/unit/file.cpp
        tests/unit/fileformatfactory.cpp
        tests/unit/fileinfo.cpp
        tests/unit/fileiodevice.cpp
        tests/unit/filepath.cpp
        tests/unit/fnv1a.cpp
        tests/unit/fourcc.cpp
        tests/unit/framepacer.cpp
        tests/unit/framesync.cpp
        tests/unit/framerate.cpp
        tests/unit/future.cpp
        tests/unit/h264bitstream.cpp
        tests/unit/hashmap.cpp
        tests/unit/hashset.cpp
        tests/unit/hevcbitstream.cpp
        tests/unit/histogram.cpp
        tests/unit/iodevice.cpp
        tests/unit/json.cpp
        tests/unit/line.cpp
        tests/unit/libraryoptions.cpp
        tests/unit/list.cpp
        tests/unit/logger.cpp
        tests/unit/map.cpp
        tests/unit/matrix.cpp
        tests/unit/matrix3x3.cpp
        tests/unit/mediatimestamp.cpp
        tests/unit/md5.cpp
        tests/unit/mempool.cpp
        tests/unit/memspace.cpp
        tests/unit/metadata.cpp
        tests/unit/mutex.cpp
        tests/unit/numname.cpp
        tests/unit/obfuscatedstring.cpp
        tests/unit/objectbase.cpp
        tests/unit/objectbase2.cpp
        tests/unit/periodiccallback.cpp
        tests/unit/pair.cpp
        tests/unit/pidcontroller.cpp
        tests/unit/pixelformat.cpp
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
        tests/unit/thread.cpp
        tests/unit/threadpool.cpp
        tests/unit/timecode.cpp
        tests/unit/timecodegenerator.cpp
        tests/unit/timestamp.cpp
        tests/unit/umid.cpp
        tests/unit/units.cpp
        tests/unit/uuid.cpp
        tests/unit/variant.cpp
        tests/unit/variantdatabase.cpp
        tests/unit/variantspec.cpp
        tests/unit/videoformat.cpp
        tests/unit/waitcondition.cpp
        tests/unit/xyzcolor.cpp
    )

    if(PROMEKI_ENABLE_NETWORK)
        list(APPEND UNITTEST_SOURCES
            tests/unit/network/abstractsocket.cpp
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
            tests/unit/network/prioritysocket.cpp
            tests/unit/network/rawsocket.cpp
            tests/unit/network/rtppacket.cpp
            tests/unit/network/rtppayload.cpp
            tests/unit/network/rtpsession.cpp
            tests/unit/network/sdpsession.cpp
            tests/unit/network/socketaddress.cpp
            tests/unit/network/tcpserver.cpp
            tests/unit/network/tcpsocket.cpp
            tests/unit/network/udpsocket.cpp
            tests/unit/network/udpsockettransport.cpp
        )
    endif()

    if(PROMEKI_ENABLE_PROAV)
        list(APPEND UNITTEST_SOURCES
            tests/unit/audio.cpp
            tests/unit/audioblock.cpp
            tests/unit/audiobuffer.cpp
            tests/unit/audiodesc.cpp
            tests/unit/audiofilefactory.cpp
            tests/unit/audiotestpattern.cpp
            tests/unit/codec.cpp
            tests/unit/encodeddesc.cpp
            tests/unit/fastfont.cpp
            tests/unit/basicfont.cpp
            tests/unit/frame.cpp
            tests/unit/framebridge.cpp
            tests/unit/image.cpp
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
            tests/unit/jpegimagecodec.cpp
            tests/unit/ltcdecoder.cpp
            tests/unit/ltcencoder.cpp
            tests/unit/mediadesc.cpp
            tests/unit/mediaio.cpp
            tests/unit/mediaiodescription.cpp
            tests/unit/mediaio_bridges.cpp
            tests/unit/mediaio_identifiers.cpp
            tests/unit/mediaio_negotiation.cpp
            tests/unit/mediaiotask_overrides.cpp
            tests/unit/mediapacket.cpp
            tests/unit/mediaio_stats.cpp
            tests/unit/mediapipelineconfig.cpp
            tests/unit/mediapipelineplanner.cpp
            tests/unit/mediapipelinestats.cpp
            tests/unit/mediapipeline.cpp
            tests/unit/mediaiotask_csc.cpp
            tests/unit/mediaiotask_src.cpp
            tests/unit/mediaiotask_burn.cpp
            tests/unit/mediaiotask_framebridge.cpp
            tests/unit/mediaiotask_framesync.cpp
            tests/unit/mediaiotask_videoencoder.cpp
            tests/unit/mediaiotask_videodecoder.cpp
            tests/unit/mediaiotask_inspector.cpp
            tests/unit/mediaiotask_quicktime.cpp
            tests/unit/mediaiotask_rtp.cpp
            tests/unit/paintengine.cpp
            tests/unit/pixeldesc.cpp
            tests/unit/videocodec.cpp
            tests/unit/videocodec_registry.cpp
            tests/unit/quicktime.cpp
            tests/unit/videotestpattern.cpp
        )
        if(PROMEKI_ENABLE_CSC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/cscpipeline.cpp
            )
        endif()
        if(PROMEKI_ENABLE_PNG)
            list(APPEND UNITTEST_SOURCES
                tests/unit/imagefileio_png.cpp
            )
        endif()
        if(PROMEKI_ENABLE_JPEG)
            list(APPEND UNITTEST_SOURCES
                tests/unit/imagefileio_jpeg.cpp
            )
        endif()
        if(PROMEKI_ENABLE_JPEGXS)
            list(APPEND UNITTEST_SOURCES
                tests/unit/jpegxsimagecodec.cpp
                tests/unit/imagefileio_jpegxs.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NVENC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/nvencvideoencoder.cpp
            )
        endif()
        if(PROMEKI_ENABLE_NVDEC)
            list(APPEND UNITTEST_SOURCES
                tests/unit/nvdecvideodecoder.cpp
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

    add_executable(unittest-promeki ${UNITTEST_SOURCES})
    target_compile_definitions(unittest-promeki PRIVATE
        $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>
        PROMEKI_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_include_directories(unittest-promeki PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
    target_link_libraries(unittest-promeki PRIVATE promeki)
    add_test(NAME unittest-promeki COMMAND unittest-promeki)
    set_tests_properties(unittest-promeki PROPERTIES TIMEOUT 60)

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
        add_executable(unittest-tui ${UNITTEST_TUI_SOURCES})
        target_compile_definitions(unittest-tui PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-tui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-tui PRIVATE promeki-tui)
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
        add_executable(unittest-sdl ${UNITTEST_SDL_SOURCES})
        target_compile_definitions(unittest-sdl PRIVATE $<$<OR:$<CONFIG:Debug>,$<CONFIG:DevRelease>>:PROMEKI_DEBUG_ENABLE>)
        target_include_directories(unittest-sdl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty)
        target_link_libraries(unittest-sdl PRIVATE promeki-sdl)
        add_test(NAME unittest-sdl COMMAND unittest-sdl)
        set_tests_properties(unittest-sdl PROPERTIES TIMEOUT 60)
    endif()

    # Run tests as part of the normal build
    set(PROMEKI_TEST_TARGETS unittest-promeki)
    if(PROMEKI_BUILD_TUI)
        list(APPEND PROMEKI_TEST_TARGETS unittest-tui)
    endif()
    if(PROMEKI_BUILD_SDL)
        list(APPEND PROMEKI_TEST_TARGETS unittest-sdl)
    endif()

    add_custom_target(run-tests ALL
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS ${PROMEKI_TEST_TARGETS}
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        COMMENT "Running unit tests"
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
        ${CMAKE_BINARY_DIR}/CMakeFiles/run-tests.dir
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

# Documentation ################################################################

set(PROMEKI_DOCS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/doxygen" CACHE PATH
    "Output directory for generated documentation")

find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
    string(SUBSTRING "${BUILD_INFO_REPOIDENT}" 0 7 BUILD_INFO_REPOIDENT_SHORT)
    set(PROMEKI_DOCS_VERSION "${BUILD_INFO_VERSION}" CACHE STRING
        "Version string shown in generated documentation")
    set(DOXYGEN_IN ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in)
    set(DOXYGEN_OUT ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/footer.html.in
        ${CMAKE_CURRENT_BINARY_DIR}/doxygen_footer.html @ONLY)
    configure_file(${DOXYGEN_IN} ${DOXYGEN_OUT} @ONLY)
    file(GLOB PROMEKI_DOCS_IMAGES ${CMAKE_CURRENT_SOURCE_DIR}/docs/*.jpg)
    add_custom_target(docs
        COMMAND ${DOXYGEN_EXECUTABLE} ${DOXYGEN_OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs
        COMMAND ${CMAKE_COMMAND} -E copy ${PROMEKI_DOCS_IMAGES} ${PROMEKI_DOCS_OUTPUT_DIR}/html/docs/
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM)
    if(PROMEKI_BUILD_DOCS)
        add_custom_target(docs-all ALL DEPENDS docs)
    endif()
else()
    if(PROMEKI_BUILD_DOCS)
        message(FATAL_ERROR "PROMEKI_BUILD_DOCS is ON but Doxygen was not found")
    endif()
endif()
