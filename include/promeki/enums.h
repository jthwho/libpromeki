/**
 * @file      enums.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @defgroup wellknownenums Well-known Enums
 * @ingroup util
 *
 * Library-provided @ref Enum types that are commonly used as config values
 * throughout the ProAV pipeline.  Each class inherits from
 * @ref TypedEnum "TypedEnum<Self>" so its values carry a compile-time
 * type identity:
 *
 * - A @c Type member holds the registered @ref Enum::Type handle.
 * - A set of @c static @c const wrapper-class constants, one per
 *   registered value, for ergonomic use at call sites:
 *   @code
 *   cfg.set(MediaConfig::VideoPattern, VideoPattern::ColorBars);
 *   @endcode
 * - Functions that want compile-time type checking take the concrete
 *   class (e.g. @c "const VideoPattern &") instead of a bare
 *   @c "const Enum &"; runtime compatibility with @ref Variant and any
 *   @ref Enum-based API is preserved via public inheritance.
 *
 * The integer values are stable and match the corresponding C++ `enum`
 * inside the subsystem that originally defined them (e.g.
 * VideoTestPattern::Pattern), so code that still holds the C++ `enum`
 * internally can convert in either direction via a plain @c static_cast
 * on @c Enum::value().
 *
 * The registered string names use the same CamelCase spelling as the C++
 * constants on the wrapper struct (e.g. @c "ColorBars", @c "BottomCenter",
 * @c "LTC") rather than the legacy all-lowercase config strings.  Call
 * sites that previously wrote @c "colorbars" / @c "bottomcenter" /
 * @c "tone" / @c "422" need to be updated to the CamelCase form or, better,
 * to use the typed constants directly.
 * @{
 */

/**
 * @brief Well-known Enum type for video test pattern generator modes.
 *
 * Mirrors @c VideoTestPattern::Pattern in value and order.  Used as the
 * value type for the @c MediaIOTask_TPG @c ConfigVideoPattern config key.
 *
 * @par Example
 * @code
 * cfg.set(MediaConfig::VideoPattern, VideoPattern::ZonePlate);
 * Enum e = cfg.getAs<Enum>(MediaConfig::VideoPattern,
 *                          VideoPattern::ColorBars);
 * VideoTestPattern::Pattern pat =
 *         static_cast<VideoTestPattern::Pattern>(e.value());
 * @endcode
 */
class VideoPattern : public TypedEnum<VideoPattern> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoPattern", 0,
                                { "ColorBars",    0  },
                                { "ColorBars75",  1  },
                                { "Ramp",         2  },
                                { "Grid",         3  },
                                { "Crosshatch",   4  },
                                { "Checkerboard", 5  },
                                { "SolidColor",   6  },
                                { "White",        7  },
                                { "Black",        8  },
                                { "Noise",        9  },
                                { "ZonePlate",    10 },
                                { "AvSync",       11 });  // default: ColorBars

                using TypedEnum<VideoPattern>::TypedEnum;

                static const VideoPattern ColorBars;
                static const VideoPattern ColorBars75;
                static const VideoPattern Ramp;
                static const VideoPattern Grid;
                static const VideoPattern Crosshatch;
                static const VideoPattern Checkerboard;
                static const VideoPattern SolidColor;
                static const VideoPattern White;
                static const VideoPattern Black;
                static const VideoPattern Noise;
                static const VideoPattern ZonePlate;
                static const VideoPattern AvSync;
};

inline const VideoPattern VideoPattern::ColorBars    { 0  };
inline const VideoPattern VideoPattern::ColorBars75  { 1  };
inline const VideoPattern VideoPattern::Ramp         { 2  };
inline const VideoPattern VideoPattern::Grid         { 3  };
inline const VideoPattern VideoPattern::Crosshatch   { 4  };
inline const VideoPattern VideoPattern::Checkerboard { 5  };
inline const VideoPattern VideoPattern::SolidColor   { 6  };
inline const VideoPattern VideoPattern::White        { 7  };
inline const VideoPattern VideoPattern::Black        { 8  };
inline const VideoPattern VideoPattern::Noise        { 9  };
inline const VideoPattern VideoPattern::ZonePlate    { 10 };
inline const VideoPattern VideoPattern::AvSync       { 11 };

/**
 * @brief Well-known Enum type for on-screen burn-in position presets.
 *
 * Mirrors @c VideoTestPattern::BurnPosition in value and order.  Used as
 * the value type for the @c MediaIOTask_TPG @c ConfigVideoBurnPosition
 * config key.
 */
class BurnPosition : public TypedEnum<BurnPosition> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("BurnPosition", 4,
                                { "TopLeft",      0 },
                                { "TopCenter",    1 },
                                { "TopRight",     2 },
                                { "BottomLeft",   3 },
                                { "BottomCenter", 4 },
                                { "BottomRight",  5 },
                                { "Center",       6 });  // default: BottomCenter

                using TypedEnum<BurnPosition>::TypedEnum;

                static const BurnPosition TopLeft;
                static const BurnPosition TopCenter;
                static const BurnPosition TopRight;
                static const BurnPosition BottomLeft;
                static const BurnPosition BottomCenter;
                static const BurnPosition BottomRight;
                static const BurnPosition Center;
};

inline const BurnPosition BurnPosition::TopLeft      { 0 };
inline const BurnPosition BurnPosition::TopCenter    { 1 };
inline const BurnPosition BurnPosition::TopRight     { 2 };
inline const BurnPosition BurnPosition::BottomLeft   { 3 };
inline const BurnPosition BurnPosition::BottomCenter { 4 };
inline const BurnPosition BurnPosition::BottomRight  { 5 };
inline const BurnPosition BurnPosition::Center       { 6 };

