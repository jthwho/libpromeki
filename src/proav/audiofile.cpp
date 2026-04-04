/**
 * @file      audiofile.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/audiofile.h>
#include <promeki/audiofilefactory.h>
#include <promeki/iodevice.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

AudioFile AudioFile::createForOperation(Operation op, const String &fn) {
        AudioFileFactory::Context ctx;
        ctx.operation = op;
        ctx.filename = fn;
        const AudioFileFactory *factory = AudioFileFactory::lookup(ctx);
        if(factory == nullptr) {
                promekiWarn("Failed to find audio file factory for operation %d, fn '%s'", op, fn.cstr());
                return AudioFile();
        }
        auto [ret, err] = factory->createForOperation(ctx);
        if(err.isError() || !ret.isValid()) {
                promekiWarn("Factory %s couldn't create for %d, fn '%s'", factory->name().cstr(), op, fn.cstr());
                return AudioFile();
        }
        ret.setFilename(fn);
        return ret;
}

Result<AudioFile> AudioFile::createForOperation(Operation op, IODevice *device, const String &formatHint) {
        if(device == nullptr) {
                return makeError<AudioFile>(Error::InvalidArgument);
        }
        if(device->isSequential()) {
                return makeError<AudioFile>(Error::NotSupported);
        }
        AudioFileFactory::Context ctx;
        ctx.operation = op;
        ctx.formatHint = formatHint;
        ctx.device = device;
        const AudioFileFactory *factory = AudioFileFactory::lookup(ctx);
        if(factory == nullptr) {
                promekiWarn("Failed to find audio file factory for operation %d, hint '%s'",
                        op, formatHint.cstr());
                return makeError<AudioFile>(Error::NotSupported);
        }
        auto [ret, err] = factory->createForOperation(ctx);
        if(err.isError() || !ret.isValid()) {
                promekiWarn("Factory %s couldn't create for %d, hint '%s'",
                        factory->name().cstr(), op, formatHint.cstr());
                return makeError<AudioFile>(err.isError() ? err : Error::NotSupported);
        }
        ret.d.modify()->setDevice(device);
        ret.d.modify()->setFormatHint(formatHint);
        return makeResult(ret);
}

AudioFile::Impl::~Impl() {
        if(_ownsDevice && _device != nullptr) {
                delete _device;
                _device = nullptr;
        }
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
