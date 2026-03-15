/**
 * @file      buffer.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/core/buffer.h>
#include <promeki/core/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <unistd.h>
#endif

PROMEKI_NAMESPACE_BEGIN

const size_t Buffer::DefaultAlign = getPageSize();

size_t Buffer::getPageSize() {
        static size_t ret = 0;
        if(ret == 0) {
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

PROMEKI_NAMESPACE_END