/**
 * @brief Well-known Enum type for audio test pattern generator modes.
 *
 * Used as the element type for the @ref MediaConfig::AudioChannelModes
 * EnumList — the TPG consumes a list of @ref AudioPattern values, one
 * per audio channel, so each channel can carry a different diagnostic
 * signal.  If the configured list is shorter than the stream's channel
 * count, the remaining channels are silenced.
 *
 * - @c Tone       — continuous sine at the configured tone frequency.
 * - @c Silence    — constant zero.
 * - @c LTC        — Linear Timecode audio encoding this channel.
 * - @c AvSync     — one-shot tone burst on @c tc.frame() == 0, silent
 *                   otherwise.  Pairs with the picture AvSync marker
 *                   so inspectors can measure audio-to-video drift.
 * - @c SrcProbe   — 997 Hz reference tone.  997 Hz is prime relative
 *                   to common sample rates so any downstream sample
 *                   rate conversion shifts the observed frequency by
 *                   a detectable amount.
 * - @c ChannelId  — channel-unique sine whose frequency encodes the
 *                   channel index, so a receiver that sees the audio
 *                   out of order can identify which channel is which
 *                   without metadata.  See
 *                   @ref MediaConfig::AudioChannelIdBaseFreq and
 *                   @ref MediaConfig::AudioChannelIdStepFreq.
 * - @c PcmMarker  — sample-domain framing of a 64-bit payload
 *                   (frame BCD64 timecode when available, otherwise
 *                   a per-instance monotonic counter).  Enables
 *                   bit-exact round-trip verification of the audio
 *                   pipeline — the analog of the picture-side
 *                   @ref ImageDataEncoder / @ref ImageDataDecoder.
 * - @c WhiteNoise — flat-spectrum noise generated from a cached
 *                   pseudo-random buffer.  The buffer is built once
 *                   from a fixed seed in @ref AudioTestPattern::configure
 *                   and shared across WhiteNoise channels, with a
 *                   per-channel read offset so the channels stay
 *                   decorrelated.
 * - @c PinkNoise  — -3 dB/octave noise generated from a cached buffer
 *                   (Paul Kellet's IIR filter applied to white noise).
 *                   Same caching strategy as WhiteNoise.
 * - @c Chirp      — continuous logarithmic sine sweep from
 *                   @ref MediaConfig::AudioChirpStartFreq to
 *                   @ref MediaConfig::AudioChirpEndFreq over
 *                   @ref MediaConfig::AudioChirpDurationSec.  Phase
 *                   is integrated across create() calls so chunk
 *                   boundaries don't introduce clicks; the sweep
 *                   loops once it reaches the end frequency.
 * - @c DualTone   — two simultaneous sines summed on the channel.
 *                   The default configuration reproduces the SMPTE
 *                   IMD-1 reference (60 Hz + 7 kHz at 4:1 amplitude)
 *                   for intermodulation-distortion measurement.
 *                   See @ref MediaConfig::AudioDualToneFreq1,
 *                   @ref MediaConfig::AudioDualToneFreq2, and
 *                   @ref MediaConfig::AudioDualToneRatio.
 */
class AudioPattern : public TypedEnum<AudioPattern> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioPattern", 0,
                                { "Tone",       0 },
                                { "Silence",    1 },
                                { "LTC",        2 },
                                { "AvSync",     3 },
                                { "SrcProbe",   4 },
                                { "ChannelId",  5 },
                                { "PcmMarker",  6 },
                                { "WhiteNoise", 7 },
                                { "PinkNoise",  8 },
                                { "Chirp",      9 },
                                { "DualTone",  10 });  // default: Tone

                using TypedEnum<AudioPattern>::TypedEnum;

                static const AudioPattern Tone;
                static const AudioPattern Silence;
                static const AudioPattern LTC;
                static const AudioPattern AvSync;
                static const AudioPattern SrcProbe;
                static const AudioPattern ChannelId;
                static const AudioPattern PcmMarker;
                static const AudioPattern WhiteNoise;
                static const AudioPattern PinkNoise;
                static const AudioPattern Chirp;
                static const AudioPattern DualTone;
};

inline const AudioPattern AudioPattern::Tone       { 0  };
inline const AudioPattern AudioPattern::Silence    { 1  };
inline const AudioPattern AudioPattern::LTC        { 2  };
inline const AudioPattern AudioPattern::AvSync     { 3  };
inline const AudioPattern AudioPattern::SrcProbe   { 4  };
inline const AudioPattern AudioPattern::ChannelId  { 5  };
inline const AudioPattern AudioPattern::PcmMarker  { 6  };
inline const AudioPattern AudioPattern::WhiteNoise { 7  };
inline const AudioPattern AudioPattern::PinkNoise  { 8  };
inline const AudioPattern AudioPattern::Chirp      { 9  };
inline const AudioPattern AudioPattern::DualTone   { 10 };

/**
 * @brief Well-known Enum type for chroma subsampling modes.
 *
 * Mirrors @c JpegImageCodec::Subsampling in value and order.  Used as the
 * value type for @ref MediaConfig::JpegSubsampling and anywhere else a
 * simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
 */
class ChromaSubsampling : public TypedEnum<ChromaSubsampling> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ChromaSubsampling", 1,
                                { "YUV444", 0 },
                                { "YUV422", 1 },
                                { "YUV420", 2 });  // default: YUV422 (RFC 2435 JPEG-over-RTP compatible)

                using TypedEnum<ChromaSubsampling>::TypedEnum;

                static const ChromaSubsampling YUV444;
                static const ChromaSubsampling YUV422;
                static const ChromaSubsampling YUV420;
};

