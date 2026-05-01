/**
 * @file      securemem.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/platform.h>
#include <promeki/error.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#include <memoryapi.h>
#elif defined(PROMEKI_PLATFORM_POSIX)
#include <sys/mman.h>
#include <string.h> // explicit_bzero
#endif

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Securely zeros a memory region, guaranteed not to be optimized away.
 * @ingroup util
 *
 * Uses platform-specific secure zeroing primitives when available:
 * - Windows: SecureZeroMemory
 * - glibc 2.25+/BSD: explicit_bzero
 * - Fallback: volatile pointer write loop
 *
 * This function cannot fail. If ptr is nullptr or size is 0, it is a no-op.
 *
 * @param ptr  Pointer to the memory region to zero.
 * @param size Number of bytes to zero.
 */
inline void secureZero(void *ptr, size_t size) {
        if (ptr == nullptr || size == 0) return;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        SecureZeroMemory(ptr, size);
#elif defined(PROMEKI_LIBC_GLIBC) &&                                                                                   \
        (PROMEKI_LIBC_GLIBC_VERSION_MAJOR > 2 ||                                                                       \
         (PROMEKI_LIBC_GLIBC_VERSION_MAJOR == 2 && PROMEKI_LIBC_GLIBC_VERSION_MINOR >= 25))
        explicit_bzero(ptr, size);
#elif defined(PROMEKI_PLATFORM_BSD)
        explicit_bzero(ptr, size);
#else
        volatile unsigned char *p = static_cast<volatile unsigned char *>(ptr);
        while (size--) *p++ = 0;
#endif
}

/**
 * @brief Locks a memory region into physical RAM, preventing it from being swapped to disk.
 *
 * Also marks the region as excluded from core dumps on Linux (MADV_DONTDUMP).
 * If `madvise` fails (e.g. on a kernel without MADV_DONTDUMP support) the
 * function still returns Error::Ok, since the lock itself succeeded; a warning
 * is logged so the missing dump-exclusion is observable.
 *
 * @param ptr  Pointer to the memory region to lock.
 * @param size Size of the region in bytes.
 * @return Error::Ok on success, or the system error on failure (e.g. NoMem
 *         for RLIMIT_MEMLOCK exceeded, NoPermission for missing CAP_IPC_LOCK).
 */
Error secureLock(void *ptr, size_t size);

/**
 * @brief Unlocks a memory region previously locked with secureLock().
 *
 * Reverses the effects of secureLock(), allowing the OS to swap the pages
 * and include them in core dumps again. As with secureLock(), a `madvise`
 * failure on Linux is logged as a warning but does not turn the operation
 * into an error.
 *
 * @param ptr  Pointer to the memory region to unlock.
 * @param size Size of the region in bytes.
 * @return Error::Ok on success, or the system error on failure.
 */
Error secureUnlock(void *ptr, size_t size);

PROMEKI_NAMESPACE_END
