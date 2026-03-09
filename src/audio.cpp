/**
 * @file      audio.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

