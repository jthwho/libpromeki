/**
 * @file      audiofile.cpp
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

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

