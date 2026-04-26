/**
 * @file      platform.h
 * @copyright Howard Logic. All rights reserved.
 * @ingroup util
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

// ============================================================================
// Operating system detection
// ============================================================================

#if defined(__EMSCRIPTEN__)
#define PROMEKI_PLATFORM_EMSCRIPTEN 1
#define PROMEKI_PLATFORM "Emscripten"
#elif defined(_WIN64)
#define PROMEKI_PLATFORM_WINDOWS 64
#define PROMEKI_PLATFORM "Win64"
#elif defined(_WIN32)
#define PROMEKI_PLATFORM_WINDOWS 32
#define PROMEKI_PLATFORM "Win32"
#elif defined(__APPLE__)
#define PROMEKI_PLATFORM_APPLE 1
#define PROMEKI_PLATFORM "MacOS"
#elif defined(__linux__)
#define PROMEKI_PLATFORM_LINUX 1
#define PROMEKI_PLATFORM "Linux"
#elif defined(__FreeBSD__)
#define PROMEKI_PLATFORM_FREEBSD 1
#define PROMEKI_PLATFORM "FreeBSD"
#elif defined(__OpenBSD__)
#define PROMEKI_PLATFORM_OPENBSD 1
#define PROMEKI_PLATFORM "OpenBSD"
#else
#define PROMEKI_PLATFORM_UNKNOWN 1
#define PROMEKI_PLATFORM "Unknown"
#endif

// Convenience macro for any POSIX-like system (not Windows)
#if defined(PROMEKI_PLATFORM_LINUX) || defined(PROMEKI_PLATFORM_APPLE) || defined(PROMEKI_PLATFORM_FREEBSD) ||         \
        defined(PROMEKI_PLATFORM_OPENBSD)
#define PROMEKI_PLATFORM_POSIX 1
#endif

// Convenience macro for any BSD-derived system
#if defined(PROMEKI_PLATFORM_FREEBSD) || defined(PROMEKI_PLATFORM_OPENBSD) || defined(PROMEKI_PLATFORM_APPLE)
#define PROMEKI_PLATFORM_BSD 1
#endif

// ============================================================================
// Compiler detection
// ============================================================================

// Note: Clang check must come before GCC check since Clang also defines __GNUC__
#if defined(__clang__)
#define PROMEKI_COMPILER_CLANG 1
#elif defined(__GNUC__)
#define PROMEKI_COMPILER_GCC 1
#elif defined(_MSC_VER)
#define PROMEKI_COMPILER_MSVC 1
#endif

// Convenience macro for GCC-compatible compilers (GCC or Clang)
#if defined(PROMEKI_COMPILER_GCC) || defined(PROMEKI_COMPILER_CLANG)
#define PROMEKI_COMPILER_GCC_COMPAT 1
#endif

// NVIDIA's nvcc is a driver around a host compiler (typically GCC or Clang),
// so this is set in addition to one of the host-compiler macros above when
// the current TU is being processed by nvcc.  Use it to gate CUDA-only
// constructs (`#pragma unroll`, `__device__`, etc.).
#if defined(__CUDACC__)
#define PROMEKI_COMPILER_NVCC 1
#endif

// ============================================================================
// Compiler feature macros
// ============================================================================

// PROMEKI_UNROLL — request loop unrolling on the immediately-following loop.
// Clang and nvcc honour `#pragma unroll`; GCC ignores it (and warns under
// -Wunknown-pragmas, which we treat as an error in precommit).  Falls back
// to a no-op everywhere else — at -O3, GCC will already unroll loops with
// constant trip counts on its own, so dropping the hint costs nothing in
// practice.
#if defined(PROMEKI_COMPILER_CLANG) || defined(PROMEKI_COMPILER_NVCC)
#define PROMEKI_UNROLL _Pragma("unroll")
#else
#define PROMEKI_UNROLL
#endif

// ============================================================================
// C library detection
// ============================================================================

#if defined(__GLIBC__)
#define PROMEKI_LIBC_GLIBC 1
#define PROMEKI_LIBC_GLIBC_VERSION_MAJOR __GLIBC__
#define PROMEKI_LIBC_GLIBC_VERSION_MINOR __GLIBC_MINOR__
#endif
