/**
 * @file      platform.h
 * @copyright Howard Logic. All rights reserved.
 * @ingroup util
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
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
// CPU architecture detection
// ============================================================================
//
// Exactly one of PROMEKI_ARCH_* is set for each build, plus PROMEKI_ARCH_64
// when the target ABI is 64-bit.  PROMEKI_ARCH_NAME is a short string suitable
// for logging / build banners.  See the ISA-capability macros below for
// SIMD-feature gating — never key SIMD code paths off the arch macros
// directly, since e.g. an x86_64 build may still be compiled without SSE
// enabled, and an arm32 build may or may not have NEON.

#if defined(__x86_64__) || defined(_M_X64)
#define PROMEKI_ARCH_X86_64 1
#define PROMEKI_ARCH_64 1
#define PROMEKI_ARCH_NAME "x86_64"
#elif defined(__i386__) || defined(_M_IX86)
#define PROMEKI_ARCH_X86 1
#define PROMEKI_ARCH_NAME "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define PROMEKI_ARCH_AARCH64 1
#define PROMEKI_ARCH_64 1
#define PROMEKI_ARCH_NAME "aarch64"
#elif defined(__arm__) || defined(_M_ARM)
#define PROMEKI_ARCH_ARM 1
#define PROMEKI_ARCH_NAME "arm"
#elif defined(__riscv) && (__riscv_xlen == 64)
#define PROMEKI_ARCH_RISCV64 1
#define PROMEKI_ARCH_64 1
#define PROMEKI_ARCH_NAME "riscv64"
#elif defined(__riscv)
#define PROMEKI_ARCH_RISCV 1
#define PROMEKI_ARCH_NAME "riscv"
#elif defined(__powerpc64__) || defined(__ppc64__)
#define PROMEKI_ARCH_PPC64 1
#define PROMEKI_ARCH_64 1
#define PROMEKI_ARCH_NAME "ppc64"
#elif defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
#define PROMEKI_ARCH_WASM 1
#if defined(__wasm64__)
#define PROMEKI_ARCH_64 1
#endif
#define PROMEKI_ARCH_NAME "wasm"
#else
#define PROMEKI_ARCH_UNKNOWN 1
#define PROMEKI_ARCH_NAME "Unknown"
#endif

// ============================================================================
// ISA capability detection
// ============================================================================
//
// Each PROMEKI_HAS_<FEATURE> is set to 1 when the corresponding instruction
// set is available at compile time and the matching intrinsics header has
// been pre-included by this header (or is safe to include).  Code that wants
// to gate a SIMD path keys off these rather than off the arch macros — the
// arch is necessary but not sufficient (e.g. -march=armv8-a always has NEON,
// but armv7a builds may be compiled with or without -mfpu=neon).
//
// On x86, GCC / Clang define __SSE2__ / __SSE4_2__ / __AVX__ / __AVX2__ when
// the compiler has been told to target those.  SSE2 is part of the x86_64
// ABI so it is always available on x86_64.  On aarch64, NEON is part of the
// base ABI so it is always available; on arm32, __ARM_NEON gates it.

#if defined(__SSE2__) || defined(PROMEKI_ARCH_X86_64)
#define PROMEKI_HAS_SSE2 1
#endif
#if defined(__SSE3__)
#define PROMEKI_HAS_SSE3 1
#endif
#if defined(__SSSE3__)
#define PROMEKI_HAS_SSSE3 1
#endif
#if defined(__SSE4_1__)
#define PROMEKI_HAS_SSE4_1 1
#endif
#if defined(__SSE4_2__)
#define PROMEKI_HAS_SSE4_2 1
#endif
#if defined(__AVX__)
#define PROMEKI_HAS_AVX 1
#endif
#if defined(__AVX2__)
#define PROMEKI_HAS_AVX2 1
#endif
#if defined(__AVX512F__)
#define PROMEKI_HAS_AVX512F 1
#endif

// aarch64 always has NEON (mandatory in the base ABI); on arm32 it's gated
// by the compiler's -mfpu=neon flag, which sets __ARM_NEON.
#if defined(PROMEKI_ARCH_AARCH64) || defined(__ARM_NEON)
#define PROMEKI_HAS_NEON 1
#endif

// Convenience: any 128-bit float vector ISA we know how to write intrinsics
// for from a single PROMEKI_HAS_SIMD128 gate.  Code that just wants "do I
// have a 128-bit float vector unit" can key off this.
#if defined(PROMEKI_HAS_SSE2) || defined(PROMEKI_HAS_NEON)
#define PROMEKI_HAS_SIMD128 1
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

#endif // PROMEKI_ENABLE_CORE
