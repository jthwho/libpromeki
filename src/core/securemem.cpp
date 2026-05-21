/**
 * @file      securemem.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/securemem.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SecureMem)

Error secureLock(void *ptr, size_t size) {
        if (ptr == nullptr || size == 0) return Error::Ok;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        if (VirtualLock(ptr, size) == 0) {
                Error e = Error::syserr();
                promekiWarn("secureLock(%p, %zu) VirtualLock failed: %s", ptr, size, e.name().cstr());
                return e;
        }
        return Error::Ok;
#elif defined(PROMEKI_PLATFORM_POSIX)
        if (mlock(ptr, size) != 0) {
                Error e = Error::syserr();
                promekiWarn("secureLock(%p, %zu) mlock failed: %s (errno=%d) — RLIMIT_MEMLOCK?",
                            ptr, size, e.name().cstr(), e.systemError());
                return e;
        }
#if defined(PROMEKI_PLATFORM_LINUX)
        if (madvise(ptr, size, MADV_DONTDUMP) != 0) {
                Error err = Error::syserr();
                promekiWarn("secureLock(%p, %zu): madvise(MADV_DONTDUMP) failed (%s); region is locked but may appear "
                            "in core dumps",
                            ptr, size, err.desc().cstr());
        }
#endif
        return Error::Ok;
#else
        promekiWarnOnce("secureLock refused: not supported on this platform");
        return Error::NotSupported;
#endif
}

Error secureUnlock(void *ptr, size_t size) {
        if (ptr == nullptr || size == 0) return Error::Ok;
#if defined(PROMEKI_PLATFORM_WINDOWS)
        if (VirtualUnlock(ptr, size) == 0) {
                Error e = Error::syserr();
                promekiWarn("secureUnlock(%p, %zu) VirtualUnlock failed: %s", ptr, size, e.name().cstr());
                return e;
        }
        return Error::Ok;
#elif defined(PROMEKI_PLATFORM_POSIX)
#if defined(PROMEKI_PLATFORM_LINUX)
        if (madvise(ptr, size, MADV_DODUMP) != 0) {
                Error err = Error::syserr();
                promekiWarn("secureUnlock(%p, %zu): madvise(MADV_DODUMP) failed (%s); region remains excluded from "
                            "core dumps",
                            ptr, size, err.desc().cstr());
        }
#endif
        if (munlock(ptr, size) != 0) {
                Error e = Error::syserr();
                promekiWarn("secureUnlock(%p, %zu) munlock failed: %s (errno=%d)",
                            ptr, size, e.name().cstr(), e.systemError());
                return e;
        }
        return Error::Ok;
#else
        promekiWarnOnce("secureUnlock refused: not supported on this platform");
        return Error::NotSupported;
#endif
}

PROMEKI_NAMESPACE_END
