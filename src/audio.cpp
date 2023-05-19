/*****************************************************************************
 * audio.cpp
 * May 17, 2023
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

#include <cstdint>
#include <limits>
#include <promeki/audio.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

bool Audio::Data::allocate(const MemSpace &ms) {
        size_t size = desc.bufferSize(samples);
        Buffer b = Buffer(size, Buffer::DefaultAlign, ms);
        if(!b.isValid()) {
                promekiErr("Audio(%s, %d samples) allocate %d failed", 
                        desc.toString().cstr(), (int)samples, (int)size);
                return false;
        }
        buffer = b;
        return true;
}

PROMEKI_NAMESPACE_END