inline const ChromaSubsampling ChromaSubsampling::YUV444 { 0 };
inline const ChromaSubsampling ChromaSubsampling::YUV422 { 1 };
inline const ChromaSubsampling ChromaSubsampling::YUV420 { 2 };

/**
 * @brief Well-known Enum type for audio sample formats.
 *
 * Mirrors @c AudioDesc::DataType in value and order.  Used as the value
 * type for any config key that selects an audio sample format (e.g.
 * @c MediaConfig::OutputAudioDataType).
 *
 * The integer values match the @c AudioDesc::DataType enumeration so
 * callers can convert in either direction via a plain @c static_cast
 * on @c Enum::value().  The string names also match — e.g.
 * @c "PCMI_S16LE", @c "PCMI_Float32LE" — so legacy code that still
 * passes strings through @c AudioDesc::stringToDataType keeps working
 * when the same string is fed through the Enum lookup path.
 */
class AudioDataType : public TypedEnum<AudioDataType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioDataType", 1,
                                { "Invalid",        0  },
                                { "PCMI_Float32LE", 1  },
                                { "PCMI_Float32BE", 2  },
                                { "PCMI_S8",        3  },
                                { "PCMI_U8",        4  },
                                { "PCMI_S16LE",     5  },
                                { "PCMI_U16LE",     6  },
                                { "PCMI_S16BE",     7  },
                                { "PCMI_U16BE",     8  },
                                { "PCMI_S24LE",     9  },
                                { "PCMI_U24LE",     10 },
                                { "PCMI_S24BE",     11 },
                                { "PCMI_U24BE",     12 },
                                { "PCMI_S32LE",     13 },
                                { "PCMI_U32LE",     14 },
                                { "PCMI_S32BE",     15 },
                                { "PCMI_U32BE",     16 });  // default: PCMI_Float32LE

                using TypedEnum<AudioDataType>::TypedEnum;

                static const AudioDataType Invalid;
                static const AudioDataType PCMI_Float32LE;
                static const AudioDataType PCMI_Float32BE;
                static const AudioDataType PCMI_S8;
                static const AudioDataType PCMI_U8;
                static const AudioDataType PCMI_S16LE;
                static const AudioDataType PCMI_U16LE;
                static const AudioDataType PCMI_S16BE;
                static const AudioDataType PCMI_U16BE;
                static const AudioDataType PCMI_S24LE;
                static const AudioDataType PCMI_U24LE;
                static const AudioDataType PCMI_S24BE;
                static const AudioDataType PCMI_U24BE;
                static const AudioDataType PCMI_S32LE;
                static const AudioDataType PCMI_U32LE;
                static const AudioDataType PCMI_S32BE;
                static const AudioDataType PCMI_U32BE;
};

inline const AudioDataType AudioDataType::Invalid        { 0  };
inline const AudioDataType AudioDataType::PCMI_Float32LE { 1  };
inline const AudioDataType AudioDataType::PCMI_Float32BE { 2  };
inline const AudioDataType AudioDataType::PCMI_S8        { 3  };
inline const AudioDataType AudioDataType::PCMI_U8        { 4  };
inline const AudioDataType AudioDataType::PCMI_S16LE     { 5  };
inline const AudioDataType AudioDataType::PCMI_U16LE     { 6  };
inline const AudioDataType AudioDataType::PCMI_S16BE     { 7  };
inline const AudioDataType AudioDataType::PCMI_U16BE     { 8  };
inline const AudioDataType AudioDataType::PCMI_S24LE     { 9  };
inline const AudioDataType AudioDataType::PCMI_U24LE     { 10 };
inline const AudioDataType AudioDataType::PCMI_S24BE     { 11 };
inline const AudioDataType AudioDataType::PCMI_U24BE     { 12 };
inline const AudioDataType AudioDataType::PCMI_S32LE     { 13 };
inline const AudioDataType AudioDataType::PCMI_U32LE     { 14 };
inline const AudioDataType AudioDataType::PCMI_S32BE     { 15 };
inline const AudioDataType AudioDataType::PCMI_U32BE     { 16 };

/**
 * @brief Well-known Enum type for @ref CSCPipeline processing-path selection.
 *
 * Used as the value type for the @ref MediaConfig::CscPath config key.
 * @c Optimized lets the pipeline pick the best registered fast-path
 * kernel (or fall back to the SIMD-accelerated generic pipeline).
 * @c Scalar forces the generic float pipeline with SIMD disabled —
 * useful for debugging and as a reference for accuracy comparisons
 * against @ref Color::convert.
 */
class CscPath : public TypedEnum<CscPath> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("CscPath", 0,
                                { "Optimized", 0 },
                                { "Scalar",    1 });  // default: Optimized

                using TypedEnum<CscPath>::TypedEnum;

                static const CscPath Optimized;
                static const CscPath Scalar;
};

inline const CscPath CscPath::Optimized { 0 };
inline const CscPath CscPath::Scalar    { 1 };

/**
 * @brief Well-known Enum type for QuickTime / ISO-BMFF container layout.
 *
 * Used as the value type for the @ref MediaConfig::QuickTimeLayout config
 * key.  @c Classic writes a traditional movie atom ending in a single
 * moov atom; @c Fragmented writes an initial moov followed by a series
 * of moof / mdat fragment pairs, which is what streaming and live
 * ingest pipelines need.
 */
class QuickTimeLayout : public TypedEnum<QuickTimeLayout> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("QuickTimeLayout", 1,
                                { "Classic",    0 },
                                { "Fragmented", 1 });  // default: Fragmented

                using TypedEnum<QuickTimeLayout>::TypedEnum;

                static const QuickTimeLayout Classic;
                static const QuickTimeLayout Fragmented;
};

