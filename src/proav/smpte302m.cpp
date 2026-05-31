/**
 * @file      smpte302m.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/smpte302m.h>

#include <cstring>
#include <promeki/audiodesc.h>
#include <promeki/audioformat.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        constexpr unsigned kAes3BlockSize = 192; // 192 AES3 frames per channel-status block.

        // Read one PCM sample, normalise to a signed 32-bit value
        // right-aligned in the low @c bits bits (top bits sign-extended
        // / cleared per the format's signedness).  Returns the 16/20/24
        // significant bits packed in the low end; the high bits are 0.
        inline uint32_t fetchPcmSample(const uint8_t *src, AudioFormat::ID id) {
                switch (id) {
                        case AudioFormat::PCMI_S16LE: {
                                uint16_t v = static_cast<uint16_t>(src[0]) |
                                             (static_cast<uint16_t>(src[1]) << 8);
                                return v;
                        }
                        case AudioFormat::PCMI_S16BE: {
                                uint16_t v = (static_cast<uint16_t>(src[0]) << 8) |
                                             static_cast<uint16_t>(src[1]);
                                return v;
                        }
                        case AudioFormat::PCMI_S24LE: {
                                uint32_t v = static_cast<uint32_t>(src[0]) |
                                             (static_cast<uint32_t>(src[1]) << 8) |
                                             (static_cast<uint32_t>(src[2]) << 16);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S24BE: {
                                uint32_t v = (static_cast<uint32_t>(src[0]) << 16) |
                                             (static_cast<uint32_t>(src[1]) << 8) |
                                             static_cast<uint32_t>(src[2]);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S32LE: {
                                // Top 24 bits (drop the low byte).
                                uint32_t v = (static_cast<uint32_t>(src[1])) |
                                             (static_cast<uint32_t>(src[2]) << 8) |
                                             (static_cast<uint32_t>(src[3]) << 16);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S32BE: {
                                uint32_t v = (static_cast<uint32_t>(src[0]) << 16) |
                                             (static_cast<uint32_t>(src[1]) << 8) |
                                             static_cast<uint32_t>(src[2]);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S24LE_HB32: {
                                // 24 data bits in high 3 bytes of a 32-bit LE word.
                                uint32_t v = (static_cast<uint32_t>(src[1])) |
                                             (static_cast<uint32_t>(src[2]) << 8) |
                                             (static_cast<uint32_t>(src[3]) << 16);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S24LE_LB32: {
                                // 24 data bits in low 3 bytes of a 32-bit LE word.
                                uint32_t v = static_cast<uint32_t>(src[0]) |
                                             (static_cast<uint32_t>(src[1]) << 8) |
                                             (static_cast<uint32_t>(src[2]) << 16);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S24BE_HB32: {
                                uint32_t v = (static_cast<uint32_t>(src[0]) << 16) |
                                             (static_cast<uint32_t>(src[1]) << 8) |
                                             static_cast<uint32_t>(src[2]);
                                return v & 0xFFFFFFu;
                        }
                        case AudioFormat::PCMI_S24BE_LB32: {
                                uint32_t v = (static_cast<uint32_t>(src[1]) << 16) |
                                             (static_cast<uint32_t>(src[2]) << 8) |
                                             static_cast<uint32_t>(src[3]);
                                return v & 0xFFFFFFu;
                        }
                        default: return 0;
                }
        }

        // Bytes-per-sample stride in the input buffer.
        inline size_t sourceSampleStride(AudioFormat::ID id) {
                switch (id) {
                        case AudioFormat::PCMI_S16LE:
                        case AudioFormat::PCMI_S16BE: return 2;
                        case AudioFormat::PCMI_S24LE:
                        case AudioFormat::PCMI_S24BE: return 3;
                        case AudioFormat::PCMI_S32LE:
                        case AudioFormat::PCMI_S32BE:
                        case AudioFormat::PCMI_S24LE_HB32:
                        case AudioFormat::PCMI_S24LE_LB32:
                        case AudioFormat::PCMI_S24BE_HB32:
                        case AudioFormat::PCMI_S24BE_LB32: return 4;
                        default: return 0;
                }
        }

        // Bit-stream writer with MSB-first byte ordering.  Calls
        // append at the end of the active byte and writes bits into
        // the most-significant bit position first, which matches
        // standard MPEG serial-bit conventions.
        class BitWriter {
                public:
                        explicit BitWriter(uint8_t *dst) : _dst(dst) {}

                        // Write @p numBits of @p value, LSB first
                        // (i.e. bit 0 of @p value is emitted first
                        // into the bit-stream).
                        void writeBitsLsbFirst(uint32_t value, unsigned numBits) {
                                for (unsigned i = 0; i < numBits; ++i) {
                                        writeBit((value >> i) & 1u);
                                }
                        }

                        // Write @p numBits of @p value, MSB first.
                        void writeBitsMsbFirst(uint32_t value, unsigned numBits) {
                                for (unsigned i = numBits; i-- > 0;) {
                                        writeBit((value >> i) & 1u);
                                }
                        }

                        // Write a single bit (0 or 1).
                        void writeBit(unsigned bit) {
                                if (_bitPos == 0) _dst[_bytePos] = 0;
                                if (bit & 1u) _dst[_bytePos] |= static_cast<uint8_t>(1u << (7 - _bitPos));
                                ++_bitPos;
                                if (_bitPos == 8) {
                                        _bitPos = 0;
                                        ++_bytePos;
                                }
                        }

                        size_t bytesWritten() const {
                                return _bytePos + (_bitPos != 0 ? 1u : 0u);
                        }

                private:
                        uint8_t *_dst;
                        size_t   _bytePos = 0;
                        unsigned _bitPos = 0;
        };

        class BitReader {
                public:
                        BitReader(const uint8_t *src, size_t len) : _src(src), _len(len) {}

                        bool readBit(unsigned &out) {
                                if (_bytePos >= _len) return false;
                                out = (_src[_bytePos] >> (7 - _bitPos)) & 1u;
                                ++_bitPos;
                                if (_bitPos == 8) {
                                        _bitPos = 0;
                                        ++_bytePos;
                                }
                                return true;
                        }

                        bool readBitsLsbFirst(unsigned numBits, uint32_t &outValue) {
                                outValue = 0;
                                for (unsigned i = 0; i < numBits; ++i) {
                                        unsigned b = 0;
                                        if (!readBit(b)) return false;
                                        outValue |= (static_cast<uint32_t>(b) << i);
                                }
                                return true;
                        }

                private:
                        const uint8_t *_src;
                        size_t         _len;
                        size_t         _bytePos = 0;
                        unsigned       _bitPos = 0;
        };

        // Per-AES3-frame byte count: ceil((bits+4) * channels / 8).
        // For all (bps, channels) tuples in §5.2 / §5.3 the result is
        // an integer.
        inline size_t bytesPerFrameImpl(unsigned bitsPerSample, unsigned channels) {
                const unsigned bitsPerFrame = (bitsPerSample + 4) * channels;
                return static_cast<size_t>((bitsPerFrame + 7) / 8);
        }

        // Map raw BitsPerSample to numeric bit count (16 / 20 / 24).
        inline unsigned bitsCount(Smpte302M::BitsPerSample bps) {
                switch (bps) {
                        case Smpte302M::Bits16: return 16;
                        case Smpte302M::Bits20: return 20;
                        case Smpte302M::Bits24: return 24;
                }
                return 0;
        }

        // Map channel count (2/4/6/8) to the 2-bit number_channels field.
        inline int numberChannelsCode(unsigned channels) {
                switch (channels) {
                        case 2: return 0;
                        case 4: return 1;
                        case 6: return 2;
                        case 8: return 3;
                }
                return -1;
        }

        inline unsigned numberChannelsToChannels(uint8_t code) {
                switch (code & 0x3) {
                        case 0: return 2;
                        case 1: return 4;
                        case 2: return 6;
                        case 3: return 8;
                }
                return 0;
        }

        inline AudioFormat::ID outputFormatForBps(Smpte302M::BitsPerSample bps) {
                return (bps == Smpte302M::Bits16) ? AudioFormat::PCMI_S16LE : AudioFormat::PCMI_S24LE;
        }

} // namespace

int Smpte302M::bitsPerSampleCode(const AudioFormat &fmt) {
        if (!fmt.isValid()) return -1;
        switch (fmt.id()) {
                case AudioFormat::PCMI_S16LE:
                case AudioFormat::PCMI_S16BE: return Bits16;
                case AudioFormat::PCMI_S24LE:
                case AudioFormat::PCMI_S24BE:
                case AudioFormat::PCMI_S32LE:
                case AudioFormat::PCMI_S32BE:
                case AudioFormat::PCMI_S24LE_HB32:
                case AudioFormat::PCMI_S24LE_LB32:
                case AudioFormat::PCMI_S24BE_HB32:
                case AudioFormat::PCMI_S24BE_LB32: return Bits24;
                default: return -1;
        }
}

bool Smpte302M::isFormatSupported(const AudioFormat &fmt) {
        return bitsPerSampleCode(fmt) >= 0;
}

size_t Smpte302M::bytesPerAes3Frame(BitsPerSample bps, unsigned channels) {
        if (channels < MinChannels || channels > MaxChannels || (channels & 1u) != 0) {
                return 0;
        }
        return bytesPerFrameImpl(bitsCount(bps), channels);
}

size_t Smpte302M::payloadSize(BitsPerSample bps, unsigned channels, size_t sampleCount) {
        const size_t per = bytesPerAes3Frame(bps, channels);
        if (per == 0) return 0;
        return HeaderSize + per * sampleCount;
}

Error Smpte302M::pack(const void *pcm, const AudioDesc &desc, size_t sampleCount, uint32_t &blockPhase,
                      uint8_t firstChannelId, Buffer &outPesPayload, VucSource vuc) {
        if (pcm == nullptr && sampleCount > 0) return Error::InvalidArgument;
        if (desc.sampleRate() != RequiredSampleRate) return Error::InvalidArgument;
        const unsigned channels = desc.channels();
        if (channels < MinChannels || channels > MaxChannels || (channels & 1u) != 0) {
                return Error::InvalidArgument;
        }
        const int bpsCode = bitsPerSampleCode(desc.format());
        if (bpsCode < 0) return Error::NotSupported;
        const BitsPerSample bps = static_cast<BitsPerSample>(bpsCode);
        const int           ncCode = numberChannelsCode(channels);
        if (ncCode < 0) return Error::InvalidArgument;

        const size_t perFrame = bytesPerFrameImpl(bitsCount(bps), channels);
        const size_t audioBytes = perFrame * sampleCount;
        if (audioBytes > 0xFFFFu) return Error::OutOfRange;
        const size_t total = HeaderSize + audioBytes;

        outPesPayload = Buffer(total);
        if (!outPesPayload.isValid()) return Error::NoMem;
        outPesPayload.setSize(total);
        uint8_t *out = static_cast<uint8_t *>(outPesPayload.data());

        // Header (§6.7 Table 1).
        out[0] = static_cast<uint8_t>((audioBytes >> 8) & 0xFF);
        out[1] = static_cast<uint8_t>(audioBytes & 0xFF);
        out[2] = static_cast<uint8_t>((static_cast<unsigned>(ncCode) << 6) |
                                      ((firstChannelId >> 2) & 0x3F));
        out[3] = static_cast<uint8_t>(((firstChannelId & 0x3) << 6) |
                                      ((static_cast<unsigned>(bps) & 0x3) << 4));

        if (sampleCount == 0) return Error::Ok;

        BitWriter bw(out + HeaderSize);
        const AudioFormat::ID fid = desc.format().id();
        const size_t          stride = sourceSampleStride(fid);
        const unsigned        bits = bitsCount(bps);

        const uint8_t *p = static_cast<const uint8_t *>(pcm);
        for (size_t f = 0; f < sampleCount; ++f) {
                // F bit is 1 on the first AES3 frame of every 192-frame block.
                const bool fBit = (blockPhase == 0);
                for (unsigned c = 0; c < channels; ++c) {
                        const uint32_t sample = fetchPcmSample(p, fid);
                        // §5.8: bits of the data word sent LSB-first.
                        // V, U, C, F follow (F is MSB of the 302M
                        // word, but in time-order it appears last).
                        // Optional VUC source carries per-subframe
                        // V/U/C bits; default of all-zero matches
                        // synthesized linear PCM that has no AES3
                        // channel-status metadata.
                        uint8_t vucByte = 0;
                        if (vuc != nullptr) {
                                vucByte = vuc[f * channels + c];
                        }
                        bw.writeBitsLsbFirst(sample, bits);
                        bw.writeBit((vucByte & VucValidity) ? 1u : 0u);
                        bw.writeBit((vucByte & VucUser) ? 1u : 0u);
                        bw.writeBit((vucByte & VucChannelStatus) ? 1u : 0u);
                        bw.writeBit(fBit ? 1u : 0u);
                        p += stride;
                }
                blockPhase = (blockPhase + 1u) % kAes3BlockSize;
        }

        return Error::Ok;
}

Error Smpte302M::parseHeader(const BufferView &in, ParsedHeader *out) {
        if (out == nullptr) return Error::InvalidArgument;
        if (!in.isValid() || in.size() < HeaderSize) return Error::OutOfRange;
        const uint8_t *p = in.data();
        out->audioPacketSize = static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
        out->numberChannels = static_cast<uint8_t>((p[2] >> 6) & 0x3);
        out->channels = numberChannelsToChannels(out->numberChannels);
        out->channelIdentification = static_cast<uint8_t>(((p[2] & 0x3F) << 2) | ((p[3] >> 6) & 0x3));
        const uint8_t bpsCode = static_cast<uint8_t>((p[3] >> 4) & 0x3);
        if (bpsCode > 2) return Error::CorruptData;
        out->bitsPerSample = static_cast<BitsPerSample>(bpsCode);
        return Error::Ok;
}

Error Smpte302M::parse(const BufferView &in, Buffer &outPcm, AudioDesc &outDesc, size_t &outSampleCount,
                       uint8_t *outFirstChannelId) {
        return parseWithVuc(in, outPcm, outDesc, outSampleCount, /*outVuc=*/nullptr, outFirstChannelId);
}

