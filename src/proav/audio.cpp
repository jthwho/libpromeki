/**
 * @file      audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <promeki/audio.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

Audio::Audio(const AudioDesc &desc, size_t samples, const MemSpace &ms) :
        _desc(desc), _samples(samples), _maxSamples(samples) {
        allocate(ms);
}

Audio Audio::convertTo(AudioDesc::DataType format) const {
        if(!isValid()) return Audio();
        if(_desc.dataType() == format) return *this;

        const AudioDesc::Format *srcFmt = AudioDesc::lookupFormat(_desc.dataType());
        const AudioDesc::Format *dstFmt = AudioDesc::lookupFormat(format);
        if(srcFmt == nullptr || dstFmt == nullptr) return Audio();
        if(dstFmt->bytesPerSample == 0) return Audio();

        AudioDesc dstDesc(format, _desc.sampleRate(), _desc.channels());
        if(!dstDesc.isValid()) return Audio();

        Audio result(dstDesc, _samples);
        if(!result.isValid()) return Audio();

        size_t totalSamples = _samples * _desc.channels();
        const uint8_t *srcData = static_cast<const uint8_t *>(_buffer->data());
        uint8_t *dstData = static_cast<uint8_t *>(result._buffer->data());

        // Fast path: source is native float — single pass, no intermediate buffer
        if(_desc.dataType() == AudioDesc::NativeType) {
                const float *floatData = reinterpret_cast<const float *>(srcData);
                dstFmt->floatToSamples(dstData, floatData, totalSamples);
                return result;
        }

        // Fast path: target is native float — single pass
        if(format == AudioDesc::NativeType) {
                float *floatData = reinterpret_cast<float *>(dstData);
                srcFmt->samplesToFloat(floatData, srcData, totalSamples);
                return result;
        }

        // General case: source → float → target (two passes)
        float *tmp = static_cast<float *>(std::malloc(totalSamples * sizeof(float)));
        if(tmp == nullptr) return Audio();
        srcFmt->samplesToFloat(tmp, srcData, totalSamples);
        dstFmt->floatToSamples(dstData, tmp, totalSamples);
        std::free(tmp);
        return result;
}

bool Audio::allocate(const MemSpace &ms) {
        size_t size = _desc.bufferSize(_samples);
        _buffer = Buffer::Ptr::create(size, Buffer::DefaultAlign, ms);
        if(!_buffer->isValid()) {
                promekiErr("Audio(%s, %d samples) allocate %d failed",
                        _desc.toString().cstr(), (int)_samples, (int)size);
                return false;
        }
        _buffer->setSize(size);
        return true;
}

PROMEKI_NAMESPACE_END