inline const QuickTimeLayout QuickTimeLayout::Classic    { 0 };
inline const QuickTimeLayout QuickTimeLayout::Fragmented { 1 };

/**
 * @brief Well-known Enum type for RTP sender pacing mode.
 *
 * Selects how the RTP sink stages space packets out over time.
 * Drives the @ref MediaConfig::RtpPacingMode config key and the
 * equivalent runtime path inside @c MediaIOTask_Rtp.
 *
 * - @c Auto     — pick the best available mechanism at open time.
 *                 On Linux this resolves to @c KernelFq; on other
 *                 platforms it falls back to @c Userspace.  This
 *                 is the default — callers only need to set an
 *                 explicit mode when they want to override the
 *                 platform default (e.g. @c None for loopback /
 *                 LAN tests, @c TxTime for ST 2110-21 deployments).
 * - @c None     — burst all packets at once.  Appropriate only
 *                 for loopback / LAN tests or when the downstream
 *                 network is guaranteed to absorb the burst.
 * - @c Userspace — pace by sleeping between sends (the
 *                 @c RtpSession::sendPacketsPaced() path).  Works
 *                 everywhere without kernel configuration but ties
 *                 up the worker thread during the pacing window.
 * - @c KernelFq — push the rate to @c SO_MAX_PACING_RATE and let
 *                 the @c fq qdisc space the packets with zero
 *                 per-packet CPU cost.
 * - @c TxTime   — per-packet @c SCM_TXTIME deadlines via the ETF
 *                 qdisc.  Only enabled when the transport and
 *                 kernel both support it; falls back to @c KernelFq
 *                 otherwise.  Used for ST 2110-21-grade pacing.
 */
class RtpPacingMode : public TypedEnum<RtpPacingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtpPacingMode", 4,
                                { "None",      0 },
                                { "Userspace", 1 },
                                { "KernelFq",  2 },
                                { "TxTime",    3 },
                                { "Auto",      4 });  // default: Auto

                using TypedEnum<RtpPacingMode>::TypedEnum;

                static const RtpPacingMode None;
                static const RtpPacingMode Userspace;
                static const RtpPacingMode KernelFq;
                static const RtpPacingMode TxTime;
                static const RtpPacingMode Auto;
};

inline const RtpPacingMode RtpPacingMode::None      { 0 };
inline const RtpPacingMode RtpPacingMode::Userspace { 1 };
inline const RtpPacingMode RtpPacingMode::KernelFq  { 2 };
inline const RtpPacingMode RtpPacingMode::TxTime    { 3 };
inline const RtpPacingMode RtpPacingMode::Auto      { 4 };

/**
 * @brief Well-known Enum type for the metadata-stream wire format over RTP.
 *
 * Selects how the @c MediaIOTask_Rtp metadata stream serializes
 * per-frame @ref Metadata objects onto the wire.
 *
 * - @c JsonMetadata — serialize the @ref Metadata container via
 *                     its JSON toJson representation and ship the
 *                     resulting bytes as a dynamic-PT RTP payload
 *                     (see @ref RtpPayloadJson).  Simple, not
 *                     interoperable, useful for intra-promeki
 *                     round-trips and bring-up.
 * - @c St2110_40    — SMPTE ST 2110-40 / RFC 8331 Ancillary Data
 *                     over RTP.  Carries SMPTE ST 291 ANC packets
 *                     (closed captions, AFD, SCTE-104, VITC, etc.).
 *                     Placeholder entry — not yet implemented; the
 *                     backend rejects this value until the ANC
 *                     payload class lands.
 */
class MetadataRtpFormat : public TypedEnum<MetadataRtpFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("MetadataRtpFormat", 0,
                                { "JsonMetadata", 0 },
                                { "St2110_40",    1 });  // default: JsonMetadata

                using TypedEnum<MetadataRtpFormat>::TypedEnum;

                static const MetadataRtpFormat JsonMetadata;
                static const MetadataRtpFormat St2110_40;
};

inline const MetadataRtpFormat MetadataRtpFormat::JsonMetadata { 0 };
inline const MetadataRtpFormat MetadataRtpFormat::St2110_40    { 1 };

/**
 * @brief Well-known Enum type for human-readable byte-count formatting.
 *
 * Selects the unit family used by @ref String::fromByteCount when
 * formatting an allocation size or similar byte-valued quantity:
 *
 * - @c Metric — powers of 1000 with SI suffixes: `B`, `KB`, `MB`,
 *               `GB`, `TB`, `PB`, `EB`.
 * - @c Binary — powers of 1024 with IEC suffixes: `B`, `KiB`, `MiB`,
 *               `GiB`, `TiB`, `PiB`, `EiB`.
 *
 * Inherits from @ref TypedEnum so function signatures can take
 * `const ByteCountStyle &` and get compile-time type checking —
 * other enum kinds (e.g. @c VideoPattern) will fail to compile
 * when passed in the same slot.  Runtime compatibility with
 * @ref Variant and any API that takes a plain @ref Enum is
 * preserved via implicit derived-to-base slicing.
 *
 * @par Example
 * @code
 * String s = String::fromByteCount(1048576, 3, ByteCountStyle::Binary);
 * // → "1 MiB"
 *
 * // Round-trip through Variant still works:
 * Variant v = ByteCountStyle::Binary;          // Enum slice
 * Enum e = v.get<Enum>();                      // "ByteCountStyle::Binary"
 * ByteCountStyle back{e.value()};              // back to typed
 * @endcode
 */
