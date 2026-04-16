/**
 * @file      audio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <promeki/audio.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

Audio::Audio(const AudioDesc &desc, size_t samples, const MemSpace &ms) :
        _desc(desc), _samples(samples), _maxSamples(samples) {
        allocate(ms);
}

Audio Audio::fromBuffer(const Buffer::Ptr &buffer, const AudioDesc &desc) {
        if(!buffer.isValid() || !desc.isValid()) return Audio();

        Audio out;
        out._desc = desc;
        out._buffer = buffer;
        if(desc.isCompressed()) {
                // For compressed audio we don't know "sample count" in PCM
                // terms — leave _samples = 0 and report the byte count
                // via compressedSize().
                out._samples    = 0;
                out._maxSamples = 0;
        } else {
                size_t stride = desc.bytesPerSampleStride();
                if(stride == 0) return Audio();
                size_t total = buffer->size() / stride;
                out._samples    = total;
                out._maxSamples = total;
        }
        return out;
}

Audio Audio::fromCompressedData(const void *data, size_t size, const AudioDesc &desc) {
        if(!data || size == 0 || !desc.isCompressed()) return Audio();
        Buffer buf(size);
        if(!buf.isValid()) return Audio();
        std::memcpy(buf.data(), data, size);
        buf.setSize(size);
        return fromBuffer(Buffer::Ptr::create(std::move(buf)), desc);
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

std::optional<String> Audio::resolveTemplateKey(const String &key, const String &spec) const {
        if(!key.isEmpty() && key.cstr()[0] == '@') {
                return resolvePseudoKey(key, spec);
        }
        Metadata::ID id = Metadata::ID::find(key);
        if(id.isValid() && _desc.metadata().contains(id)) {
                return _desc.metadata().get(id).format(spec);
        }
        return std::nullopt;
}

std::optional<String> Audio::resolvePseudoKey(const String &key, const String &spec) const {
        Variant v;
        if(key == String("@SampleRate"))           v = _desc.sampleRate();
        else if(key == String("@Channels"))        v = static_cast<uint32_t>(_desc.channels());
        else if(key == String("@DataType"))        v = _desc.dataTypeName();
        else if(key == String("@Samples"))         v = static_cast<uint64_t>(_samples);
        else if(key == String("@MaxSamples"))      v = static_cast<uint64_t>(_maxSamples);
        else if(key == String("@Frames"))          v = static_cast<uint64_t>(frames());
        else if(key == String("@BytesPerSample"))  v = static_cast<uint64_t>(_desc.bytesPerSample());
        else if(key == String("@IsValid"))         v = isValid();
        else if(key == String("@IsCompressed"))    v = isCompressed();
        else if(key == String("@IsNative"))        v = isNative();
        else if(key == String("@CompressedSize"))  v = static_cast<uint64_t>(compressedSize());
        else if(key == String("@CodecFourCC")) {
                FourCC fc = _desc.codecFourCC();
                uint32_t raw = fc.value();
                if(raw == 0) {
                        v = String();
                } else {
                        char buf[5];
                        buf[0] = static_cast<char>((raw >> 24) & 0xFF);
                        buf[1] = static_cast<char>((raw >> 16) & 0xFF);
                        buf[2] = static_cast<char>((raw >>  8) & 0xFF);
                        buf[3] = static_cast<char>( raw        & 0xFF);
                        buf[4] = '\0';
                        v = String(buf);
                }
        }
        else return std::nullopt;
        return v.format(spec);
}

PROMEKI_NAMESPACE_END
