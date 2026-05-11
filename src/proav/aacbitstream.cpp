/**
 * @file      aacbitstream.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * AAC AudioSpecificConfig (ISO/IEC 14496-3) and ADTS framing helpers.
 *
 * The structured fields cover the LC, HE-AAC v1, and HE-AAC v2 cases
 * RTMP cares about; bytes outside the decoded portion are preserved
 * verbatim in @ref AacDecoderConfig::rawConfig so a parse/serialize
 * round-trip can replay an unmodified blob even when we don't model
 * every bit of an extended config.
 */

#include <cstring>
#include <promeki/aacbitstream.h>
#include <promeki/audioformat.h>

PROMEKI_NAMESPACE_BEGIN

namespace {

        // Standard sample-rate table from ISO/IEC 14496-3 §1.6.3.4.  Indices
        // 13 and 14 are reserved.  Index 15 escapes to a 24-bit explicit
        // frequency that follows in the bitstream.
        constexpr uint32_t kFreqTable[16] = {
                96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                16000, 12000, 11025, 8000,  7350,  0,     0,     0
        };

        Error appendToBuffer(Buffer &out, const void *bytes, size_t len) {
                if (len == 0) return Error::Ok;
                size_t cur = out.size();
                if (cur + len > out.availSize()) {
                        size_t newAlloc = out.allocSize() * 2;
                        if (newAlloc < cur + len) newAlloc = cur + len;
                        if (newAlloc < 32) newAlloc = 32;
                        Buffer bigger(newAlloc);
                        if (cur > 0 && out.data() != nullptr) {
                                std::memcpy(bigger.data(), out.data(), cur);
                        }
                        bigger.setSize(cur);
                        out = bigger;
                }
                uint8_t *base = static_cast<uint8_t *>(out.data());
                if (base == nullptr) return Error::NotHostAccessible;
                std::memcpy(base + cur, bytes, len);
                out.setSize(cur + len);
                return Error::Ok;
        }

        // Minimal MSB-first bit reader.  Tracks whether the input ran out.
        class BitReader {
                public:
                        BitReader(const uint8_t *data, size_t bytes) : _data(data), _bits(bytes * 8) {}

                        uint32_t read(unsigned n) {
                                uint32_t v = 0;
                                for (unsigned i = 0; i < n; ++i) {
                                        if (_pos >= _bits) {
                                                _truncated = true;
                                                return v;
                                        }
                                        size_t   byte = _pos / 8;
                                        unsigned bit  = 7 - (_pos % 8);
                                        v             = (v << 1) | ((_data[byte] >> bit) & 0x1);
                                        ++_pos;
                                }
                                return v;
                        }

                        bool   truncated() const { return _truncated; }
                        size_t bitsRead() const { return _pos; }

                private:
                        const uint8_t *_data;
                        size_t         _bits;
                        size_t         _pos       = 0;
                        bool           _truncated = false;
        };

        // Minimal MSB-first bit writer that flushes to a Buffer when finalize
        // is called (or on destruction).
        class BitWriter {
                public:
                        explicit BitWriter(Buffer &out) : _out(out) {}

                        void write(uint32_t value, unsigned n) {
                                for (unsigned i = 0; i < n; ++i) {
                                        unsigned bit = (value >> (n - 1 - i)) & 0x1;
                                        _accum       = static_cast<uint8_t>((_accum << 1) | bit);
                                        ++_pendingBits;
                                        if (_pendingBits == 8) flushByte();
                                }
                        }

                        Error finalize() {
                                if (_pendingBits != 0) {
                                        // Pad with zero bits to byte boundary.
                                        _accum = static_cast<uint8_t>(_accum << (8 - _pendingBits));
                                        Error err = appendToBuffer(_out, &_accum, 1);
                                        if (err.isError()) return err;
                                        _accum       = 0;
                                        _pendingBits = 0;
                                }
                                return Error::Ok;
                        }

                private:
                        Buffer &_out;
                        uint8_t _accum       = 0;
                        unsigned _pendingBits = 0;

                        void flushByte() {
                                appendToBuffer(_out, &_accum, 1);
                                _accum       = 0;
                                _pendingBits = 0;
                        }
        };

} // anonymous namespace

// ============================================================================
// AacDecoderConfig
// ============================================================================

uint32_t AacDecoderConfig::indexToFrequency(uint8_t index) {
        if (index >= 16) return 0;
        return kFreqTable[index];
}