class ByteCountStyle : public TypedEnum<ByteCountStyle> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ByteCountStyle", 0,
                                { "Metric", 0 },
                                { "Binary", 1 });  // default: Metric

                using TypedEnum<ByteCountStyle>::TypedEnum;

                static const ByteCountStyle Metric;     ///< Powers of 1000 (`KB`, `MB`, ...).
                static const ByteCountStyle Binary;     ///< Powers of 1024 (`KiB`, `MiB`, ...).
};

inline const ByteCountStyle ByteCountStyle::Metric{0};
inline const ByteCountStyle ByteCountStyle::Binary{1};

/**
 * @brief Well-known Enum type for @c .imgseq sidecar path mode.
 *
 * Controls whether the directory written into an @c .imgseq sidecar
 * is expressed as a relative path (from the sidecar's own location)
 * or as an absolute path.  Used as the value type for the
 * @ref MediaConfig::SaveImgSeqPathMode config key.
 */
class ImgSeqPathMode : public TypedEnum<ImgSeqPathMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ImgSeqPathMode", 0,
                                { "Relative", 0 },
                                { "Absolute", 1 });  // default: Relative

                using TypedEnum<ImgSeqPathMode>::TypedEnum;

                static const ImgSeqPathMode Relative;
                static const ImgSeqPathMode Absolute;
};

inline const ImgSeqPathMode ImgSeqPathMode::Relative { 0 };
inline const ImgSeqPathMode ImgSeqPathMode::Absolute { 1 };

/**
 * @brief Selects the on-the-wire BCD time-address layout used by
 *        @ref Timecode::toBcd64 and @ref Timecode::fromBcd64.
 *
 * The 64-bit BCD packing carries the eight BCD time digits, the binary
 * groups (32 bits of user bits), the drop-frame flag, the color-frame
 * flag, and the binary group flags BGF0/BGF1/BGF2 — i.e. the same set
 * of fields that SMPTE 12M-1 (LTC) and SMPTE 12M-2 (VITC) carry in
 * their respective time-address fields, minus the wire-level framing
 * (sync words, biphase mark transitions, CRC).  The two variants below
 * differ in how a single bit position — bit 27, "the bit immediately
 * above the 3-bit seconds-tens field" — is interpreted, since SMPTE
 * 12M-1 and 12M-2 disagree on its meaning:
 *
 * | Bit 27 | LTC (SMPTE 12M-1)              | VITC (SMPTE 12M-2)         |
 * |--------|--------------------------------|----------------------------|
 * | Name   | Polarity correction (or BGF0)  | Field marker               |
 * | Source | Computed by libvtc to balance  | Sourced from               |
 * |        | the codeword's 0/1 count       | @ref Timecode::isFirstField |
 *
 * In @c Ltc mode, @ref Timecode::toBcd64 wraps libvtc's
 * @c vtc_ltc_pack so the polarity correction bit is set correctly per
 * SMPTE 12M-1.  In @c Vitc mode, the packer writes the time digits and
 * binary groups directly into the 64-bit word and uses bit 27 as the
 * field marker — which doubles as the HFR frame-pair identifier per
 * SMPTE 12M-2 / 12-3.
 *
 * The two variants are byte-identical for "well-behaved" timecodes
 * (no field marker, no userbits, balanced 0/1 counts) — the variant
 * mostly chooses *who computes* the auxiliary bits and which spec the
 * decoder should consult to interpret them.
 *
 * Default is @ref Vitc, since the typical libpromeki use case is
 * stamping a frame's identity (including the HFR field/pair bit) into
 * the image itself, where there is no biphase mark to balance.
 */
class TimecodePackFormat : public TypedEnum<TimecodePackFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("TimecodePackFormat", 0,
                                { "Vitc", 0 },
                                { "Ltc",  1 });  // default: Vitc

                using TypedEnum<TimecodePackFormat>::TypedEnum;

                static const TimecodePackFormat Vitc;
                static const TimecodePackFormat Ltc;
};

inline const TimecodePackFormat TimecodePackFormat::Vitc { 0 };
inline const TimecodePackFormat TimecodePackFormat::Ltc  { 1 };

/**
 * @brief Well-known Enum type for progressive / interlaced video scan mode.
 *
 * Describes how the rows of a single @ref ImageDesc are temporally
 * arranged.  Replaces the earlier bare @c bool interlaced flag with a
 * richer state that distinguishes progressive, interlaced with unknown
 * field order, even-field-first interlaced, and odd-field-first
 * interlaced content — which matters for field-aware deinterlacers,
 * SDI receivers that hand back raw fields, and metadata round-trips
 * through QuickTime / MXF containers.
 *
 * - @c Unknown             — scan mode is not specified (legacy /
 *                            unassigned default).
 * - @c Progressive         — all rows belong to the same temporal
 *                            sample; no field separation.
 * - @c Interlaced          — interlaced content with an unspecified
 *                            field order.  Used when the source
 *                            flagged the stream as interlaced but
 *                            didn't carry a reliable dominance
 *                            indicator — common with legacy DV, some
 *                            MXF variants, and raw SDI captures that
 *                            lost the container-level flag.
 * - @c InterlacedEvenFirst — interlaced with the even (top) field
 *                            first in time (NTSC / 480i, 1080i50,
 *                            1080i59.94 per SMPTE 274M).
 * - @c InterlacedOddFirst  — interlaced with the odd (bottom) field
 *                            first in time (PAL / 576i legacy, some
 *                            consumer DV variants).
 * - @c PsF                 — Progressive segmented Frame: a
 *                            progressive image carried as two fields
 *                            over an interlaced transport (common for
 *                            24p / 25p / 30p material on HD-SDI at
 *                            1080psf23.98, 1080psf24, 1080psf25,
 *                            1080psf29.97).  For display purposes it
 *                            behaves as progressive, but the wire
 *                            format is two-field.
 */
