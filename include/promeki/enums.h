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
                static inline const Enum::Type Type = Enum::registerType("VideoPattern",
                        {
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
                                { "AvSync",       11 }
                        },
                        0);  // default: ColorBars

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
                static inline const Enum::Type Type = Enum::registerType("BurnPosition",
                        {
                                { "TopLeft",      0 },
                                { "TopCenter",    1 },
                                { "TopRight",     2 },
                                { "BottomLeft",   3 },
                                { "BottomCenter", 4 },
                                { "BottomRight",  5 },
                                { "Center",       6 }
                        },
                        4);  // default: BottomCenter

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
 * Mirrors @c AudioTestPattern::Mode in value and order.  Used as the value
 * type for the @c MediaIOTask_TPG @c ConfigAudioMode config key.
 *
 * Note that the config key is historically named @c ConfigAudioMode, but
 * the corresponding Enum type is called @c AudioPattern for consistency
 * with @ref VideoPattern.
 */
class AudioPattern : public TypedEnum<AudioPattern> {
        public:
                static inline const Enum::Type Type = Enum::registerType("AudioPattern",
                        {
                                { "Tone",    0 },
                                { "Silence", 1 },
                                { "LTC",     2 },
                                { "AvSync",  3 }
                        },
                        0);  // default: Tone

                using TypedEnum<AudioPattern>::TypedEnum;

                static const AudioPattern Tone;
                static const AudioPattern Silence;
                static const AudioPattern LTC;
                static const AudioPattern AvSync;
};

inline const AudioPattern AudioPattern::Tone    { 0 };
inline const AudioPattern AudioPattern::Silence { 1 };
inline const AudioPattern AudioPattern::LTC     { 2 };
inline const AudioPattern AudioPattern::AvSync  { 3 };

/**
 * @brief Well-known Enum type for chroma subsampling modes.
 *
 * Mirrors @c JpegImageCodec::Subsampling in value and order.  Used as the
 * value type for @ref MediaConfig::JpegSubsampling and anywhere else a
 * simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
 */
class ChromaSubsampling : public TypedEnum<ChromaSubsampling> {
        public:
                static inline const Enum::Type Type = Enum::registerType("ChromaSubsampling",
                        {
                                { "YUV444", 0 },
                                { "YUV422", 1 },
                                { "YUV420", 2 }
                        },
                        1);  // default: YUV422 (RFC 2435 JPEG-over-RTP compatible)

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
                static inline const Enum::Type Type = Enum::registerType("AudioDataType",
                        {
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
                                { "PCMI_U32BE",     16 }
                        },
                        1);  // default: PCMI_Float32LE

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
                static inline const Enum::Type Type = Enum::registerType("CscPath",
                        {
                                { "Optimized", 0 },
                                { "Scalar",    1 }
                        },
                        0);  // default: Optimized

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
                static inline const Enum::Type Type = Enum::registerType("QuickTimeLayout",
                        {
                                { "Classic",    0 },
                                { "Fragmented", 1 }
                        },
                        1);  // default: Fragmented

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
                static inline const Enum::Type Type = Enum::registerType("RtpPacingMode",
                        {
                                { "None",      0 },
                                { "Userspace", 1 },
                                { "KernelFq",  2 },
                                { "TxTime",    3 },
                                { "Auto",      4 }
                        },
                        4);  // default: Auto

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
                static inline const Enum::Type Type = Enum::registerType("MetadataRtpFormat",
                        {
                                { "JsonMetadata", 0 },
                                { "St2110_40",    1 }
                        },
                        0);  // default: JsonMetadata

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
                static inline const Enum::Type Type = Enum::registerType("ByteCountStyle",
                        {
                                { "Metric", 0 },
                                { "Binary", 1 }
                        },
                        0);  // default: Metric

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
                static inline const Enum::Type Type = Enum::registerType("ImgSeqPathMode",
                        {
                                { "Relative", 0 },
                                { "Absolute", 1 }
                        },
                        0);  // default: Relative

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
                static inline const Enum::Type Type = Enum::registerType("TimecodePackFormat",
                        {
                                { "Vitc", 0 },
                                { "Ltc",  1 }
                        },
                        0);  // default: Vitc

                using TypedEnum<TimecodePackFormat>::TypedEnum;

                static const TimecodePackFormat Vitc;
                static const TimecodePackFormat Ltc;
};

inline const TimecodePackFormat TimecodePackFormat::Vitc { 0 };
inline const TimecodePackFormat TimecodePackFormat::Ltc  { 1 };

/** @} */

PROMEKI_NAMESPACE_END
