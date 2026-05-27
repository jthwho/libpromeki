/**
 * @file      pcmsilencefiller.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/audioformat.h>
#include <promeki/pcmsilencefiller.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

// Writes one big-endian unsigned-integer midpoint into @p p of width
// @p bytes.  Used to seed an unsigned-PCM silence packet in big-
// endian wire formats.  The midpoint is 1 << (bits - 1) — i.e.
// 0x80 for 8-bit, 0x8000 for 16-bit, etc.
void writeMidpointBE(uint8_t *p, size_t bytes) {
        if (bytes == 0) return;
        std::memset(p, 0, bytes);
        p[0] = static_cast<uint8_t>(0x80);
}

// Same as @ref writeMidpointBE but for little-endian — the
// midpoint byte sits at the high index instead of index 0.
void writeMidpointLE(uint8_t *p, size_t bytes) {
        if (bytes == 0) return;
        std::memset(p, 0, bytes);
        p[bytes - 1] = static_cast<uint8_t>(0x80);
}

// Populates @p data with @p samplesPerPacket samples of silence
// for the supplied @p desc in either interleaved or planar layout
// — both layouts collapse to "fill N × channels samples with the
// per-sample silence value", so there's no separate planar path.
void fillSilence(uint8_t *data, size_t samplesPerPacket, const AudioDesc &desc) {
        const size_t bps = desc.bytesPerSample();
        const size_t totalSamples = samplesPerPacket * desc.channels();
        if (data == nullptr || bps == 0 || totalSamples == 0) return;

        const AudioFormat &fmt = desc.format();
        // Signed integer + float: silence is bit-exactly zero.  Float
        // zero is +0.0 in IEEE 754, which has the all-zero bit
        // pattern at any width and any endianness — so a single
        // memset covers both signed-int and float wire formats.
        if (fmt.isFloat() || fmt.isSigned()) {
                std::memset(data, 0, totalSamples * bps);
                return;
        }

        // Unsigned integer: silence is the per-sample midpoint
        // (0x80 << (bits - 8)).  The byte pattern depends on
        // endianness.  Walk every sample slot and stamp the
        // appropriate pattern.
        const bool be = fmt.isBigEndian();
        for (size_t i = 0; i < totalSamples; ++i) {
                uint8_t *p = data + i * bps;
                if (be) {
                        writeMidpointBE(p, bps);
                } else {
                        writeMidpointLE(p, bps);
                }
        }
}

} // namespace

PcmSilenceFiller::PcmSilenceFiller(const AudioDesc &desc, size_t samplesPerPacket) {
        (void)reset(desc, samplesPerPacket);
}

Error PcmSilenceFiller::reset(const AudioDesc &desc, size_t samplesPerPacket) {
        _desc = AudioDesc();
        _samplesPerPacket = 0;
        _payload = Buffer();
        if (!desc.isValid()) return Error::InvalidArgument;
        _desc = desc;
        _samplesPerPacket = samplesPerPacket;
        const size_t bytes = desc.bufferSize(samplesPerPacket);
        if (bytes == 0) {
                // Legal: zero samples produces an empty silence
                // payload (a stream that's been configured with
                // samplesPerPacket=0 simply emits nothing).
                _payload = Buffer();
                return Error::Ok;
        }
        Buffer buf(bytes);
        buf.setSize(bytes);
        fillSilence(static_cast<uint8_t *>(buf.data()), samplesPerPacket, desc);
        _payload = std::move(buf);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
