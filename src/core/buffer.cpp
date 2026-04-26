/**
 * @file      buffer.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/buffer.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

const size_t Buffer::DefaultAlign = getPageSize();

size_t Buffer::getPageSize() {
        static size_t ret = 0;
        if (ret == 0) {
#if defined(PROMEKI_PLATFORM_WINDOWS)
                SYSTEM_INFO sysInfo;
                GetSystemInfo(&sysInfo);
                ret = sysInfo.dwPageSize;
#else
                ret = sysconf(_SC_PAGESIZE);
#endif
        }
        return ret;
}

Error Buffer::copyFrom(const void *src, size_t bytes, size_t offset) const {
        if (!isValid()) return Error::Invalid;
        if (!isHostAccessible()) return Error::NotHostAccessible;
        if (offset + bytes > availSize()) return Error::BufferTooSmall;
        std::memcpy(static_cast<uint8_t *>(_data) + offset, src, bytes);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