class VideoScanMode : public TypedEnum<VideoScanMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoScanMode", 0,
                                { "Unknown",             0 },
                                { "Progressive",         1 },
                                { "Interlaced",          2 },
                                { "InterlacedEvenFirst", 3 },
                                { "InterlacedOddFirst",  4 },
                                { "PsF",                 5 });  // default: Unknown

                using TypedEnum<VideoScanMode>::TypedEnum;

                static const VideoScanMode Unknown;
                static const VideoScanMode Progressive;
                static const VideoScanMode Interlaced;
                static const VideoScanMode InterlacedEvenFirst;
                static const VideoScanMode InterlacedOddFirst;
                static const VideoScanMode PsF;
};

inline const VideoScanMode VideoScanMode::Unknown             { 0 };
inline const VideoScanMode VideoScanMode::Progressive         { 1 };
inline const VideoScanMode VideoScanMode::Interlaced          { 2 };
inline const VideoScanMode VideoScanMode::InterlacedEvenFirst { 3 };
inline const VideoScanMode VideoScanMode::InterlacedOddFirst  { 4 };
inline const VideoScanMode VideoScanMode::PsF                 { 5 };

/**
 * @brief Well-known Enum type for MediaIO open direction.
 *
 * Describes the role a @ref MediaIO instance plays in its pipeline
 * from the backend's own perspective:
 *
 * - @c Input          — the @ref MediaIO @em accepts frames from
 *                       the caller (it is a sink — e.g. a file
 *                       writer, a display, an RTP sender).
 * - @c Output         — the @ref MediaIO @em provides frames to
 *                       the caller (it is a source — e.g. a file
 *                       reader, a capture card, a test pattern
 *                       generator).
 * - @c InputAndOutput — the @ref MediaIO both consumes and emits
 *                       frames in the same instance (a converter
 *                       or passthrough filter).
 *
 * Note that the mediaplay CLI's @c -i / @c -o flags are named for
 * the @em pipeline's input and output, which is the opposite
 * perspective: @c -i wires a source backend into the pipeline (so
 * it expects a backend registered with @ref Output support), and
 * @c -o wires a sink backend (expects @ref Input support).
 */
class MediaIODirection : public TypedEnum<MediaIODirection> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("MediaIODirection", 0,
                                { "Output",         0 },
                                { "Input",          1 },
                                { "InputAndOutput", 2 });  // default: Output (source)

                using TypedEnum<MediaIODirection>::TypedEnum;

                static const MediaIODirection Output;
                static const MediaIODirection Input;
                static const MediaIODirection InputAndOutput;
};

inline const MediaIODirection MediaIODirection::Output         { 0 };
inline const MediaIODirection MediaIODirection::Input          { 1 };
inline const MediaIODirection MediaIODirection::InputAndOutput { 2 };

/**
 * @brief Preferred audio source for image-sequence readers.
 *
 * Image sequences can carry audio in two places: embedded per-frame
 * data (e.g. DPX user-data blocks) or a sidecar audio file
 * (Broadcast WAV alongside the images).  This enum selects which
 * source is preferred.
 *
 * The value is a **hint**, not a hard requirement.  If the preferred
 * source is not available the backend falls back to the other:
 *
 * | Hint       | First choice     | Fallback         |
 * |------------|------------------|------------------|
 * | @c Sidecar | sidecar file     | embedded audio   |
 * | @c Embedded| embedded audio   | sidecar file     |
 *
 * Default is @c Sidecar.
 */
class AudioSourceHint : public TypedEnum<AudioSourceHint> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioSourceHint", 0,
                                { "Sidecar",  0 },
                                { "Embedded", 1 });  // default: Sidecar

                using TypedEnum<AudioSourceHint>::TypedEnum;

                static const AudioSourceHint Sidecar;
                static const AudioSourceHint Embedded;
};

inline const AudioSourceHint AudioSourceHint::Sidecar  { 0 };
inline const AudioSourceHint AudioSourceHint::Embedded { 1 };

/**
 * @brief Well-known Enum type for V4L2 power line frequency filter.
 *
 * Maps to @c V4L2_CID_POWER_LINE_FREQUENCY menu items.
 */
class V4l2PowerLineMode : public TypedEnum<V4l2PowerLineMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("V4l2PowerLineMode", 3,
                                { "Disabled", 0 },
                                { "50Hz",     1 },
                                { "60Hz",     2 },
                                { "Auto",     3 });  // default: Auto

                using TypedEnum<V4l2PowerLineMode>::TypedEnum;

                static const V4l2PowerLineMode Disabled;
                static const V4l2PowerLineMode Hz50;
                static const V4l2PowerLineMode Hz60;
                static const V4l2PowerLineMode Auto;
};

inline const V4l2PowerLineMode V4l2PowerLineMode::Disabled { 0 };
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz50     { 1 };
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz60     { 2 };
inline const V4l2PowerLineMode V4l2PowerLineMode::Auto     { 3 };

/**
 * @brief Well-known Enum type for V4L2 auto exposure mode.
 *
 * Maps to @c V4L2_CID_EXPOSURE_AUTO menu items.
 */
class V4l2ExposureMode : public TypedEnum<V4l2ExposureMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("V4l2ExposureMode", 3,
                                { "Auto",             0 },
                                { "Manual",           1 },
                                { "ShutterPriority",  2 },
                                { "AperturePriority", 3 });  // default: AperturePriority

                using TypedEnum<V4l2ExposureMode>::TypedEnum;

                static const V4l2ExposureMode Auto;
                static const V4l2ExposureMode Manual;
                static const V4l2ExposureMode ShutterPriority;
                static const V4l2ExposureMode AperturePriority;
};

