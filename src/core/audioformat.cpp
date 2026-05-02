/**
 * @file      audioformat.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <promeki/audioformat.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/map.h>
#include <promeki/pair.h>

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
        for (size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isBigEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void floatToFloat32LE(uint8_t *outbuf, const float *in, size_t samples) {
        float *out = reinterpret_cast<float *>(outbuf);
        for (size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isBigEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void float32BEToFloat(float *out, const uint8_t *inbuf, size_t samples) {
        const float *in = reinterpret_cast<const float *>(inbuf);
        for (size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

static void floatToFloat32BE(uint8_t *outbuf, const float *in, size_t samples) {
        float *out = reinterpret_cast<float *>(outbuf);
        for (size_t i = 0; i < samples; ++i) {
                float val = *in++;
                if constexpr (System::isLittleEndian()) System::swapEndian(val);
                *out++ = val;
        }
}

template <bool SignedRange, bool BigEndian> static void s24ToFloat(float *out, const uint8_t *in, size_t samples) {
        for (size_t i = 0; i < samples; ++i) {
                int32_t val;
                if constexpr (BigEndian) {
                        val = static_cast<int32_t>(in[0]) << 16 | static_cast<int32_t>(in[1]) << 8 |
                              static_cast<int32_t>(in[2]);
                } else {
                        val = static_cast<int32_t>(in[0]) | static_cast<int32_t>(in[1]) << 8 |
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

template <bool SignedRange, bool BigEndian> static void floatToS24(uint8_t *out, const float *in, size_t samples) {
        for (size_t i = 0; i < samples; ++i) {
                int32_t val;
                if constexpr (SignedRange) {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinS24, AudioFormat::MaxS24>(*in++);
                } else {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinU24, AudioFormat::MaxU24>(*in++);
                }
                if constexpr (BigEndian) {
                        out[0] = static_cast<uint8_t>((val >> 16) & 0xFF);
                        out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                        out[2] = static_cast<uint8_t>(val & 0xFF);
                } else {
                        out[0] = static_cast<uint8_t>(val & 0xFF);
                        out[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                        out[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                }
                out += 3;
        }
}

// 24-bit data carried in a 32-bit container.  The HighBytes flag selects
// whether the 24-bit value occupies the upper or lower 3 bytes of the word;
// the unused byte is forced to zero on writeback.  Endianness applies to the
// full 32-bit word, so the byte the data occupies depends on both flags.
template <bool SignedRange, bool BigEndian, bool HighBytes>
static void s24In32ToFloat(float *out, const uint8_t *in, size_t samples) {
        for (size_t i = 0; i < samples; ++i) {
                uint32_t word;
                if constexpr (BigEndian) {
                        word = static_cast<uint32_t>(in[0]) << 24 | static_cast<uint32_t>(in[1]) << 16 |
                               static_cast<uint32_t>(in[2]) << 8 | static_cast<uint32_t>(in[3]);
                } else {
                        word = static_cast<uint32_t>(in[0]) | static_cast<uint32_t>(in[1]) << 8 |
                               static_cast<uint32_t>(in[2]) << 16 | static_cast<uint32_t>(in[3]) << 24;
                }
                uint32_t u24 = HighBytes ? (word >> 8) & 0xFFFFFFu : word & 0xFFFFFFu;
                if constexpr (SignedRange) {
                        // Sign-extend the 24-bit value into a 32-bit signed int.
                        int32_t s24 =
                                (u24 & 0x800000u) ? static_cast<int32_t>(u24 | 0xFF000000u) : static_cast<int32_t>(u24);
                        *out++ = AudioFormat::integerToFloat<int32_t, AudioFormat::MinS24, AudioFormat::MaxS24>(s24);
                } else {
                        *out++ = AudioFormat::integerToFloat<int32_t, AudioFormat::MinU24, AudioFormat::MaxU24>(
                                static_cast<int32_t>(u24));
                }
                in += 4;
        }
}

template <bool SignedRange, bool BigEndian, bool HighBytes>
static void floatToS24In32(uint8_t *out, const float *in, size_t samples) {
        for (size_t i = 0; i < samples; ++i) {
                int32_t val;
                if constexpr (SignedRange) {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinS24, AudioFormat::MaxS24>(*in++);
                } else {
                        val = AudioFormat::floatToInteger<int32_t, AudioFormat::MinU24, AudioFormat::MaxU24>(*in++);
                }
                uint32_t u24 = static_cast<uint32_t>(val) & 0xFFFFFFu;
                uint32_t word = HighBytes ? (u24 << 8) : u24;
                if constexpr (BigEndian) {
                        out[0] = static_cast<uint8_t>((word >> 24) & 0xFF);
                        out[1] = static_cast<uint8_t>((word >> 16) & 0xFF);
                        out[2] = static_cast<uint8_t>((word >> 8) & 0xFF);
                        out[3] = static_cast<uint8_t>(word & 0xFF);
                } else {
                        out[0] = static_cast<uint8_t>(word & 0xFF);
                        out[1] = static_cast<uint8_t>((word >> 8) & 0xFF);
                        out[2] = static_cast<uint8_t>((word >> 16) & 0xFF);
                        out[3] = static_cast<uint8_t>((word >> 24) & 0xFF);
                }
                out += 4;
        }
}

// ---------------------------------------------------------------------------
// Factory functions for well-known formats
// ---------------------------------------------------------------------------

static AudioFormat::Data makeInvalid() {
        AudioFormat::Data d;
        d.id = AudioFormat::Invalid;
        d.name = "Invalid";
        d.desc = "Invalid audio format";
        return d;
}

// Interleaved PCM ---------------------------------------------------------

static AudioFormat::Data makePCMI_Float32LE() {
        AudioFormat::Data d;
        d.id = AudioFormat::PCMI_Float32LE;
        d.name = "PCMI_Float32LE";
        d.desc = "PCM Interleaved 32-bit float, little-endian";
        d.bytesPerSample = sizeof(float);
        d.bitsPerSample = 32;
        d.isSigned = true;
        d.isFloat = true;
        d.isPlanar = false;
        d.isBigEndian = false;
        d.samplesToFloat = float32LEToFloat;
        d.floatToSamples = floatToFloat32LE;
        return d;
}

static AudioFormat::Data makePCMI_Float32BE() {
        AudioFormat::Data d;
        d.id = AudioFormat::PCMI_Float32BE;
        d.name = "PCMI_Float32BE";
        d.desc = "PCM Interleaved 32-bit float, big-endian";
        d.bytesPerSample = sizeof(float);
        d.bitsPerSample = 32;
        d.isSigned = true;
        d.isFloat = true;
        d.isPlanar = false;
        d.isBigEndian = true;
        d.samplesToFloat = float32BEToFloat;
        d.floatToSamples = floatToFloat32BE;
        return d;
}

static AudioFormat::Data makePCMI_S8() {
        AudioFormat::Data d;
        d.id = AudioFormat::PCMI_S8;
        d.name = "PCMI_S8";
        d.desc = "PCM Interleaved signed 8-bit";
        d.bytesPerSample = 1;
        d.bitsPerSample = 8;
        d.isSigned = true;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<int8_t, false>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<int8_t, false>;
        return d;
}

static AudioFormat::Data makePCMI_U8() {
        AudioFormat::Data d;
        d.id = AudioFormat::PCMI_U8;
        d.name = "PCMI_U8";
        d.desc = "PCM Interleaved unsigned 8-bit";
        d.bytesPerSample = 1;
        d.bitsPerSample = 8;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<uint8_t, false>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<uint8_t, false>;
        return d;
}

template <typename IntType, bool BigEndian>
static AudioFormat::Data makePCMI_Int(AudioFormat::ID id, const char *name, const char *desc) {
        AudioFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.bytesPerSample = sizeof(IntType);
        d.bitsPerSample = sizeof(IntType) * 8;
        d.isSigned = std::is_signed_v<IntType>;
        d.isBigEndian = BigEndian;
        d.samplesToFloat = AudioFormat::samplesToFloatImpl<IntType, BigEndian>;
        d.floatToSamples = AudioFormat::floatToSamplesImpl<IntType, BigEndian>;
        return d;
}

template <bool SignedRange, bool BigEndian>
static AudioFormat::Data makePCMI_24(AudioFormat::ID id, const char *name, const char *desc) {
        AudioFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.bytesPerSample = 3;
        d.bitsPerSample = 24;
        d.isSigned = SignedRange;
        d.isBigEndian = BigEndian;
        d.samplesToFloat = s24ToFloat<SignedRange, BigEndian>;
        d.floatToSamples = floatToS24<SignedRange, BigEndian>;
        return d;
}

template <bool SignedRange, bool BigEndian, bool HighBytes>
static AudioFormat::Data makePCMI_24In32(AudioFormat::ID id, const char *name, const char *desc) {
        AudioFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.bytesPerSample = 4;
        d.bitsPerSample = 24;
        d.isSigned = SignedRange;
        d.isBigEndian = BigEndian;
        d.samplesToFloat = s24In32ToFloat<SignedRange, BigEndian, HighBytes>;
        d.floatToSamples = floatToS24In32<SignedRange, BigEndian, HighBytes>;
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

static AudioFormat::Data planarFrom(const AudioFormat::Data &src, AudioFormat::ID id, const char *name,
                                    const char *desc) {
        AudioFormat::Data d = src;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.isPlanar = true;
        return d;
}

// Compressed --------------------------------------------------------------

static AudioFormat::Data makeCompressed(AudioFormat::ID id, const char *name, const char *desc, AudioCodec::ID codec,
                                        FourCC::List fourccs = {}) {
        AudioFormat::Data d;
        d.id = id;
        d.name = name;
        d.desc = desc;
        d.compressed = true;
        d.audioCodec = AudioCodec(codec);
        d.fourccList = std::move(fourccs);
        return d;
}

// ---------------------------------------------------------------------------
// Registry (construct-on-first-use)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Direct converter kernels — no-float, per-sample bit-pattern transforms used
// to skip the via-float intermediate when the source and destination formats
// are related by a trivial reversible transform (identity, endian swap, sign
// flip, 24-in-32 repack).  Caller passes a contiguous run of samples; planar
// callers walk channels themselves.
// ---------------------------------------------------------------------------

template <size_t Bytes> static void directIdentity(void *out, const void *in, size_t samples) {
        std::memcpy(out, in, samples * Bytes);
}

template <size_t Bytes> static void directEndianSwap(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                for (size_t j = 0; j < Bytes; ++j) dst[j] = src[Bytes - 1 - j];
                src += Bytes;
                dst += Bytes;
        }
}

// Sign flip: XOR the most-significant bit, leaving every other bit alone.
// @p HiByte is the index of the byte that holds the sign bit in memory
// (Bytes-1 for LE, 0 for BE).
template <size_t Bytes, size_t HiByte> static void directSignFlip(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                std::memcpy(dst, src, Bytes);
                dst[HiByte] ^= 0x80;
                src += Bytes;
                dst += Bytes;
        }
}

// Endian swap + sign flip in one pass.  Source byte at the high-byte index
// holds the sign bit; after the swap that byte lands at (Bytes-1-HiByte) in
// the destination — XOR it there.
template <size_t Bytes, size_t SrcHiByte> static void directSignFlipAndSwap(void *out, const void *in, size_t samples) {
        const uint8_t   *src = static_cast<const uint8_t *>(in);
        uint8_t         *dst = static_cast<uint8_t *>(out);
        constexpr size_t dstHi = Bytes - 1 - SrcHiByte;
        for (size_t i = 0; i < samples; ++i) {
                for (size_t j = 0; j < Bytes; ++j) dst[j] = src[Bytes - 1 - j];
                dst[dstHi] ^= 0x80;
                src += Bytes;
                dst += Bytes;
        }
}

// 24-in-32 repack helpers — sign and unsigned share the bit pattern, so
// only endian and the source/destination layout (HB32/LB32) matter.

// LE HB32 -> LE LB32: drop the zero byte at index 0, shift bytes down.
static void direct_LE_HB32_to_LB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[1];
                dst[1] = src[2];
                dst[2] = src[3];
                dst[3] = 0;
                src += 4;
                dst += 4;
        }
}

// LE LB32 -> LE HB32: insert zero byte at index 0, shift bytes up.
static void direct_LE_LB32_to_HB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = 0;
                dst[1] = src[0];
                dst[2] = src[1];
                dst[3] = src[2];
                src += 4;
                dst += 4;
        }
}

// BE HB32 -> BE LB32: insert zero byte at index 0, shift bytes up.
static void direct_BE_HB32_to_LB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = 0;
                dst[1] = src[0];
                dst[2] = src[1];
                dst[3] = src[2];
                src += 4;
                dst += 4;
        }
}

// BE LB32 -> BE HB32: drop the zero byte at index 0, shift bytes down.
static void direct_BE_LB32_to_HB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[1];
                dst[1] = src[2];
                dst[2] = src[3];
                dst[3] = 0;
                src += 4;
                dst += 4;
        }
}

// 24-packed -> 24-in-32: the three data bytes go in the data positions of
// the 32-bit container, the unused byte is zeroed.

static void direct_24LE_to_LE_HB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = 0;
                dst[1] = src[0];
                dst[2] = src[1];
                dst[3] = src[2];
                src += 3;
                dst += 4;
        }
}

static void direct_24LE_to_LE_LB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 0;
                src += 3;
                dst += 4;
        }
}

static void direct_24BE_to_BE_HB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                dst[3] = 0;
                src += 3;
                dst += 4;
        }
}

static void direct_24BE_to_BE_LB32(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = 0;
                dst[1] = src[0];
                dst[2] = src[1];
                dst[3] = src[2];
                src += 3;
                dst += 4;
        }
}

// 24-in-32 -> 24-packed: the three data bytes from the data positions of the
// 32-bit container go into the packed 3-byte form (the unused byte is dropped).

static void direct_LE_HB32_to_24LE(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[1];
                dst[1] = src[2];
                dst[2] = src[3];
                src += 4;
                dst += 3;
        }
}

static void direct_LE_LB32_to_24LE(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                src += 4;
                dst += 3;
        }
}

static void direct_BE_HB32_to_24BE(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                src += 4;
                dst += 3;
        }
}

static void direct_BE_LB32_to_24BE(void *out, const void *in, size_t samples) {
        const uint8_t *src = static_cast<const uint8_t *>(in);
        uint8_t       *dst = static_cast<uint8_t *>(out);
        for (size_t i = 0; i < samples; ++i) {
                dst[0] = src[1];
                dst[1] = src[2];
                dst[2] = src[3];
                src += 4;
                dst += 3;
        }
}

struct DirectConverterEntry {
                AudioFormat::DirectConvertFn fn = nullptr;
                bool                         bitAccurate = false;
};

using DirectKey = Pair<AudioFormat::ID, AudioFormat::ID>;

struct AudioFormatRegistry {
                Map<AudioFormat::ID, AudioFormat::Data> entries;
                Map<String, AudioFormat::ID>            nameMap;
                Map<DirectKey, DirectConverterEntry>    directConverters;

                AudioFormatRegistry() {
                        add(makeInvalid());

                        // Interleaved PCM
                        add(makePCMI_Float32LE());
                        add(makePCMI_Float32BE());
                        add(makePCMI_S8());
                        add(makePCMI_U8());
                        add(makePCMI_Int<int16_t, false>(AudioFormat::PCMI_S16LE, "PCMI_S16LE",
                                                         "PCM Interleaved signed 16-bit, little-endian"));
                        add(makePCMI_Int<uint16_t, false>(AudioFormat::PCMI_U16LE, "PCMI_U16LE",
                                                          "PCM Interleaved unsigned 16-bit, little-endian"));
                        add(makePCMI_Int<int16_t, true>(AudioFormat::PCMI_S16BE, "PCMI_S16BE",
                                                        "PCM Interleaved signed 16-bit, big-endian"));
                        add(makePCMI_Int<uint16_t, true>(AudioFormat::PCMI_U16BE, "PCMI_U16BE",
                                                         "PCM Interleaved unsigned 16-bit, big-endian"));
                        add(makePCMI_24<true, false>(AudioFormat::PCMI_S24LE, "PCMI_S24LE",
                                                     "PCM Interleaved signed 24-bit, little-endian"));
                        add(makePCMI_24<false, false>(AudioFormat::PCMI_U24LE, "PCMI_U24LE",
                                                      "PCM Interleaved unsigned 24-bit, little-endian"));
                        add(makePCMI_24<true, true>(AudioFormat::PCMI_S24BE, "PCMI_S24BE",
                                                    "PCM Interleaved signed 24-bit, big-endian"));
                        add(makePCMI_24<false, true>(AudioFormat::PCMI_U24BE, "PCMI_U24BE",
                                                     "PCM Interleaved unsigned 24-bit, big-endian"));
                        add(makePCMI_Int<int32_t, false>(AudioFormat::PCMI_S32LE, "PCMI_S32LE",
                                                         "PCM Interleaved signed 32-bit, little-endian"));
                        add(makePCMI_Int<uint32_t, false>(AudioFormat::PCMI_U32LE, "PCMI_U32LE",
                                                          "PCM Interleaved unsigned 32-bit, little-endian"));
                        add(makePCMI_Int<int32_t, true>(AudioFormat::PCMI_S32BE, "PCMI_S32BE",
                                                        "PCM Interleaved signed 32-bit, big-endian"));
                        add(makePCMI_Int<uint32_t, true>(AudioFormat::PCMI_U32BE, "PCMI_U32BE",
                                                         "PCM Interleaved unsigned 32-bit, big-endian"));

                        // 24-in-32 interleaved variants
                        add(makePCMI_24In32<true, false, true>(
                                AudioFormat::PCMI_S24LE_HB32, "PCMI_S24LE_HB32",
                                "PCM Interleaved signed 24-bit in high 3 bytes of LE 32-bit word"));
                        add(makePCMI_24In32<true, false, false>(
                                AudioFormat::PCMI_S24LE_LB32, "PCMI_S24LE_LB32",
                                "PCM Interleaved signed 24-bit in low 3 bytes of LE 32-bit word"));
                        add(makePCMI_24In32<true, true, true>(
                                AudioFormat::PCMI_S24BE_HB32, "PCMI_S24BE_HB32",
                                "PCM Interleaved signed 24-bit in high 3 bytes of BE 32-bit word"));
                        add(makePCMI_24In32<true, true, false>(
                                AudioFormat::PCMI_S24BE_LB32, "PCMI_S24BE_LB32",
                                "PCM Interleaved signed 24-bit in low 3 bytes of BE 32-bit word"));
                        add(makePCMI_24In32<false, false, true>(
                                AudioFormat::PCMI_U24LE_HB32, "PCMI_U24LE_HB32",
                                "PCM Interleaved unsigned 24-bit in high 3 bytes of LE 32-bit word"));
                        add(makePCMI_24In32<false, false, false>(
                                AudioFormat::PCMI_U24LE_LB32, "PCMI_U24LE_LB32",
                                "PCM Interleaved unsigned 24-bit in low 3 bytes of LE 32-bit word"));
                        add(makePCMI_24In32<false, true, true>(
                                AudioFormat::PCMI_U24BE_HB32, "PCMI_U24BE_HB32",
                                "PCM Interleaved unsigned 24-bit in high 3 bytes of BE 32-bit word"));
                        add(makePCMI_24In32<false, true, false>(
                                AudioFormat::PCMI_U24BE_LB32, "PCMI_U24BE_LB32",
                                "PCM Interleaved unsigned 24-bit in low 3 bytes of BE 32-bit word"));

                        // Planar PCM
                        add(planarFrom(entries[AudioFormat::PCMI_Float32LE], AudioFormat::PCMP_Float32LE,
                                       "PCMP_Float32LE", "PCM Planar 32-bit float, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_Float32BE], AudioFormat::PCMP_Float32BE,
                                       "PCMP_Float32BE", "PCM Planar 32-bit float, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S8], AudioFormat::PCMP_S8, "PCMP_S8",
                                       "PCM Planar signed 8-bit"));
                        add(planarFrom(entries[AudioFormat::PCMI_U8], AudioFormat::PCMP_U8, "PCMP_U8",
                                       "PCM Planar unsigned 8-bit"));
                        add(planarFrom(entries[AudioFormat::PCMI_S16LE], AudioFormat::PCMP_S16LE, "PCMP_S16LE",
                                       "PCM Planar signed 16-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U16LE], AudioFormat::PCMP_U16LE, "PCMP_U16LE",
                                       "PCM Planar unsigned 16-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S16BE], AudioFormat::PCMP_S16BE, "PCMP_S16BE",
                                       "PCM Planar signed 16-bit, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U16BE], AudioFormat::PCMP_U16BE, "PCMP_U16BE",
                                       "PCM Planar unsigned 16-bit, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S24LE], AudioFormat::PCMP_S24LE, "PCMP_S24LE",
                                       "PCM Planar signed 24-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24LE], AudioFormat::PCMP_U24LE, "PCMP_U24LE",
                                       "PCM Planar unsigned 24-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S24BE], AudioFormat::PCMP_S24BE, "PCMP_S24BE",
                                       "PCM Planar signed 24-bit, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24BE], AudioFormat::PCMP_U24BE, "PCMP_U24BE",
                                       "PCM Planar unsigned 24-bit, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S32LE], AudioFormat::PCMP_S32LE, "PCMP_S32LE",
                                       "PCM Planar signed 32-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U32LE], AudioFormat::PCMP_U32LE, "PCMP_U32LE",
                                       "PCM Planar unsigned 32-bit, little-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_S32BE], AudioFormat::PCMP_S32BE, "PCMP_S32BE",
                                       "PCM Planar signed 32-bit, big-endian"));
                        add(planarFrom(entries[AudioFormat::PCMI_U32BE], AudioFormat::PCMP_U32BE, "PCMP_U32BE",
                                       "PCM Planar unsigned 32-bit, big-endian"));

                        // Planar 24-in-32 variants — share the conversion helpers with
                        // their interleaved counterparts; only the isPlanar flag differs.
                        add(planarFrom(entries[AudioFormat::PCMI_S24LE_HB32], AudioFormat::PCMP_S24LE_HB32,
                                       "PCMP_S24LE_HB32",
                                       "PCM Planar signed 24-bit in high 3 bytes of LE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_S24LE_LB32], AudioFormat::PCMP_S24LE_LB32,
                                       "PCMP_S24LE_LB32", "PCM Planar signed 24-bit in low 3 bytes of LE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_S24BE_HB32], AudioFormat::PCMP_S24BE_HB32,
                                       "PCMP_S24BE_HB32",
                                       "PCM Planar signed 24-bit in high 3 bytes of BE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_S24BE_LB32], AudioFormat::PCMP_S24BE_LB32,
                                       "PCMP_S24BE_LB32", "PCM Planar signed 24-bit in low 3 bytes of BE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24LE_HB32], AudioFormat::PCMP_U24LE_HB32,
                                       "PCMP_U24LE_HB32",
                                       "PCM Planar unsigned 24-bit in high 3 bytes of LE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24LE_LB32], AudioFormat::PCMP_U24LE_LB32,
                                       "PCMP_U24LE_LB32",
                                       "PCM Planar unsigned 24-bit in low 3 bytes of LE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24BE_HB32], AudioFormat::PCMP_U24BE_HB32,
                                       "PCMP_U24BE_HB32",
                                       "PCM Planar unsigned 24-bit in high 3 bytes of BE 32-bit word"));
                        add(planarFrom(entries[AudioFormat::PCMI_U24BE_LB32], AudioFormat::PCMP_U24BE_LB32,
                                       "PCMP_U24BE_LB32",
                                       "PCM Planar unsigned 24-bit in low 3 bytes of BE 32-bit word"));

                        // Compressed
                        add(makeCompressed(AudioFormat::Opus, "Opus", "Opus compressed bitstream (RFC 6716)",
                                           AudioCodec::Opus, {"Opus", "opus"}));
                        add(makeCompressed(AudioFormat::AAC, "AAC", "Advanced Audio Coding (ISO/IEC 14496-3)",
                                           AudioCodec::AAC, {"mp4a", "aac "}));
                        add(makeCompressed(AudioFormat::FLAC, "FLAC", "Free Lossless Audio Codec", AudioCodec::FLAC,
                                           {"fLaC", "flac"}));
                        add(makeCompressed(AudioFormat::MP3, "MP3", "MPEG-1 Audio Layer III", AudioCodec::MP3,
                                           {"mp3 ", ".mp3"}));
                        add(makeCompressed(AudioFormat::AC3, "AC3", "Dolby Digital (AC-3)", AudioCodec::AC3,
                                           {"ac-3", "AC-3"}));

                        registerDirectConverters();
                }

                void add(AudioFormat::Data d) {
                        AudioFormat::ID id = d.id;
                        if (id != AudioFormat::Invalid) nameMap[d.name] = id;
                        entries[id] = std::move(d);
                }

                void addDirect(AudioFormat::ID src, AudioFormat::ID dst, AudioFormat::DirectConvertFn fn,
                               bool bitAccurate) {
                        directConverters[DirectKey(src, dst)] = DirectConverterEntry{fn, bitAccurate};
                }

                // ---- Built-in direct converter table ------------------------
                //
                // Registers the trivial reversible transforms for every PCM
                // format pair where they make sense.  The four kernels at the
                // top of this file (identity, endian swap, sign flip, swap+
                // sign-flip) cover the same-width, same-planar pairings; the
                // 24-in-32 repack helpers cover the SDI / AES3 round-trips
                // between packed 24-bit and the 32-bit container formats.
                //
                // Everything here is bit-accurate by construction — every
                // input bit is preserved (or, for upcasts that zero-extend,
                // recoverable) — so non-PCM payloads survive the trip.
                void registerDirectConverters() {
                        struct Group {
                                        size_t          bytes;
                                        AudioFormat::ID s_le, s_be, u_le, u_be;
                        };

                        // Same-width sign-flip / endian-swap groups: identity,
                        // s↔s endian, u↔u endian, s↔u sign-flip, s↔u combo.
                        const Group groups16[] = {
                                {2, AudioFormat::PCMI_S16LE, AudioFormat::PCMI_S16BE, AudioFormat::PCMI_U16LE,
                                 AudioFormat::PCMI_U16BE},
                                {2, AudioFormat::PCMP_S16LE, AudioFormat::PCMP_S16BE, AudioFormat::PCMP_U16LE,
                                 AudioFormat::PCMP_U16BE},
                        };
                        const Group groups32[] = {
                                {4, AudioFormat::PCMI_S32LE, AudioFormat::PCMI_S32BE, AudioFormat::PCMI_U32LE,
                                 AudioFormat::PCMI_U32BE},
                                {4, AudioFormat::PCMP_S32LE, AudioFormat::PCMP_S32BE, AudioFormat::PCMP_U32LE,
                                 AudioFormat::PCMP_U32BE},
                        };

                        // 8-bit only has signed/unsigned (no endian) — sign flip only.
                        for (auto pair : {std::pair{AudioFormat::PCMI_S8, AudioFormat::PCMI_U8},
                                          std::pair{AudioFormat::PCMP_S8, AudioFormat::PCMP_U8}}) {
                                addDirect(pair.first, pair.first, directIdentity<1>, true);
                                addDirect(pair.second, pair.second, directIdentity<1>, true);
                                addDirect(pair.first, pair.second, directSignFlip<1, 0>, true);
                                addDirect(pair.second, pair.first, directSignFlip<1, 0>, true);
                        }

                        for (const Group &g : groups16) {
                                // Identity.
                                addDirect(g.s_le, g.s_le, directIdentity<2>, true);
                                addDirect(g.s_be, g.s_be, directIdentity<2>, true);
                                addDirect(g.u_le, g.u_le, directIdentity<2>, true);
                                addDirect(g.u_be, g.u_be, directIdentity<2>, true);
                                // Endian swap (signed and unsigned).
                                addDirect(g.s_le, g.s_be, directEndianSwap<2>, true);
                                addDirect(g.s_be, g.s_le, directEndianSwap<2>, true);
                                addDirect(g.u_le, g.u_be, directEndianSwap<2>, true);
                                addDirect(g.u_be, g.u_le, directEndianSwap<2>, true);
                                // Sign flip (LE ↔ LE, BE ↔ BE).  HiByte index
                                // differs by endianness: 1 for LE, 0 for BE.
                                addDirect(g.s_le, g.u_le, directSignFlip<2, 1>, true);
                                addDirect(g.u_le, g.s_le, directSignFlip<2, 1>, true);
                                addDirect(g.s_be, g.u_be, directSignFlip<2, 0>, true);
                                addDirect(g.u_be, g.s_be, directSignFlip<2, 0>, true);
                                // Sign flip + endian swap (cross-endian s↔u).
                                addDirect(g.s_le, g.u_be, directSignFlipAndSwap<2, 1>, true);
                                addDirect(g.u_be, g.s_le, directSignFlipAndSwap<2, 0>, true);
                                addDirect(g.s_be, g.u_le, directSignFlipAndSwap<2, 0>, true);
                                addDirect(g.u_le, g.s_be, directSignFlipAndSwap<2, 1>, true);
                        }

                        for (const Group &g : groups32) {
                                addDirect(g.s_le, g.s_le, directIdentity<4>, true);
                                addDirect(g.s_be, g.s_be, directIdentity<4>, true);
                                addDirect(g.u_le, g.u_le, directIdentity<4>, true);
                                addDirect(g.u_be, g.u_be, directIdentity<4>, true);
                                addDirect(g.s_le, g.s_be, directEndianSwap<4>, true);
                                addDirect(g.s_be, g.s_le, directEndianSwap<4>, true);
                                addDirect(g.u_le, g.u_be, directEndianSwap<4>, true);
                                addDirect(g.u_be, g.u_le, directEndianSwap<4>, true);
                                addDirect(g.s_le, g.u_le, directSignFlip<4, 3>, true);
                                addDirect(g.u_le, g.s_le, directSignFlip<4, 3>, true);
                                addDirect(g.s_be, g.u_be, directSignFlip<4, 0>, true);
                                addDirect(g.u_be, g.s_be, directSignFlip<4, 0>, true);
                                addDirect(g.s_le, g.u_be, directSignFlipAndSwap<4, 3>, true);
                                addDirect(g.u_be, g.s_le, directSignFlipAndSwap<4, 0>, true);
                                addDirect(g.s_be, g.u_le, directSignFlipAndSwap<4, 0>, true);
                                addDirect(g.u_le, g.s_be, directSignFlipAndSwap<4, 3>, true);
                        }

                        // 24-bit packed (3-byte) groups — same shape as the 32-bit
                        // groups but with sign-bit at the topmost data byte.
                        const Group groups24[] = {
                                {3, AudioFormat::PCMI_S24LE, AudioFormat::PCMI_S24BE, AudioFormat::PCMI_U24LE,
                                 AudioFormat::PCMI_U24BE},
                                {3, AudioFormat::PCMP_S24LE, AudioFormat::PCMP_S24BE, AudioFormat::PCMP_U24LE,
                                 AudioFormat::PCMP_U24BE},
                        };
                        for (const Group &g : groups24) {
                                addDirect(g.s_le, g.s_le, directIdentity<3>, true);
                                addDirect(g.s_be, g.s_be, directIdentity<3>, true);
                                addDirect(g.u_le, g.u_le, directIdentity<3>, true);
                                addDirect(g.u_be, g.u_be, directIdentity<3>, true);
                                addDirect(g.s_le, g.s_be, directEndianSwap<3>, true);
                                addDirect(g.s_be, g.s_le, directEndianSwap<3>, true);
                                addDirect(g.u_le, g.u_be, directEndianSwap<3>, true);
                                addDirect(g.u_be, g.u_le, directEndianSwap<3>, true);
                                addDirect(g.s_le, g.u_le, directSignFlip<3, 2>, true);
                                addDirect(g.u_le, g.s_le, directSignFlip<3, 2>, true);
                                addDirect(g.s_be, g.u_be, directSignFlip<3, 0>, true);
                                addDirect(g.u_be, g.s_be, directSignFlip<3, 0>, true);
                                addDirect(g.s_le, g.u_be, directSignFlipAndSwap<3, 2>, true);
                                addDirect(g.u_be, g.s_le, directSignFlipAndSwap<3, 0>, true);
                                addDirect(g.s_be, g.u_le, directSignFlipAndSwap<3, 0>, true);
                                addDirect(g.u_le, g.s_be, directSignFlipAndSwap<3, 2>, true);
                        }

                        // 24-in-32 container groups — same kernels as the 32-bit
                        // case (the unused container byte stays zero through the
                        // bit transform because both src and dst hold it at the
                        // same memory position when the layout matches).
                        const Group groupsHB32[] = {
                                {4, AudioFormat::PCMI_S24LE_HB32, AudioFormat::PCMI_S24BE_HB32,
                                 AudioFormat::PCMI_U24LE_HB32, AudioFormat::PCMI_U24BE_HB32},
                                {4, AudioFormat::PCMP_S24LE_HB32, AudioFormat::PCMP_S24BE_HB32,
                                 AudioFormat::PCMP_U24LE_HB32, AudioFormat::PCMP_U24BE_HB32},
                        };
                        const Group groupsLB32[] = {
                                {4, AudioFormat::PCMI_S24LE_LB32, AudioFormat::PCMI_S24BE_LB32,
                                 AudioFormat::PCMI_U24LE_LB32, AudioFormat::PCMI_U24BE_LB32},
                                {4, AudioFormat::PCMP_S24LE_LB32, AudioFormat::PCMP_S24BE_LB32,
                                 AudioFormat::PCMP_U24LE_LB32, AudioFormat::PCMP_U24BE_LB32},
                        };
                        // For HB32 LE the sign bit lives in byte index 3 (the high byte
                        // of the data, which is also the high byte of the LE word).
                        // For HB32 BE the sign bit is at byte 0.
                        // For LB32 LE: sign bit at byte 2 (high byte of data; byte 3 is the zero pad).
                        // For LB32 BE: sign bit at byte 1 (byte 0 is the zero pad).
                        // Endian swap on a 24-in-32 word also flips HB32↔LB32, so
                        // the cross-endian conversions need to compose endian
                        // swap with the appropriate repack — handled separately
                        // below.
                        for (const Group &g : groupsHB32) {
                                addDirect(g.s_le, g.s_le, directIdentity<4>, true);
                                addDirect(g.s_be, g.s_be, directIdentity<4>, true);
                                addDirect(g.u_le, g.u_le, directIdentity<4>, true);
                                addDirect(g.u_be, g.u_be, directIdentity<4>, true);
                                addDirect(g.s_le, g.u_le, directSignFlip<4, 3>, true);
                                addDirect(g.u_le, g.s_le, directSignFlip<4, 3>, true);
                                addDirect(g.s_be, g.u_be, directSignFlip<4, 0>, true);
                                addDirect(g.u_be, g.s_be, directSignFlip<4, 0>, true);
                                // Cross-endian within HB32 — a plain 4-byte word
                                // swap moves the zero pad and the data bytes
                                // together, yielding the matching HB32 layout
                                // on the destination endianness.
                                addDirect(g.s_le, g.s_be, directEndianSwap<4>, true);
                                addDirect(g.s_be, g.s_le, directEndianSwap<4>, true);
                                addDirect(g.u_le, g.u_be, directEndianSwap<4>, true);
                                addDirect(g.u_be, g.u_le, directEndianSwap<4>, true);
                                // Cross-endian + sign flip.  After the byte swap,
                                // the sign byte for the HB32 layout lands at the
                                // far-end byte of the destination word (LE→BE
                                // moves byte 3 to byte 0; BE→LE moves byte 0 to
                                // byte 3).
                                addDirect(g.s_le, g.u_be, directSignFlipAndSwap<4, 3>, true);
                                addDirect(g.u_be, g.s_le, directSignFlipAndSwap<4, 0>, true);
                                addDirect(g.s_be, g.u_le, directSignFlipAndSwap<4, 0>, true);
                                addDirect(g.u_le, g.s_be, directSignFlipAndSwap<4, 3>, true);
                        }
                        for (const Group &g : groupsLB32) {
                                addDirect(g.s_le, g.s_le, directIdentity<4>, true);
                                addDirect(g.s_be, g.s_be, directIdentity<4>, true);
                                addDirect(g.u_le, g.u_le, directIdentity<4>, true);
                                addDirect(g.u_be, g.u_be, directIdentity<4>, true);
                                addDirect(g.s_le, g.u_le, directSignFlip<4, 2>, true);
                                addDirect(g.u_le, g.s_le, directSignFlip<4, 2>, true);
                                addDirect(g.s_be, g.u_be, directSignFlip<4, 1>, true);
                                addDirect(g.u_be, g.s_be, directSignFlip<4, 1>, true);
                                // Cross-endian within LB32 — same structural
                                // argument as HB32: a plain 4-byte swap maps
                                // LE_LB32 onto BE_LB32 (the zero pad migrates
                                // along with the data bytes).
                                addDirect(g.s_le, g.s_be, directEndianSwap<4>, true);
                                addDirect(g.s_be, g.s_le, directEndianSwap<4>, true);
                                addDirect(g.u_le, g.u_be, directEndianSwap<4>, true);
                                addDirect(g.u_be, g.u_le, directEndianSwap<4>, true);
                                // Cross-endian + sign flip for LB32.  Sign byte
                                // index after swap: LE_LB32 sign at byte 2;
                                // after swap that lands at byte 1 of the BE
                                // word.  And vice versa.
                                addDirect(g.s_le, g.u_be, directSignFlipAndSwap<4, 2>, true);
                                addDirect(g.u_be, g.s_le, directSignFlipAndSwap<4, 1>, true);
                                addDirect(g.s_be, g.u_le, directSignFlipAndSwap<4, 1>, true);
                                addDirect(g.u_le, g.s_be, directSignFlipAndSwap<4, 2>, true);
                        }

                        // 24-in-32 HB32 ↔ LB32 (same endian / signedness / planar).
                        // Pure byte-shift inside the 32-bit word — bit-accurate.
                        struct HBLB {
                                        AudioFormat::ID hb;
                                        AudioFormat::ID lb;
                                        bool            isLE;
                        };
                        const HBLB hblb[] = {
                                {AudioFormat::PCMI_S24LE_HB32, AudioFormat::PCMI_S24LE_LB32, true},
                                {AudioFormat::PCMI_U24LE_HB32, AudioFormat::PCMI_U24LE_LB32, true},
                                {AudioFormat::PCMI_S24BE_HB32, AudioFormat::PCMI_S24BE_LB32, false},
                                {AudioFormat::PCMI_U24BE_HB32, AudioFormat::PCMI_U24BE_LB32, false},
                                {AudioFormat::PCMP_S24LE_HB32, AudioFormat::PCMP_S24LE_LB32, true},
                                {AudioFormat::PCMP_U24LE_HB32, AudioFormat::PCMP_U24LE_LB32, true},
                                {AudioFormat::PCMP_S24BE_HB32, AudioFormat::PCMP_S24BE_LB32, false},
                                {AudioFormat::PCMP_U24BE_HB32, AudioFormat::PCMP_U24BE_LB32, false},
                        };
                        for (const HBLB &h : hblb) {
                                if (h.isLE) {
                                        addDirect(h.hb, h.lb, direct_LE_HB32_to_LB32, true);
                                        addDirect(h.lb, h.hb, direct_LE_LB32_to_HB32, true);
                                } else {
                                        addDirect(h.hb, h.lb, direct_BE_HB32_to_LB32, true);
                                        addDirect(h.lb, h.hb, direct_BE_LB32_to_HB32, true);
                                }
                        }

                        // 24-packed ↔ 24-in-32 (same endian / signedness / planar).
                        struct PackPair {
                                        AudioFormat::ID packed;
                                        AudioFormat::ID hb32;
                                        AudioFormat::ID lb32;
                                        bool            isLE;
                        };
                        const PackPair packPairs[] = {
                                {AudioFormat::PCMI_S24LE, AudioFormat::PCMI_S24LE_HB32, AudioFormat::PCMI_S24LE_LB32,
                                 true},
                                {AudioFormat::PCMI_U24LE, AudioFormat::PCMI_U24LE_HB32, AudioFormat::PCMI_U24LE_LB32,
                                 true},
                                {AudioFormat::PCMI_S24BE, AudioFormat::PCMI_S24BE_HB32, AudioFormat::PCMI_S24BE_LB32,
                                 false},
                                {AudioFormat::PCMI_U24BE, AudioFormat::PCMI_U24BE_HB32, AudioFormat::PCMI_U24BE_LB32,
                                 false},
                                {AudioFormat::PCMP_S24LE, AudioFormat::PCMP_S24LE_HB32, AudioFormat::PCMP_S24LE_LB32,
                                 true},
                                {AudioFormat::PCMP_U24LE, AudioFormat::PCMP_U24LE_HB32, AudioFormat::PCMP_U24LE_LB32,
                                 true},
                                {AudioFormat::PCMP_S24BE, AudioFormat::PCMP_S24BE_HB32, AudioFormat::PCMP_S24BE_LB32,
                                 false},
                                {AudioFormat::PCMP_U24BE, AudioFormat::PCMP_U24BE_HB32, AudioFormat::PCMP_U24BE_LB32,
                                 false},
                        };
                        for (const PackPair &p : packPairs) {
                                if (p.isLE) {
                                        addDirect(p.packed, p.hb32, direct_24LE_to_LE_HB32, true);
                                        addDirect(p.packed, p.lb32, direct_24LE_to_LE_LB32, true);
                                        addDirect(p.hb32, p.packed, direct_LE_HB32_to_24LE, true);
                                        addDirect(p.lb32, p.packed, direct_LE_LB32_to_24LE, true);
                                } else {
                                        addDirect(p.packed, p.hb32, direct_24BE_to_BE_HB32, true);
                                        addDirect(p.packed, p.lb32, direct_24BE_to_BE_LB32, true);
                                        addDirect(p.hb32, p.packed, direct_BE_HB32_to_24BE, true);
                                        addDirect(p.lb32, p.packed, direct_BE_LB32_to_24BE, true);
                                }
                        }

                        // Float identity — registered as bit-accurate too,
                        // since storing identical float bits is exactly that.
                        addDirect(AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_Float32LE, directIdentity<4>, true);
                        addDirect(AudioFormat::PCMI_Float32BE, AudioFormat::PCMI_Float32BE, directIdentity<4>, true);
                        addDirect(AudioFormat::PCMP_Float32LE, AudioFormat::PCMP_Float32LE, directIdentity<4>, true);
                        addDirect(AudioFormat::PCMP_Float32BE, AudioFormat::PCMP_Float32BE, directIdentity<4>, true);
                        addDirect(AudioFormat::PCMI_Float32LE, AudioFormat::PCMI_Float32BE, directEndianSwap<4>, true);
                        addDirect(AudioFormat::PCMI_Float32BE, AudioFormat::PCMI_Float32LE, directEndianSwap<4>, true);
                        addDirect(AudioFormat::PCMP_Float32LE, AudioFormat::PCMP_Float32BE, directEndianSwap<4>, true);
                        addDirect(AudioFormat::PCMP_Float32BE, AudioFormat::PCMP_Float32LE, directEndianSwap<4>, true);
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
        auto  it = reg.entries.find(id);
        if (it != reg.entries.end()) return &it->second;
        return &reg.entries[Invalid];
}

void AudioFormat::registerData(Data &&data) {
        auto &reg = registry();
        if (data.id != Invalid) reg.nameMap[data.name] = data.id;
        reg.entries[data.id] = std::move(data);
}

Result<AudioFormat> AudioFormat::lookup(const String &name) {
        auto &reg = registry();
        auto  it = reg.nameMap.find(name);
        if (it == reg.nameMap.end()) return makeError<AudioFormat>(Error::IdNotFound);
        return makeResult(AudioFormat(it->second));
}

Result<AudioFormat> AudioFormat::fromString(const String &name) {
        // AudioFormat has no backend-suffix shape — fromString is a
        // straight alias for lookup with the same Result error contract.
        return lookup(name);
}

AudioFormat AudioFormat::lookupByFourCC(const FourCC &fcc) {
        auto &reg = registry();
        for (const auto &[id, data] : reg.entries) {
                if (id == Invalid) continue;
                for (const FourCC &entry : data.fourccList) {
                        if (entry == fcc) return AudioFormat(id);
                }
        }
        return AudioFormat(Invalid);
}

AudioFormat::IDList AudioFormat::registeredIDs() {
        auto  &reg = registry();
        IDList ret;
        for (const auto &[id, data] : reg.entries) {
                if (id != Invalid) ret.pushToBack(id);
        }
        return ret;
}

// ---------------------------------------------------------------------------
// Direct converter registry / dispatch
// ---------------------------------------------------------------------------

AudioFormat::DirectConvertFn AudioFormat::directConverter(ID src, ID dst) {
        auto &reg = registry();
        auto  it = reg.directConverters.find(DirectKey(src, dst));
        if (it == reg.directConverters.end()) return nullptr;
        return it->second.fn;
}

bool AudioFormat::isBitAccurate(ID src, ID dst) {
        auto &reg = registry();
        auto  it = reg.directConverters.find(DirectKey(src, dst));
        if (it == reg.directConverters.end()) return false;
        return it->second.bitAccurate;
}

void AudioFormat::registerDirectConverter(ID src, ID dst, DirectConvertFn fn, bool bitAccurate) {
        auto &reg = registry();
        reg.directConverters[DirectKey(src, dst)] = DirectConverterEntry{fn, bitAccurate};
}

Error AudioFormat::convertTo(const AudioFormat &dst, void *out, const void *in, size_t samples, float *scratch) const {
        if (!isValid() || !dst.isValid()) return Error::InvalidArgument;
        if (samples == 0) return Error::Ok;

        // Direct path — registered (src, dst) function pointer.
        if (DirectConvertFn fn = directConverter(id(), dst.id()); fn != nullptr) {
                fn(out, in, samples);
                return Error::Ok;
        }

        // Via-float fallback.  Both sides must be PCM — compressed
        // formats have no via-float trip and need a codec.
        if (isCompressed() || dst.isCompressed()) return Error::NotSupported;
        if (scratch == nullptr) return Error::InvalidArgument;

        samplesToFloat(scratch, static_cast<const uint8_t *>(in), samples);
        dst.floatToSamples(static_cast<uint8_t *>(out), scratch, samples);
        return Error::Ok;
}

Error AudioFormat::convertTo(const AudioFormat &dst, void *out, const void *in, size_t samplesPerChannel,
                              size_t channels, float *scratch) const {
        if (!isValid() || !dst.isValid()) return Error::InvalidArgument;
        if (samplesPerChannel == 0 || channels == 0) return Error::Ok;

        const size_t totalFloats = samplesPerChannel * channels;
        const bool   needsTranspose = (channels > 1) && (isPlanar() != dst.isPlanar());

        // Direct converters are layout-agnostic byte-level translations
        // (e.g. endian swap, identity copy) — they do not transpose.
        // We can only use them when the planar/interleaved layout
        // matches across src and dst.
        if (!needsTranspose) {
                if (DirectConvertFn fn = directConverter(id(), dst.id()); fn != nullptr) {
                        fn(out, in, totalFloats);
                        return Error::Ok;
                }
        }

        if (isCompressed() || dst.isCompressed()) return Error::NotSupported;
        if (scratch == nullptr) return Error::InvalidArgument;

        // Same-layout via-float trip — no transpose needed.
        if (!needsTranspose) {
                samplesToFloat(scratch, static_cast<const uint8_t *>(in), totalFloats);
                dst.floatToSamples(static_cast<uint8_t *>(out), scratch, totalFloats);
                return Error::Ok;
        }

        // Cross-layout: convert src bytes to a contiguous float run in
        // src's layout, transpose into dst's layout in a second float
        // buffer, then convert that to dst bytes.  The transpose has
        // to live in a separate buffer because each output element
        // pulls from a non-adjacent input slot.
        samplesToFloat(scratch, static_cast<const uint8_t *>(in), totalFloats);
        Buffer transposed(totalFloats * sizeof(float));
        if (!transposed.isValid()) return Error::NoMem;
        float *t = static_cast<float *>(transposed.data());
        if (isPlanar()) {
                // Planar input  [ch0[0..N), ch1[0..N), ...]
                //   →
                // Interleaved   [s0c0,s0c1,...,sNc0,sNc1,...]
                for (size_t c = 0; c < channels; ++c) {
                        const float *cIn = scratch + c * samplesPerChannel;
                        for (size_t s = 0; s < samplesPerChannel; ++s) {
                                t[s * channels + c] = cIn[s];
                        }
                }
        } else {
                // Interleaved input → planar output.
                for (size_t s = 0; s < samplesPerChannel; ++s) {
                        const float *sIn = scratch + s * channels;
                        for (size_t c = 0; c < channels; ++c) {
                                t[c * samplesPerChannel + s] = sIn[c];
                        }
                }
        }
        dst.floatToSamples(static_cast<uint8_t *>(out), t, totalFloats);
        return Error::Ok;
}

PROMEKI_NAMESPACE_END
