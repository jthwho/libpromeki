/**
 * @file      audioformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/audioformat.h>
#include <promeki/atomic.h>
#include <promeki/map.h>

PROMEKI_NAMESPACE_BEGIN

// ---------------------------------------------------------------------------
// Atomic ID counter for user-registered formats
// ---------------------------------------------------------------------------

static Atomic<int> _nextType{AudioFormat::UserDefined};

AudioFormat::ID AudioFormat::registerType() {
        return static_cast<ID>(_nextType.fetchAndAdd(1));
}

// ---------------------------------------------------------------------------
// Standalone sample conversion helpers used by the registered Data
// records.  Float32 has its own helpers because the integer templates
// assume an integral sample type.  24-bit helpers are also specialised
// because a packed 3-byte sample doesn't map onto any std integer type.
// ---------------------------------------------------------------------------

static void float32LEToFloat(float *out, const uint8_t *inbuf, size_t samples) {
        const float *in = reinterpret_cast<const float *>(inbuf);
        for(size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isBigEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void floatToFloat32LE(uint8_t *outbuf, const float *in, size_t samples) {
        float *out = reinterpret_cast<float *>(outbuf);
        for(size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isBigEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void float32BEToFloat(float *out, const uint8_t *inbuf, size_t samples) {
        const float *in = reinterpret_cast<const float *>(inbuf);
        for(size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void floatToFloat32BE(uint8_t *outbuf, const float *in, size_t samples) {
        float *out = reinterpret_cast<float *>(outbuf);
        for(size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

template <bool SignedRange, bool BigEndian>
static void s24ToFloat(float *out, const uint8_t *in, size_t samples) {
        for(size_t i = 0; i < samples; ++i) {
                int32_t val;
                if constexpr (BigEndian) {
                        val = static_cast<int32_t>(in[0]) << 16 |
                              static_cast<int32_t>(in[1]) << 8  |
                              static_cast<int32_t>(in[2]);
                } else {
                        val = static_cast<int32_t>(in[0])       |
                              static_cast<int32_t>(in[1]) << 8  |
                              static_cast<int32_t>(in[2]) << 16;
                }
                if constexpr (SignedRange) {
                        *out++ = AudioFormat::integerToFloat<int32_t, AudioFormat::MinS24, AudioFormat::MaxS24>(val);
                } else {
                        *out++ = AudioFormat::integerToFloat<int32_t, AudioFormat::MinU24, AudioFormat::MaxU24>(val);
                }
                in += 3;
        }
}

template <bool SignedRange, bool BigEndian>
static void floatToS24(uint8_t *out, const float *in, size_t samples) {
        for(size_t i = 0; i < samples; ++i) {
                int32_t val;
                if constexpr (SignedRange) {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinS24, AudioFormat::MaxS24>(*in++);
                } else {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinU24, AudioFormat::MaxU24>(*in++);
                }
                if constexpr (BigEndian) {
                        out[0] = static_cast<uint8_t>((val >> 16) & 0xFF);
                        out[1] = static_cast<uint8_t>((val >>  8) & 0xFF);
                        out[2] = static_cast<uint8_t>( val        & 0xFF);
                } else {
                        out[0] = static_cast<uint8_t>( val        & 0xFF);
                        out[1] = static_cast<uint8_t>((val >>  8) & 0xFF);
                        out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                }
                out += 3;
        }
}

// ---------------------------------------------------------------------------
// Factory functions for well-known formats
// ---------------------------------------------------------------------------

static AudioFormat::Data makeInvalid() {
        AudioFormat::Data d;
        d.id   = AudioFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid audio format";
        return d;
}

// Interleaved PCM ---------------------------------------------------------

static AudioFormat::Data makePCMI_Float32LE() {
        AudioFormat::Data d;
        d.id             = AudioFormat::PCMI_Float32LE;
        d.name           = "PCMI_Float32LE";
        d.desc           = "PCM Interleaved 32-bit float, little-endian";
        d.bytesPerSample = sizeof(float);
        d.bitsPerSample  = 32;
        d.isSigned       = true;
        d.isFloat        = true;
        d.isPlanar       = false;
        d.isBigEndian    = false;
        d.samplesToFloat = float32LEToFloat;
        d.floatToSamples = floatToFloat32LE;
        return d;
}

static AudioFormat::Data makePCMI_Float32BE() {
        AudioFormat::Data d;
        d.id             = AudioFormat::PCMI_Float32BE;
        d.name           = "PCMI_Float32BE";
        d.desc           = "PCM Interleaved 32-bit float, big-endian";
        d.bytesPerSample = sizeof(float);
        d.bitsPerSample  = 32;
        d.isSigned       = true;
        d.isFloat        = true;
        d.isPlanar       = false;
        d.isBigEndian    = true;
        d.samplesToFloat = float32BEToFloat;
        d.floatToSamples = floatToFloat32BE;
        return d;
}

static AudioFormat::Data makePCMI_S8() {
        AudioFormat::Data d;
        d.id             = AudioFormat::PCMI_S8;
        d.name           = "PCMI_S8";
        d.desc           = "PCM Interleaved signed 8-bit";
        d.bytesPerSample = 1;
        d.bitsPerSample  = 8;
        d.isSigned       = true;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<int8_t, false>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<int8_t, false>;
        return d;
}

static AudioFormat::Data makePCMI_U8() {
        AudioFormat::Data d;
        d.id             = AudioFormat::PCMI_U8;
        d.name           = "PCMI_U8";
        d.desc           = "PCM Interleaved unsigned 8-bit";
        d.bytesPerSample = 1;
        d.bitsPerSample  = 8;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<uint8_t, false>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<uint8_t, false>;
        return d;
}

template <typename IntType, bool BigEndian>
static AudioFormat::Data makePCMI_Int(AudioFormat::ID id, const char *name, const char *desc) {
        AudioFormat::Data d;
        d.id             = id;
        d.name           = name;
        d.desc           = desc;
        d.bytesPerSample = sizeof(IntType);
        d.bitsPerSample  = sizeof(IntType) * 8;
        d.isSigned       = std::is_signed_v<IntType>;
        d.isBigEndian    = BigEndian;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<IntType, BigEndian>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<IntType, BigEndian>;
        return d;
}

template <bool SignedRange, bool BigEndian>
static AudioFormat::Data makePCMI_24(AudioFormat::ID id, const char *name, const char *desc) {
        AudioFormat::Data d;
        d.id             = id;
        d.name           = name;
        d.desc           = desc;
        d.bytesPerSample = 3;
        d.bitsPerSample  = 24;
        d.isSigned       = SignedRange;
        d.isBigEndian    = BigEndian;
        d.samplesToFloat = s24ToFloat<SignedRange, BigEndian>;
        d.floatToSamples = floatToS24<SignedRange, BigEndian>;
        return d;
}

// Planar PCM --------------------------------------------------------------
//
// Planar formats share the same sample-level properties and conversion
// helpers as their interleaved counterparts — the only difference is
// the @c isPlanar flag, which higher-level code (AudioDesc,
// Audio) uses to compute per-channel offsets.  The conversion helpers
// work on a single contiguous run of samples, so callers that convert
// planar audio walk the channels themselves.

static AudioFormat::Data planarFrom(const AudioFormat::Data &src,
                                    AudioFormat::ID id,
                                    const char *name,
                                    const char *desc) {
        AudioFormat::Data d = src;
        d.id       = id;
        d.name     = name;
        d.desc     = desc;
        d.isPlanar = true;
        return d;
}

// Compressed --------------------------------------------------------------

static AudioFormat::Data makeCompressed(AudioFormat::ID id, const char *name,
                                        const char *desc, AudioCodec::ID codec,
                                        FourCCList fourccs = {}) {
        AudioFormat::Data d;
        d.id         = id;
        d.name       = name;
        d.desc       = desc;
        d.compressed = true;
        d.audioCodec = AudioCodec(codec);
        d.fourccList = std::move(fourccs);
        return d;
}

// ---------------------------------------------------------------------------
// Registry (construct-on-first-use)
// ---------------------------------------------------------------------------

struct AudioFormatRegistry {
        Map<AudioFormat::ID, AudioFormat::Data> entries;
        Map<String, AudioFormat::ID>            nameMap;

        AudioFormatRegistry() {
                add(makeInvalid());

                // Interleaved PCM
                add(makePCMI_Float32LE());
                add(makePCMI_Float32BE());
                add(makePCMI_S8());
                add(makePCMI_U8());
                add(makePCMI_Int<int16_t, false>(AudioFormat::PCMI_S16LE,
                        "PCMI_S16LE", "PCM Interleaved signed 16-bit, little-endian"));
                add(makePCMI_Int<uint16_t, false>(AudioFormat::PCMI_U16LE,
                        "PCMI_U16LE", "PCM Interleaved unsigned 16-bit, little-endian"));
                add(makePCMI_Int<int16_t, true>(AudioFormat::PCMI_S16BE,
                        "PCMI_S16BE", "PCM Interleaved signed 16-bit, big-endian"));
                add(makePCMI_Int<uint16_t, true>(AudioFormat::PCMI_U16BE,
                        "PCMI_U16BE", "PCM Interleaved unsigned 16-bit, big-endian"));
                add(makePCMI_24<true,  false>(AudioFormat::PCMI_S24LE,
                        "PCMI_S24LE", "PCM Interleaved signed 24-bit, little-endian"));
                add(makePCMI_24<false, false>(AudioFormat::PCMI_U24LE,
                        "PCMI_U24LE", "PCM Interleaved unsigned 24-bit, little-endian"));
                add(makePCMI_24<true,  true >(AudioFormat::PCMI_S24BE,
                        "PCMI_S24BE", "PCM Interleaved signed 24-bit, big-endian"));
                add(makePCMI_24<false, true >(AudioFormat::PCMI_U24BE,
                        "PCMI_U24BE", "PCM Interleaved unsigned 24-bit, big-endian"));
                add(makePCMI_Int<int32_t, false>(AudioFormat::PCMI_S32LE,
                        "PCMI_S32LE", "PCM Interleaved signed 32-bit, little-endian"));
                add(makePCMI_Int<uint32_t, false>(AudioFormat::PCMI_U32LE,
                        "PCMI_U32LE", "PCM Interleaved unsigned 32-bit, little-endian"));
                add(makePCMI_Int<int32_t, true>(AudioFormat::PCMI_S32BE,
                        "PCMI_S32BE", "PCM Interleaved signed 32-bit, big-endian"));
                add(makePCMI_Int<uint32_t, true>(AudioFormat::PCMI_U32BE,
                        "PCMI_U32BE", "PCM Interleaved unsigned 32-bit, big-endian"));

                // Planar PCM
                add(planarFrom(entries[AudioFormat::PCMI_Float32LE],
                        AudioFormat::PCMP_Float32LE,
                        "PCMP_Float32LE", "PCM Planar 32-bit float, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_Float32BE],
                        AudioFormat::PCMP_Float32BE,
                        "PCMP_Float32BE", "PCM Planar 32-bit float, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S8],
                        AudioFormat::PCMP_S8,     "PCMP_S8",  "PCM Planar signed 8-bit"));
                add(planarFrom(entries[AudioFormat::PCMI_U8],
                        AudioFormat::PCMP_U8,     "PCMP_U8",  "PCM Planar unsigned 8-bit"));
                add(planarFrom(entries[AudioFormat::PCMI_S16LE],
                        AudioFormat::PCMP_S16LE,  "PCMP_S16LE",  "PCM Planar signed 16-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U16LE],
                        AudioFormat::PCMP_U16LE,  "PCMP_U16LE",  "PCM Planar unsigned 16-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S16BE],
                        AudioFormat::PCMP_S16BE,  "PCMP_S16BE",  "PCM Planar signed 16-bit, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U16BE],
                        AudioFormat::PCMP_U16BE,  "PCMP_U16BE",  "PCM Planar unsigned 16-bit, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S24LE],
                        AudioFormat::PCMP_S24LE,  "PCMP_S24LE",  "PCM Planar signed 24-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U24LE],
                        AudioFormat::PCMP_U24LE,  "PCMP_U24LE",  "PCM Planar unsigned 24-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S24BE],
                        AudioFormat::PCMP_S24BE,  "PCMP_S24BE",  "PCM Planar signed 24-bit, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U24BE],
                        AudioFormat::PCMP_U24BE,  "PCMP_U24BE",  "PCM Planar unsigned 24-bit, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S32LE],
                        AudioFormat::PCMP_S32LE,  "PCMP_S32LE",  "PCM Planar signed 32-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U32LE],
                        AudioFormat::PCMP_U32LE,  "PCMP_U32LE",  "PCM Planar unsigned 32-bit, little-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_S32BE],
                        AudioFormat::PCMP_S32BE,  "PCMP_S32BE",  "PCM Planar signed 32-bit, big-endian"));
                add(planarFrom(entries[AudioFormat::PCMI_U32BE],
                        AudioFormat::PCMP_U32BE,  "PCMP_U32BE",  "PCM Planar unsigned 32-bit, big-endian"));

                // Compressed
                add(makeCompressed(AudioFormat::Opus, "Opus",
                        "Opus compressed bitstream (RFC 6716)",
                        AudioCodec::Opus, { "Opus", "opus" }));
                add(makeCompressed(AudioFormat::AAC, "AAC",
                        "Advanced Audio Coding (ISO/IEC 14496-3)",
                        AudioCodec::AAC, { "mp4a", "aac " }));
                add(makeCompressed(AudioFormat::FLAC, "FLAC",
                        "Free Lossless Audio Codec",
                        AudioCodec::FLAC, { "fLaC", "flac" }));
                add(makeCompressed(AudioFormat::MP3, "MP3",
                        "MPEG-1 Audio Layer III",
                        AudioCodec::MP3, { "mp3 ", ".mp3" }));
                add(makeCompressed(AudioFormat::AC3, "AC3",
                        "Dolby Digital (AC-3)",
                        AudioCodec::AC3, { "ac-3", "AC-3" }));
        }

        void add(AudioFormat::Data d) {
                AudioFormat::ID id = d.id;
                if(id != AudioFormat::Invalid) nameMap[d.name] = id;
                entries[id] = std::move(d);
        }
};

static AudioFormatRegistry &registry() {
        static AudioFormatRegistry reg;
        return reg;
}

// ---------------------------------------------------------------------------
// Static methods
// ---------------------------------------------------------------------------

const AudioFormat::Data *AudioFormat::lookupData(ID id) {
        auto &reg = registry();
        auto it = reg.entries.find(id);
        if(it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void AudioFormat::registerData(Data &&data) {
        auto &reg = registry();
        if(data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

Result<AudioFormat> AudioFormat::lookup(const String &name) {
        auto &reg = registry();
        auto it = reg.nameMap.find(name);
        if(it == reg.nameMap.end()) return makeError<AudioFormat>(Error::IdNotFound);
        return makeResult(AudioFormat(it->second));
}

Result<AudioFormat> AudioFormat::fromString(const String &name) {
        // AudioFormat has no backend-suffix shape — fromString is a
        // straight alias for lookup with the same Result error contract.
        return lookup(name);
}

AudioFormat AudioFormat::lookupByFourCC(const FourCC &fcc) {
        auto &reg = registry();
        for(const auto &[id, data] : reg.entries) {
                if(id == Invalid) continue;
                for(const FourCC &entry : data.fourccList) {
                        if(entry == fcc) return AudioFormat(id);
                }
        }
        return AudioFormat(Invalid);
}

AudioFormat::IDList AudioFormat::registeredIDs() {
        auto &reg = registry();
        IDList ret;
        for(const auto &[id, data] : reg.entries) {
                if(id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
