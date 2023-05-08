/*****************************************************************************
 * buffer.cpp
 * April 29, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#include <promeki/buffer.h>
#include <promeki/util.h>

#if defined(PROMEKI_PLATFORM_WINDOWS)
#include <Windows.h>
#else
#include <unistd.h>
#endif

namespace promeki {

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

} // namespace promeki