inline const V4l2ExposureMode V4l2ExposureMode::Auto             { 0 };
inline const V4l2ExposureMode V4l2ExposureMode::Manual           { 1 };
inline const V4l2ExposureMode V4l2ExposureMode::ShutterPriority  { 2 };
inline const V4l2ExposureMode V4l2ExposureMode::AperturePriority { 3 };

/**
 * @brief Well-known Enum type for audio sample rate conversion quality.
 *
 * Selects the libsamplerate converter algorithm used by @ref AudioResampler.
 * The integer values map directly to libsamplerate's converter type
 * constants (@c SRC_SINC_BEST_QUALITY through @c SRC_ZERO_ORDER_HOLD),
 * so conversion is a plain @c static_cast on @c Enum::value().
 *
 * - @c SincBest      — highest quality sinc interpolation; most CPU.
 *                      Best for offline / non-real-time conversion.
 * - @c SincMedium    — good balance of quality and CPU; the default.
 *                      Suitable for most real-time use cases including
 *                      drift correction and timebase conversion.
 * - @c SincFastest   — lowest quality sinc; still bandlimited, no
 *                      aliasing.  Use when latency or CPU budget is
 *                      very tight.
 * - @c Linear        — linear interpolation.  Fast but introduces
 *                      aliasing on downsampling.  Acceptable for
 *                      preview / monitoring paths.
 * - @c ZeroOrderHold — nearest sample (sample-and-hold).  Useful
 *                      only for testing or intentional lo-fi effects.
 */
class SrcQuality : public TypedEnum<SrcQuality> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SrcQuality", 1,
                                { "SincBest",      0 },
                                { "SincMedium",    1 },
                                { "SincFastest",   2 },
                                { "Linear",        3 },
                                { "ZeroOrderHold", 4 });  // default: SincMedium

                using TypedEnum<SrcQuality>::TypedEnum;

                static const SrcQuality SincBest;
                static const SrcQuality SincMedium;
                static const SrcQuality SincFastest;
                static const SrcQuality Linear;
                static const SrcQuality ZeroOrderHold;
};

inline const SrcQuality SrcQuality::SincBest      { 0 };
inline const SrcQuality SrcQuality::SincMedium    { 1 };
inline const SrcQuality SrcQuality::SincFastest   { 2 };
inline const SrcQuality SrcQuality::Linear        { 3 };
inline const SrcQuality SrcQuality::ZeroOrderHold { 4 };

/**
 * @brief Well-known Enum type for clock domain epoch behaviour.
 *
 * Describes whether timestamps from a @ref ClockDomain are comparable
 * across independent streams and/or across machines.
 *
 * - @c PerStream  — each stream has its own origin.  Timestamps are
 *   only meaningful within a single stream; cross-stream subtraction
 *   is undefined without an external synchronisation event.
 * - @c Correlated — all streams in this domain share a common epoch
 *   within a process or machine (e.g. CLOCK_MONOTONIC).  Subtracting
 *   timestamps from different streams on the same machine yields a
 *   meaningful offset.
 * - @c Absolute   — the epoch is a defined real-world time reference
 *   (e.g. PTP/TAI, GPS).  Timestamps from different machines are
 *   directly comparable.
 */
class ClockEpoch : public TypedEnum<ClockEpoch> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ClockEpoch", 1,
                                { "PerStream",  0 },
                                { "Correlated", 1 },
                                { "Absolute",   2 });  // default: Correlated

                using TypedEnum<ClockEpoch>::TypedEnum;

                static const ClockEpoch PerStream;
                static const ClockEpoch Correlated;
                static const ClockEpoch Absolute;
};

inline const ClockEpoch ClockEpoch::PerStream  { 0 };
inline const ClockEpoch ClockEpoch::Correlated { 1 };
inline const ClockEpoch ClockEpoch::Absolute   { 2 };

/**
 * @brief Well-known Enum type for EUI-64 string formats.
 *
 * Selects the notation used by EUI64::toString(EUI64Format).
 *
 * - @c OctetHyphen  — `"aa-bb-cc-dd-ee-ff-00-11"` (PTP SDP convention).
 * - @c OctetColon   — `"aa:bb:cc:dd:ee:ff:00:11"`.
 * - @c IPv6         — `"aabb:ccdd:eeff:0011"` (four colon-separated
 *                     16-bit groups, used in IPv6 interface identifiers).
 */
class EUI64Format : public TypedEnum<EUI64Format> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("EUI64Format", 0,
                                { "OctetHyphen", 0 },
                                { "OctetColon",  1 },
                                { "IPv6",        2 });  // default: OctetHyphen

                using TypedEnum<EUI64Format>::TypedEnum;

                static const EUI64Format OctetHyphen;
                static const EUI64Format OctetColon;
                static const EUI64Format IPv6;
};

inline const EUI64Format EUI64Format::OctetHyphen { 0 };
inline const EUI64Format EUI64Format::OctetColon  { 1 };
inline const EUI64Format EUI64Format::IPv6        { 2 };

