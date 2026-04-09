/**
 * @file      quicktime.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/quicktime.h>
#include <promeki/iodevice.h>

#include "quicktime_reader.h"
#include "quicktime_writer.h"

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// QuickTime::Impl — concrete base with default "not implemented" behavior.
// ---------------------------------------------------------------------------

QuickTime::Impl::~Impl() {
        if(_ownsDevice && _device != nullptr) {
                delete _device;
                _device = nullptr;
        }
}

Error QuickTime::Impl::open() {
        return Error::NotImplemented;
}

void QuickTime::Impl::close() {
        return;
}

bool QuickTime::Impl::isOpen() const {
        return false;
}

Error QuickTime::Impl::readSample(size_t /*trackIndex*/, uint64_t /*sampleIndex*/,
                                  QuickTime::Sample & /*out*/) {
        return Error::NotImplemented;
}

Error QuickTime::Impl::readSampleRange(size_t trackIndex, uint64_t startSampleIndex,
                                       uint64_t count, QuickTime::Sample &out) {
        // Default: loop over readSample() and concatenate. Backends can
        // override this for efficient contiguous reads. This path is slow
        // for large counts — QuickTimeReader overrides it.
        if(count == 0) return Error::InvalidArgument;
        QuickTime::Sample first;
        Error err = readSample(trackIndex, startSampleIndex, first);
        if(err.isError()) return err;
        if(count == 1) { out = first; return Error::Ok; }

        // Compute total size by reading each sample up-front (slow path).
        size_t totalBytes = first.data.isValid() ? first.data->size() : 0;
        List<QuickTime::Sample> samples;
        samples.pushToBack(first);
        for(uint64_t i = 1; i < count; ++i) {
                QuickTime::Sample s;
                err = readSample(trackIndex, startSampleIndex + i, s);
                if(err.isError()) return err;
                totalBytes += s.data.isValid() ? s.data->size() : 0;
                samples.pushToBack(s);
        }

        Buffer cat(totalBytes);
        uint8_t *dst = static_cast<uint8_t *>(cat.data());
        size_t pos = 0;
        for(const auto &s : samples) {
                if(!s.data.isValid()) continue;
                std::memcpy(dst + pos, s.data->data(), s.data->size());
                pos += s.data->size();
        }
        cat.setSize(pos);

        out = first;
        out.data = Buffer::Ptr::create(std::move(cat));
        return Error::Ok;
}

Error QuickTime::Impl::addVideoTrack(const PixelDesc & /*codec*/, const Size2Du32 & /*size*/,
                                     const FrameRate & /*frameRate*/, uint32_t * /*outTrackId*/) {
        return Error::NotImplemented;
}

Error QuickTime::Impl::addAudioTrack(const AudioDesc & /*desc*/, uint32_t * /*outTrackId*/) {
        return Error::NotImplemented;
}

Error QuickTime::Impl::addTimecodeTrack(const Timecode & /*startTimecode*/,
                                        const FrameRate & /*frameRate*/, uint32_t * /*outTrackId*/) {
        return Error::NotImplemented;
}

Error QuickTime::Impl::writeSample(uint32_t /*trackId*/, const QuickTime::Sample & /*sample*/) {
        return Error::NotImplemented;
}

void QuickTime::Impl::setContainerMetadata(const Metadata & /*meta*/) {
        return;
}

Error QuickTime::Impl::setLayout(QuickTime::Layout layout) {
        _layout = layout;
        return Error::Ok;
}

Error QuickTime::Impl::flush() {
        // Default: no-op. Classic writers have no fragments to flush,
        // and readers don't write.
        return Error::Ok;
}

Error QuickTime::Impl::finalize() {
        return Error::NotImplemented;
}

// ---------------------------------------------------------------------------
// Factories
// ---------------------------------------------------------------------------

QuickTime QuickTime::createReader(const String &filename) {
        QuickTime qt(new QuickTimeReader());
        qt.setFilename(filename);
        return qt;
}

QuickTime QuickTime::createWriter(const String &filename) {
        QuickTime qt(new QuickTimeWriter());
        qt.setFilename(filename);
        return qt;
}

Result<QuickTime> QuickTime::createForOperation(Operation op, IODevice *device) {
        if(device == nullptr) {
                return makeError<QuickTime>(Error::InvalidArgument);
        }
        if(device->isSequential()) {
                return makeError<QuickTime>(Error::NotSupported);
        }
        switch(op) {
                case Reader: {
                        QuickTime qt(new QuickTimeReader());
                        qt.d.modify()->setDevice(device, /*takeOwnership*/ false);
                        return makeResult(qt);
                }
                case Writer: {
                        QuickTime qt(new QuickTimeWriter());
                        qt.d.modify()->setDevice(device, /*takeOwnership*/ false);
                        return makeResult(qt);
                }
                default:
                        break;
        }
        return makeError<QuickTime>(Error::InvalidArgument);
}

PROMEKI_NAMESPACE_END
