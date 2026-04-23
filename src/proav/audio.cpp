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
#include <promeki/audiocodec.h>
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

Audio Audio::convert(const AudioFormat &format) const {
        if(!isValid() || !format.isValid()) return Audio();
        if(_desc.format() == format) return *this;
        if(format.bytesPerSample() == 0) return Audio();

        AudioDesc dstDesc(format, _desc.sampleRate(), _desc.channels());
        if(!dstDesc.isValid()) return Audio();

        // Allocate destination in the same MemSpace as the source so
        // the conversion stays put.  If the source isn't host-readable,
        // bail — Audio's sample conversion kernels are CPU-only.
        const MemSpace &srcMs = _buffer->memSpace();
        if(!srcMs.isHostAccessible(_buffer->allocation())) return Audio();

        Audio result(dstDesc, _samples, srcMs);
        if(!result.isValid()) return Audio();

        size_t totalSamples = _samples * _desc.channels();
        const uint8_t *srcData = static_cast<const uint8_t *>(_buffer->data());
        uint8_t *dstData = static_cast<uint8_t *>(result._buffer->data());

        // Fast path: source is native float — single pass, no intermediate buffer
        if(_desc.format().id() == AudioFormat::NativeFloat) {
                const float *floatData = reinterpret_cast<const float *>(srcData);
                format.floatToSamples(dstData, floatData, totalSamples);
                return result;
        }

        // Fast path: target is native float — single pass
        if(format.id() == AudioFormat::NativeFloat) {
                float *floatData = reinterpret_cast<float *>(dstData);
                _desc.format().samplesToFloat(floatData, srcData, totalSamples);
                return result;
        }

        // General case: source → float → target (two passes).  Stage
        // the float scratch in the same MemSpace as the source so we
        // don't bypass the pool with std::malloc.
        Buffer::Ptr tmpBuf = Buffer::Ptr::create(
                totalSamples * sizeof(float), Buffer::DefaultAlign, srcMs);
        if(!tmpBuf->isValid()) return Audio();
        float *tmp = static_cast<float *>(tmpBuf->data());
        _desc.format().samplesToFloat(tmp, srcData, totalSamples);
        format.floatToSamples(dstData, tmp, totalSamples);
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
        // Zero the backing allocation so partial fills and end-of-buffer
        // slack don't flow into sf_write/write as uninit samples.
        _buffer->fill(0);
        _buffer->setSize(size);
        return true;
}

bool Audio::isSafeCutPoint() const {
        if(!isValid()) return false;
        const AudioFormat &fmt = _desc.format();
        if(!fmt.isCompressed()) return true;
        const AudioCodec &codec = fmt.audioCodec();
        // A compressed format with no codec identity is a malformed
        // record — treat as unsafe rather than guessing.
        if(!codec.isValid()) return false;
        switch(codec.packetIndependence()) {
                case AudioCodec::PacketIndependenceEvery:
                        // Opus, PCM-in-container, FLAC frames — every
                        // packet decodes standalone.
                        return true;
                case AudioCodec::PacketIndependenceKeyframe:
                        // Codec marks discrete sync points, but
                        // AudioPacket carries no per-packet keyframe
                        // flag — callers needing this level of cut
                        // precision must inspect the codec-specific
                        // subclass they emitted.
                        return false;
                case AudioCodec::PacketIndependenceInter:
                        // Inter-dependent codecs (MP3 bit reservoir, AAC-LTP)
                        // have no random-access point inside the bitstream
                        // — every packet depends on prior state.
                        return false;
                case AudioCodec::PacketIndependenceInvalid:
                        return false;
        }
        return false;
}

StringList Audio::dump(const String &indent) const {
        StringList out;
        VariantLookup<Audio>::forEachScalar([this, &out, &indent](const String &name) {
                auto v = VariantLookup<Audio>::resolve(*this, name);
                if(v.has_value()) {
                        out += indent + name + ": " + v->format(String());
                }
        });
        if(_buffer.isValid()) {
                out += indent + String::sprintf("Buffer: size=%zu bytes (alloc=%zu, align=%zu)",
                                                _buffer->size(), _buffer->allocSize(), _buffer->align());
        }
        if(_packet.isValid()) {
                out += indent + String::sprintf("Packet: pts=%s dts=%s size=%zu",
                                                _packet->pts().toString().cstr(),
                                                _packet->dts().toString().cstr(),
                                                _packet->size());
        }
        StringList mdLines = _desc.metadata().dump();
        if(!mdLines.isEmpty()) {
                out += indent + "Meta:";
                String sub = indent + "  ";
                for(const String &ln : mdLines) out += sub + ln;
        }
        return out;
}

PROMEKI_LOOKUP_REGISTER(Audio)
        .scalar("SampleRate",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(a.desc().sampleRate());
                })
        .scalar("Channels",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint32_t>(a.desc().channels()));
                })
        .scalar("Format",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(a.desc().format());
                })
        .scalar("Samples",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(a.samples()));
                })
        .scalar("MaxSamples",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(a.maxSamples()));
                })
        .scalar("Frames",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(a.frames()));
                })
        .scalar("BytesPerSample",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(a.desc().bytesPerSample()));
                })
        .scalar("IsValid",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(a.isValid());
                })
        .scalar("IsCompressed",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(a.isCompressed());
                })
        .scalar("IsNative",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(a.isNative());
                })
        .scalar("CompressedSize",
                [](const Audio &a) -> std::optional<Variant> {
                        return Variant(static_cast<uint64_t>(a.compressedSize()));
                })
        .database<"Metadata">("Meta",
                [](const Audio &a) -> const VariantDatabase<"Metadata"> * {
                        return &a.metadata();
                },
                [](Audio &a) -> VariantDatabase<"Metadata"> * {
                        return &a.metadata();
                });

PROMEKI_NAMESPACE_END