/**
 * @brief Well-known Enum type for @c MediaIOTask_Inspector test selection.
 *
 * Element type for the @ref MediaConfig::InspectorTests EnumList — the
 * inspector consumes a list of tests to run.  An empty list runs the
 * default suite (every value below); a non-empty list runs exactly the
 * listed tests and disables the rest.
 *
 * - @c ImageData  — decode the @c ImageDataEncoder bands carried in
 *                   the picture (frame number, stream ID, picture TC).
 * - @c Ltc        — decode LTC from the audio track.
 * - @c TcSync     — picture TC vs audio LTC offset in samples.
 *                   Implies @c ImageData + @c Ltc.
 * - @c Continuity — frame number / stream ID / TC continuity.
 *                   Implies @c ImageData.
 * - @c Timestamp    — per-essence @ref MediaTimeStamp existence
 *                     check, frame-to-frame delta (min / max / avg),
 *                     and actual observed FPS.
 * - @c AudioSamples — per-frame audio sample count (min / max / avg)
 *                     plus measured audio sample rate derived from
 *                     cumulative samples and audio MediaTimeStamps.
 * - @c CaptureStats — write a per-frame TSV record (timestamps, image
 *                     and audio formats, buffer sizes) to the file
 *                     named by @ref MediaConfig::InspectorStatsFile
 *                     (or a unique file in @c Dir::temp() when that
 *                     key is empty).
 */
class InspectorTest : public TypedEnum<InspectorTest> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("InspectorTest", 0,
                                { "ImageData",    0 },
                                { "Ltc",          1 },
                                { "TcSync",       2 },
                                { "Continuity",   3 },
                                { "Timestamp",    4 },
                                { "AudioSamples", 5 },
                                { "CaptureStats", 6 });

                using TypedEnum<InspectorTest>::TypedEnum;

                static const InspectorTest ImageData;
                static const InspectorTest Ltc;
                static const InspectorTest TcSync;
                static const InspectorTest Continuity;
                static const InspectorTest Timestamp;
                static const InspectorTest AudioSamples;
                static const InspectorTest CaptureStats;
};

inline const InspectorTest InspectorTest::ImageData    { 0 };
inline const InspectorTest InspectorTest::Ltc          { 1 };
inline const InspectorTest InspectorTest::TcSync       { 2 };
inline const InspectorTest InspectorTest::Continuity   { 3 };
inline const InspectorTest InspectorTest::Timestamp    { 4 };
inline const InspectorTest InspectorTest::AudioSamples { 5 };
inline const InspectorTest InspectorTest::CaptureStats { 6 };

/**
 * @brief Well-known Enum type for video-encoder rate-control modes.
 *
 * Selects the rate-control strategy a @ref VideoEncoder uses when
 * producing a bitstream.
 *
 * - @c CBR — constant bitrate.  The encoder targets @ref
 *   MediaConfig::BitrateKbps with as little short-term variation as
 *   possible.  Use for live streaming, broadcast contribution, and
 *   any transport where bandwidth must be tightly bounded.
 * - @c VBR — variable bitrate.  The encoder targets an average
 *   bitrate but allows short-term variation to preserve quality on
 *   complex content.  Use for file storage where the average
 *   matters but instantaneous peaks do not.
 * - @c CQP — constant quantization parameter.  The encoder ignores
 *   @c BitrateKbps and instead holds quality constant by using a
 *   fixed QP; the resulting bitrate varies with content complexity.
 *   Use for testing / quality analysis where reproducible quality
 *   matters more than bitrate.
 */
class VideoRateControl : public TypedEnum<VideoRateControl> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoRateControl", 1,
                                { "CBR", 0 },
                                { "VBR", 1 },
                                { "CQP", 2 });  // default: VBR

                using TypedEnum<VideoRateControl>::TypedEnum;

                static const VideoRateControl CBR;
                static const VideoRateControl VBR;
                static const VideoRateControl CQP;
};

inline const VideoRateControl VideoRateControl::CBR { 0 };
inline const VideoRateControl VideoRateControl::VBR { 1 };
inline const VideoRateControl VideoRateControl::CQP { 2 };

/**
 * @brief Well-known Enum type for video-encoder speed / quality presets.
 *
 * Presets are neutral names for points along the
 * encode-speed-vs-quality curve.  Each concrete backend maps the
 * generic preset onto its own native preset enum (NVENC's P1–P7,
 * x264's @c ultrafast…@c placebo, QSV's target usage, etc.).
 *
 * - @c UltraLowLatency — absolute minimum encode latency.  Typically
 *   disables B-frames, look-ahead, and multi-pass.  Use for live
 *   conferencing / interactive capture where every frame of latency
 *   costs.
 * - @c LowLatency      — low-latency tuning with some coding tools
 *   enabled.  Suitable for live streaming contribution.
 * - @c Balanced        — default midpoint.  Reasonable latency and
 *   reasonable quality at a sensible CPU / GPU cost.
 * - @c HighQuality     — prioritise quality: multi-pass / look-ahead /
 *   slower motion search.  Use for file-based encoding where
 *   latency is unconstrained.
 */
class VideoEncoderPreset : public TypedEnum<VideoEncoderPreset> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoEncoderPreset", 2,
                                { "UltraLowLatency", 0 },
                                { "LowLatency",      1 },
                                { "Balanced",        2 },
                                { "HighQuality",     3 });  // default: Balanced

                using TypedEnum<VideoEncoderPreset>::TypedEnum;

                static const VideoEncoderPreset UltraLowLatency;
                static const VideoEncoderPreset LowLatency;
                static const VideoEncoderPreset Balanced;
                static const VideoEncoderPreset HighQuality;
};

inline const VideoEncoderPreset VideoEncoderPreset::UltraLowLatency { 0 };
inline const VideoEncoderPreset VideoEncoderPreset::LowLatency      { 1 };
inline const VideoEncoderPreset VideoEncoderPreset::Balanced        { 2 };
inline const VideoEncoderPreset VideoEncoderPreset::HighQuality     { 3 };

/** @} */

PROMEKI_NAMESPACE_END
