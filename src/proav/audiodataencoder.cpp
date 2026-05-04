/**
 * @file      audiodataencoder.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <vector>
#include <promeki/audiodataencoder.h>
#include <promeki/audioformat.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/crc.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Render @p halfSamples copies of the float value @p amp into a
        // freshly-allocated Buffer in the target format.  The encoder
        // uses @ref AudioFormat::floatToSamples for the format-specific
        // quantisation so integer formats land on the format's
        // mid-scale-relative grid (e.g. 0.0f → 128 for unsigned 8-bit).
        Buffer buildPrimer(const AudioFormat &fmt, float amp, size_t halfSamples) {
                std::vector<float> floats(halfSamples, amp);
                Buffer             out(halfSamples * fmt.bytesPerSample());
                if (!out.isValid()) return Buffer();
                fmt.floatToSamples(static_cast<uint8_t *>(out.data()), floats.data(), halfSamples);
                out.setSize(halfSamples * fmt.bytesPerSample());
                return out;
        }

        // Single-sample silence primer (used for trailing pad fill).
        Buffer buildSilence(const AudioFormat &fmt) {
                const float zero = 0.0f;
                Buffer      out(fmt.bytesPerSample());
                if (!out.isValid()) return Buffer();
                fmt.floatToSamples(static_cast<uint8_t *>(out.data()), &zero, 1);
                out.setSize(fmt.bytesPerSample());
                return out;
        }

        // Copy a strided run of single-channel samples.  @p src holds
        // @p sampleCount contiguous samples in the target format;
        // @p dest is the per-channel base pointer of the destination
        // buffer with @p destStride bytes between consecutive samples
        // of the same channel.  For planar formats @p destStride
        // equals @p bytesPerSample so this is effectively a memcpy;
        // for interleaved formats it is the per-frame stride and the
        // copy walks one sample at a time.
        void writeSamples(uint8_t *dest, size_t destStride, const uint8_t *src, size_t bytesPerSample,
                          size_t sampleCount) {
                if (destStride == bytesPerSample) {
                        // Planar — single contiguous memcpy.
                        std::memcpy(dest, src, sampleCount * bytesPerSample);
                        return;
                }
                for (size_t i = 0; i < sampleCount; i++) {
                        std::memcpy(dest + i * destStride, src + i * bytesPerSample, bytesPerSample);
                }
        }

        // Fill a strided run with copies of a single sample value
        // (used for the trailing silence pad — one source sample
        // replicated across @p sampleCount destination slots).
        void fillSamples(uint8_t *dest, size_t destStride, const uint8_t *oneSample, size_t bytesPerSample,
                         size_t sampleCount) {
                for (size_t i = 0; i < sampleCount; i++) {
                        std::memcpy(dest + i * destStride, oneSample, bytesPerSample);
                }
        }

} // namespace

AudioDataEncoder::AudioDataEncoder(const AudioDesc &desc, uint32_t samplesPerBit, float amplitude)
    : _desc(desc), _samplesPerBit(samplesPerBit), _amplitude(amplitude) {
        if (!_desc.isValid()) return;
        if (_desc.isCompressed()) {
                promekiErr("AudioDataEncoder: compressed formats are not supported");
                return;
        }
        if (samplesPerBit < MinSamplesPerBit || samplesPerBit > MaxSamplesPerBit || (samplesPerBit & 1u) != 0) {
                promekiErr("AudioDataEncoder: samplesPerBit %u out of range [%u..%u, even]", samplesPerBit,
                           MinSamplesPerBit, MaxSamplesPerBit);
                return;
        }
        if (!(amplitude > 0.0f && amplitude <= 1.0f)) {
                promekiErr("AudioDataEncoder: amplitude %f outside (0,1]", static_cast<double>(amplitude));
                return;
        }
        const AudioFormat &fmt = _desc.format();
        if (fmt.bytesPerSample() == 0) {
                promekiErr("AudioDataEncoder: format %s reports zero bytesPerSample", fmt.name().cstr());
                return;
        }
        _bytesPerSample = fmt.bytesPerSample();
        _channelStride = _desc.bytesPerSampleStride();

        const size_t halfSamples = _samplesPerBit / 2;
        _posHalf = buildPrimer(fmt, +amplitude, halfSamples);
        _negHalf = buildPrimer(fmt, -amplitude, halfSamples);
        _silenceSample = buildSilence(fmt);
        if (!_posHalf.isValid() || !_negHalf.isValid() || !_silenceSample.isValid()) {
                promekiErr("AudioDataEncoder: failed to build primer buffers");
                return;
        }
        _valid = true;
}

uint8_t AudioDataEncoder::computeCrc(uint64_t payload) {
        uint8_t bytes[8];
        for (int b = 0; b < 8; b++) {
                bytes[b] = static_cast<uint8_t>((payload >> ((7 - b) * 8)) & 0xffu);
        }
        Crc8 crc(CrcParams::Crc8Autosar);
        crc.update(bytes, 8);
        return crc.value();
}

Error AudioDataEncoder::stampOne(uint8_t *channelBase, const Item &item) const {
        const size_t   halfSamples = _samplesPerBit / 2;
        const size_t   stride = _channelStride;
        const size_t   bps = _bytesPerSample;
        const uint8_t *posSrc = static_cast<const uint8_t *>(_posHalf.data());
        const uint8_t *negSrc = static_cast<const uint8_t *>(_negHalf.data());

        const uint64_t payloadBits = item.payload;
        const uint8_t  syncBits = SyncNibble;
        const uint8_t  crcBits = computeCrc(payloadBits);

        size_t sampleCursor = 0;

        // Helper lambda: write one Manchester-encoded bit at the
        // current cursor.  bit '1' = +A then -A; bit '0' = -A then +A.
        const auto writeBit = [&](bool bit) {
                uint8_t *dst = channelBase + sampleCursor * stride;
                if (bit) {
                        writeSamples(dst, stride, posSrc, bps, halfSamples);
                        writeSamples(dst + halfSamples * stride, stride, negSrc, bps, halfSamples);
                } else {
                        writeSamples(dst, stride, negSrc, bps, halfSamples);
                        writeSamples(dst + halfSamples * stride, stride, posSrc, bps, halfSamples);
                }
                sampleCursor += _samplesPerBit;
        };

        // Sync nibble (4 bits, MSB-first).
        for (int i = SyncBits - 1; i >= 0; --i) {
                writeBit(((syncBits >> i) & 1u) != 0);
        }
        // Payload (64 bits, MSB-first).
        for (int i = PayloadBits - 1; i >= 0; --i) {
                writeBit(((payloadBits >> i) & 1u) != 0);
        }
        // CRC (8 bits, MSB-first).
        for (int i = CrcBits - 1; i >= 0; --i) {
                writeBit(((crcBits >> i) & 1u) != 0);
        }

        // Trailing pad — silence to fill out the Item.
        if (item.sampleCount > sampleCursor) {
                const size_t padSamples = item.sampleCount - sampleCursor;
                fillSamples(channelBase + sampleCursor * stride, stride,
                            static_cast<const uint8_t *>(_silenceSample.data()), bps, padSamples);
        }

        return Error::Ok;
}

Error AudioDataEncoder::encode(PcmAudioPayload &inout, const List<Item> &items) const {
        if (!_valid) return Error::Invalid;
        if (!inout.isValid()) return Error::Invalid;
        if (inout.desc().format() != _desc.format() || inout.desc().sampleRate() != _desc.sampleRate() ||
            inout.desc().channels() != _desc.channels()) {
                return Error::InvalidArgument;
        }
        if (inout.planeCount() < 1) return Error::Invalid;

        const size_t   bufferSamples = inout.sampleCount();
        const uint64_t pkt = packetSamples();

        // For planar / interleaved both, the unified layout is a
        // single contiguous buffer.  channelBufferOffset gives the
        // starting byte for each channel inside that buffer.
        uint8_t *base = inout.data()[0].data();
        if (base == nullptr) return Error::Invalid;

        for (const Item &it : items) {
                if (it.sampleCount == 0) continue;
                if (it.channel >= _desc.channels()) return Error::InvalidArgument;
                if (it.sampleCount < pkt) return Error::OutOfRange;
                if (it.firstSample + it.sampleCount > bufferSamples) return Error::OutOfRange;

                const size_t channelOffset = _desc.channelBufferOffset(it.channel, bufferSamples);
                uint8_t     *channelBase = base + channelOffset + it.firstSample * _channelStride;
                Error        err = stampOne(channelBase, it);
                if (err.isError()) return err;
        }
        return Error::Ok;
}

Error AudioDataEncoder::encode(PcmAudioPayload &inout, const Item &item) const {
        List<Item> single;
        single.pushToBack(item);
        return encode(inout, single);
}

PROMEKI_NAMESPACE_END
