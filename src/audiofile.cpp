/*****************************************************************************
 * audiofile.cpp
 * May 18, 2023
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

#include <promeki/audiofile.h>
#include <promeki/audiofilefactory.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

AudioFile AudioFile::createForOperation(Operation op, const String &fn) {
        const AudioFileFactory *factory = AudioFileFactory::lookup(op, fn);
        if(factory == nullptr) {
                promekiWarn("Failed to find audio file factory for operation %d, fn '%s'", op, fn.cstr());
                return AudioFile();
        }
        AudioFile ret = factory->createForOperation(op);
        if(!ret.isValid()) {
                promekiWarn("Factory %s couldn't create for %d, fn '%s'", factory->name().cstr(), op, fn.cstr());
                return ret;
        }
        ret.setFilename(fn);
        return ret;
}

AudioFile::Impl::~Impl() {

}

Error AudioFile::Impl::open() {
        return Error::Invalid;
}

void AudioFile::Impl::close() {
        return;
}

Error AudioFile::Impl::read(Audio &audio, size_t maxSamples) {
        return Error::Invalid;
}

Error AudioFile::Impl::write(const Audio &audio) {
        return Error::Invalid;
}

Error AudioFile::Impl::seekToSample(size_t sample) {
        return Error::Invalid;
}

size_t AudioFile::Impl::sampleCount() const {
        return 0;
}

PROMEKI_NAMESPACE_END