uint8_t AacDecoderConfig::frequencyToIndex(uint32_t hz) {
        for (uint8_t i = 0; i < 13; ++i) {
                if (kFreqTable[i] == hz) return i;
        }
        return 15; // explicit
}

Error AacDecoderConfig::parse(const BufferView &payload, AacDecoderConfig &out) {
        if (payload.count() > 1) return Error::NotSupported;
        if (payload.isEmpty() || payload.data() == nullptr || payload.size() < 2)
                return Error::OutOfRange;

        const uint8_t *p   = payload.data();
        size_t         len = payload.size();
        BitReader      br(p, len);

        uint32_t aot = br.read(5);
        if (aot == 31) {
                // Extended AOT: 6 more bits + 32.  Not modeled in v1.
                return Error::NotSupported;
        }
        out.audioObjectType = static_cast<uint8_t>(aot);

        uint32_t sfi          = br.read(4);
        uint32_t freq         = 0;
        if (sfi == 15) {
                freq = br.read(24);
        } else if (sfi <= 12) {
                freq = kFreqTable[sfi];
        } else {
                return Error::CorruptData;
        }
        out.samplingFrequencyIndex = static_cast<uint8_t>(sfi);
        out.samplingFrequency      = freq;

        out.channelConfiguration = static_cast<uint8_t>(br.read(4));

        // Detect HE-AAC v1 / v2 explicit signaling.  When AOT == 5 the
        // bitstream continues with an extension sampling-frequency index
        // and the underlying AOT.  When AOT == 29 (PS) the structure
        // recurses through an SBR layer.
        if (aot == 5 || aot == 29) {
                out.sbr = (aot == 5) || (aot == 29);
                out.ps  = (aot == 29);

                uint32_t extSfi  = br.read(4);
                uint32_t extFreq = 0;
                if (extSfi == 15) {
                        extFreq = br.read(24);
                } else if (extSfi <= 12) {
                        extFreq = kFreqTable[extSfi];
                }
                out.extensionSamplingFrequencyIndex = static_cast<uint8_t>(extSfi);
                out.extensionSamplingFrequency      = extFreq;

                // Underlying AOT (e.g. 2 = LC under SBR).  We don't decode
                // the GASpecificConfig of the underlying layer for v1 —
                // the rawConfig copy preserves it.
                uint32_t innerAot = br.read(5);
                if (innerAot == 31) return Error::NotSupported;
                out.audioObjectType = static_cast<uint8_t>(innerAot);
        }

        if (br.truncated()) return Error::OutOfRange;

        // Preserve the verbatim bytes for round-trip fidelity.
        Buffer raw(len);
        std::memcpy(raw.data(), p, len);
        raw.setSize(len);
        out.rawConfig = raw;

        return Error::Ok;
}

Error AacDecoderConfig::serialize(Buffer &out) const {
        // Fast path: when caller stamped a verbatim rawConfig we replay it
        // as-is.  This matches "parse a known-good blob, serialize back the
        // same bytes" without needing every extension bit modelled.
        if (rawConfig.size() > 0) {
                return appendToBuffer(out, rawConfig.data(), rawConfig.size());
        }

        BitWriter bw(out);
        if (sbr) {
                // HE-AAC v1 explicit signaling: AOT=5 wraps the underlying
                // LC layer.  HE-AAC v2 wraps in PS (AOT=29) on top.
                bw.write(ps ? 29 : 5, 5);
        } else {
                bw.write(audioObjectType, 5);
        }

        if (samplingFrequencyIndex == 15) {
                bw.write(15, 4);
                bw.write(samplingFrequency, 24);
        } else {
                bw.write(samplingFrequencyIndex, 4);
        }
        bw.write(channelConfiguration, 4);

        if (sbr) {
                if (extensionSamplingFrequencyIndex == 15) {
                        bw.write(15, 4);
                        bw.write(extensionSamplingFrequency, 24);
                } else {
                        bw.write(extensionSamplingFrequencyIndex, 4);
                }
                // Underlying AOT — default to LC (2) when not set.
                bw.write(audioObjectType ? audioObjectType : 2, 5);
        }

        // GASpecificConfig: 3 zero bits (frameLengthFlag,
        // dependsOnCoreCoder, extensionFlag).  AOTs that don't use a
        // GASpecificConfig (like 5/29) still pad to byte boundary, so
        // emitting the zeros is harmless.
        bw.write(0, 3);

        return bw.finalize();
}