Error Smpte302M::parseWithVuc(const BufferView &in, Buffer &outPcm, AudioDesc &outDesc, size_t &outSampleCount,
                              Buffer *outVuc, uint8_t *outFirstChannelId) {
        ParsedHeader hdr;
        Error        e = parseHeader(in, &hdr);
        if (e.isError()) return e;
        if (in.size() < HeaderSize + hdr.audioPacketSize) return Error::OutOfRange;

        const size_t perFrame = bytesPerFrameImpl(bitsCount(hdr.bitsPerSample), hdr.channels);
        if (perFrame == 0) return Error::CorruptData;
        if ((hdr.audioPacketSize % perFrame) != 0) return Error::CorruptData;
        const size_t sampleCount = hdr.audioPacketSize / perFrame;

        const AudioFormat::ID outFid = outputFormatForBps(hdr.bitsPerSample);
        AudioFormat           outFmt(outFid);
        outDesc = AudioDesc(outFmt, RequiredSampleRate, hdr.channels);
        outSampleCount = sampleCount;
        if (outFirstChannelId != nullptr) *outFirstChannelId = hdr.channelIdentification;

        const size_t outSampleBytes = outFmt.bytesPerSample();
        const size_t outBytes = outSampleBytes * hdr.channels * sampleCount;
        outPcm = Buffer(outBytes);
        if (!outPcm.isValid()) return Error::NoMem;
        outPcm.setSize(outBytes);
        std::memset(outPcm.data(), 0, outBytes);
        uint8_t *dst = static_cast<uint8_t *>(outPcm.data());

        uint8_t *vucDst = nullptr;
        if (outVuc != nullptr) {
                const size_t vucBytes = hdr.channels * sampleCount;
                *outVuc = Buffer(vucBytes ? vucBytes : 1);
                if (!outVuc->isValid()) return Error::NoMem;
                outVuc->setSize(vucBytes);
                if (vucBytes > 0) std::memset(outVuc->data(), 0, vucBytes);
                vucDst = static_cast<uint8_t *>(outVuc->data());
        }

        BitReader br(in.data() + HeaderSize, hdr.audioPacketSize);
        const unsigned bits = bitsCount(hdr.bitsPerSample);
        const unsigned widenShift = (hdr.bitsPerSample == Bits20) ? 4u : 0u;
        for (size_t f = 0; f < sampleCount; ++f) {
                for (unsigned c = 0; c < hdr.channels; ++c) {
                        uint32_t sample = 0;
                        if (!br.readBitsLsbFirst(bits, sample)) return Error::OutOfRange;
                        unsigned v = 0, u = 0, cs = 0, fb = 0;
                        if (!br.readBit(v)) return Error::OutOfRange;
                        if (!br.readBit(u)) return Error::OutOfRange;
                        if (!br.readBit(cs)) return Error::OutOfRange;
                        if (!br.readBit(fb)) return Error::OutOfRange;
                        (void)fb; // F is reconstructible from PES timing, not surfaced.

                        if (vucDst != nullptr) {
                                uint8_t b = 0;
                                if (v) b |= static_cast<uint8_t>(VucValidity);
                                if (u) b |= static_cast<uint8_t>(VucUser);
                                if (cs) b |= static_cast<uint8_t>(VucChannelStatus);
                                vucDst[f * hdr.channels + c] = b;
                        }

                        // Widen 20-bit samples into the 24-bit output slot
                        // by left-shifting (the LS 4 bits become 0, matching
                        // the §5.5 zero-fill convention used for 16-bit
                        // samples in a 20-bit AES3 subframe).
                        uint32_t widened = sample << widenShift;

                        if (outFid == AudioFormat::PCMI_S16LE) {
                                dst[0] = static_cast<uint8_t>(widened & 0xFF);
                                dst[1] = static_cast<uint8_t>((widened >> 8) & 0xFF);
                        } else {
                                dst[0] = static_cast<uint8_t>(widened & 0xFF);
                                dst[1] = static_cast<uint8_t>((widened >> 8) & 0xFF);
                                dst[2] = static_cast<uint8_t>((widened >> 16) & 0xFF);
                        }
                        dst += outSampleBytes;
                }
        }

        return Error::Ok;
}

PROMEKI_NAMESPACE_END