AacDecoderConfig AacDecoderConfig::fromAudioDesc(const AudioDesc &desc) {
        AacDecoderConfig c;
        c.audioObjectType        = 2;  // AAC-LC
        uint32_t hz              = static_cast<uint32_t>(desc.sampleRate());
        c.samplingFrequencyIndex = frequencyToIndex(hz);
        c.samplingFrequency      = (c.samplingFrequencyIndex == 15) ? hz : kFreqTable[c.samplingFrequencyIndex];
        c.channelConfiguration   = static_cast<uint8_t>(desc.channels());
        return c;
}

AudioDesc AacDecoderConfig::toAudioDesc() const {
        // For HE-AAC the *output* sample rate is the SBR-doubled rate
        // (when explicit).  When no extension signaling is present we
        // report the core rate.
        uint32_t outRate = (sbr && extensionSamplingFrequency > 0) ? extensionSamplingFrequency
                                                                   : samplingFrequency;
        unsigned int channels = channelConfiguration > 0 ? channelConfiguration : 2;
        return AudioDesc(AudioFormat(AudioFormat::AAC), static_cast<float>(outRate), channels);
}

// ============================================================================
// AdtsParser
// ============================================================================

bool AdtsParser::isAdts(const BufferView &in) {
        if (in.count() > 1 || in.size() < 2 || in.data() == nullptr) return false;
        const uint8_t *p = in.data();
        return p[0] == 0xFF && (p[1] & 0xF0) == 0xF0;
}

Error AdtsParser::strip(const BufferView &in, Buffer &outRaw, AacDecoderConfig &outCfg) {
        if (in.count() > 1) return Error::NotSupported;
        if (in.isEmpty() || in.data() == nullptr) return Error::Ok;

        const uint8_t *p   = in.data();
        size_t         len = in.size();
        size_t         off = 0;

        // If the stream isn't ADTS-framed, hand it back unchanged so callers
        // can funnel either form through this helper.
        if (!isAdts(in)) {
                Buffer copy(len);
                std::memcpy(copy.data(), p, len);
                copy.setSize(len);
                outRaw = copy;
                return Error::Ok;
        }

        bool firstHeader = true;
        while (off < len) {
                if (off + 7 > len) return Error::OutOfRange;
                // Syncword + ID + layer + protection_absent.
                if (p[off] != 0xFF || (p[off + 1] & 0xF0) != 0xF0) return Error::CorruptData;
                if ((p[off + 1] & 0x06) != 0) return Error::CorruptData;  // layer must be 00
                bool protectionAbsent = (p[off + 1] & 0x01) != 0;

                // Profile is the 2-bit field == AOT - 1.
                uint8_t profile = (p[off + 2] >> 6) & 0x03;
                uint8_t sfi     = (p[off + 2] >> 2) & 0x0F;
                uint8_t cc      = static_cast<uint8_t>(((p[off + 2] & 0x01) << 2) | ((p[off + 3] >> 6) & 0x03));

                // 13-bit aac_frame_length spans bits 30..18 of the header (counting
                // from bit 0 = MSB of byte 0).
                uint32_t frameLen = (static_cast<uint32_t>(p[off + 3] & 0x03) << 11) |
                                    (static_cast<uint32_t>(p[off + 4]) << 3) |
                                    (static_cast<uint32_t>(p[off + 5] >> 5) & 0x07);
                if (frameLen < 7) return Error::CorruptData;

                size_t headerLen = protectionAbsent ? 7 : 9;
                if (frameLen < headerLen) return Error::CorruptData;
                if (off + frameLen > len) return Error::OutOfRange;

                if (firstHeader) {
                        outCfg                       = AacDecoderConfig{};
                        outCfg.audioObjectType       = static_cast<uint8_t>(profile + 1);
                        outCfg.samplingFrequencyIndex = sfi;
                        outCfg.samplingFrequency      = AacDecoderConfig::indexToFrequency(sfi);
                        outCfg.channelConfiguration   = cc;
                        // Emit a fresh rawConfig — derived, not pass-through.
                        Buffer raw;
                        Error  serErr = outCfg.serialize(raw);
                        if (serErr.isError()) return serErr;
                        outCfg.rawConfig = raw;
                        firstHeader      = false;
                }

                size_t payloadOff = off + headerLen;
                size_t payloadLen = frameLen - headerLen;

                Error err = appendToBuffer(outRaw, p + payloadOff, payloadLen);
                if (err.isError()) return err;

                off += frameLen;
        }
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
