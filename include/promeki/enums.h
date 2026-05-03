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
 *   @ref Enum based API is preserved via public inheritance.
 *
 * Subsystems that consume these values (e.g. @c VideoTestPattern) alias
 * the well-known class directly via @c using, so there is no parallel
 * C++ @c enum to keep in sync.
 *
 * The registered string names use the same CamelCase spelling as the C++
 * constants on the wrapper class (e.g. @c "ColorBars", @c "BottomCenter",
 * @c "LTC").  Use the typed constants directly at call sites.
 * @{
 */

/**
 * @brief Well-known Enum type for video test pattern generator modes.
 *
 * The canonical enum for @c VideoTestPattern — used directly as
 * @c VideoTestPattern::Pattern (a type alias).  Config keys such as
 * @c MediaConfig::VideoPattern store and retrieve this type.
 *
 * @par Example
 * @code
 * cfg.set(MediaConfig::VideoPattern, VideoPattern::ZonePlate);
 * gen.setPattern(VideoPattern::ColorBars);
 * @endcode
 */
class VideoPattern : public TypedEnum<VideoPattern> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoPattern", 0, {"ColorBars", 0}, {"ColorBars75", 1}, {"Ramp", 2},
                                           {"Grid", 3}, {"Crosshatch", 4}, {"Checkerboard", 5}, {"SolidColor", 6},
                                           {"White", 7}, {"Black", 8}, {"Noise", 9}, {"ZonePlate", 10},
                                           {"ColorChecker", 11}, {"SMPTE219", 12}, {"AvSync", 13}, {"MultiBurst", 14},
                                           {"LimitRange", 15}, {"CircularZone", 16}, {"Alignment", 17},
                                           {"SDIPathEQ", 18}, {"SDIPathPLL", 19}); // default: ColorBars

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
                static const VideoPattern ColorChecker;
                static const VideoPattern SMPTE219;
                static const VideoPattern AvSync;
                static const VideoPattern MultiBurst;
                static const VideoPattern LimitRange;
                static const VideoPattern CircularZone;
                static const VideoPattern Alignment;
                static const VideoPattern SDIPathEQ;
                static const VideoPattern SDIPathPLL;
};

inline const VideoPattern VideoPattern::ColorBars{0};
inline const VideoPattern VideoPattern::ColorBars75{1};
inline const VideoPattern VideoPattern::Ramp{2};
inline const VideoPattern VideoPattern::Grid{3};
inline const VideoPattern VideoPattern::Crosshatch{4};
inline const VideoPattern VideoPattern::Checkerboard{5};
inline const VideoPattern VideoPattern::SolidColor{6};
inline const VideoPattern VideoPattern::White{7};
inline const VideoPattern VideoPattern::Black{8};
inline const VideoPattern VideoPattern::Noise{9};
inline const VideoPattern VideoPattern::ZonePlate{10};
inline const VideoPattern VideoPattern::ColorChecker{11};
inline const VideoPattern VideoPattern::SMPTE219{12};
inline const VideoPattern VideoPattern::AvSync{13};
inline const VideoPattern VideoPattern::MultiBurst{14};
inline const VideoPattern VideoPattern::LimitRange{15};
inline const VideoPattern VideoPattern::CircularZone{16};
inline const VideoPattern VideoPattern::Alignment{17};
inline const VideoPattern VideoPattern::SDIPathEQ{18};
inline const VideoPattern VideoPattern::SDIPathPLL{19};

/**
 * @brief Well-known Enum type for on-screen burn-in position presets.
 *
 * The canonical enum for burn-in position — used directly by
 * @c VideoTestPattern.  Config keys such as
 * @c MediaConfig::VideoBurnPosition store and retrieve this type.
 */
class BurnPosition : public TypedEnum<BurnPosition> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("BurnPosition", 4, {"TopLeft", 0}, {"TopCenter", 1}, {"TopRight", 2},
                                           {"BottomLeft", 3}, {"BottomCenter", 4}, {"BottomRight", 5},
                                           {"Center", 6}); // default: BottomCenter

                using TypedEnum<BurnPosition>::TypedEnum;

                static const BurnPosition TopLeft;
                static const BurnPosition TopCenter;
                static const BurnPosition TopRight;
                static const BurnPosition BottomLeft;
                static const BurnPosition BottomCenter;
                static const BurnPosition BottomRight;
                static const BurnPosition Center;
};

inline const BurnPosition BurnPosition::TopLeft{0};
inline const BurnPosition BurnPosition::TopCenter{1};
inline const BurnPosition BurnPosition::TopRight{2};
inline const BurnPosition BurnPosition::BottomLeft{3};
inline const BurnPosition BurnPosition::BottomCenter{4};
inline const BurnPosition BurnPosition::BottomRight{5};
inline const BurnPosition BurnPosition::Center{6};

/**
 * @brief Open direction for backends that can act as either a source or a sink.
 *
 * File-based backends (ImageFile, AudioFile, DebugMedia, Quicktime,
 * etc.) consult @ref MediaConfig::OpenMode during open to decide
 * whether to register a source or a sink.  Defaults to @c Read so
 * the common case of opening an existing file for playback needs no
 * extra config.
 */
class MediaIOOpenMode : public TypedEnum<MediaIOOpenMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("MediaIOOpenMode", 0, {"Read", 0}, {"Write", 1}); // default: Read

                using TypedEnum<MediaIOOpenMode>::TypedEnum;

                static const MediaIOOpenMode Read;
                static const MediaIOOpenMode Write;
};

inline const MediaIOOpenMode MediaIOOpenMode::Read{0};
inline const MediaIOOpenMode MediaIOOpenMode::Write{1};

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
 * - @c Sweep      — linear frequency sweep from
 *                   @ref AudioTestPattern::sweepStartFreq to
 *                   @ref AudioTestPattern::sweepEndFreq over
 *                   @ref AudioTestPattern::sweepDurationSec.
 *                   Complements the logarithmic @c Chirp for
 *                   frequency-response measurements where a
 *                   constant Hz/s sweep rate is preferred.
 * - @c Polarity   — positive-going half-sine impulse repeated at
 *                   @ref AudioTestPattern::polarityPulseHz.  If the
 *                   received waveform goes positive first, the path
 *                   is non-inverting; negative first means an
 *                   odd number of polarity inversions.
 * - @c SteppedTone — discrete frequency steps cycling through
 *                   @ref AudioTestPattern::steppedToneFreqs, each
 *                   held for @ref AudioTestPattern::steppedToneStepSec.
 *                   Default 100 Hz / 1 kHz / 10 kHz covers the
 *                   low / mid / high bands for per-band level
 *                   alignment.
 * - @c Blits      — simplified EBU Tech 3304 BLITS (Broadcast
 *                   Loudness and Identification Tone Sequence).
 *                   Channel-aware cycle: all-channel tone, sequential
 *                   per-channel identification, polarity check (odd
 *                   channels inverted), then silence.
 * - @c EbuLineup  — EBU line-up tone: 1 kHz at −18 dBFS with a
 *                   3 s on / 2 s off cadence.  The standard reference
 *                   for broadcast level alignment.
 * - @c Dialnorm   — broadband pink noise calibrated to a target
 *                   loudness (default −24 dBFS RMS, approximating
 *                   −24 LKFS per ITU-R BS.1770).  Reads from the
 *                   same cached pink-noise buffer as @c PinkNoise
 *                   but at its own amplitude.
 * - @c Iec60958   — biphase-mark-encoded IEC 60958 professional
 *                   channel-status block (192 bits) in the PCM
 *                   domain.  Enables bit-exact verification of
 *                   digital audio transport paths.
 */
class AudioPattern : public TypedEnum<AudioPattern> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioPattern", 0, {"Tone", 0}, {"Silence", 1}, {"LTC", 2}, {"AvSync", 3},
                                           {"SrcProbe", 4}, {"ChannelId", 5}, {"PcmMarker", 6}, {"WhiteNoise", 7},
                                           {"PinkNoise", 8}, {"Chirp", 9}, {"DualTone", 10}, {"Sweep", 11},
                                           {"Polarity", 12}, {"SteppedTone", 13}, {"Blits", 14}, {"EbuLineup", 15},
                                           {"Dialnorm", 16}, {"Iec60958", 17}); // default: Tone

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
                static const AudioPattern Sweep;
                static const AudioPattern Polarity;
                static const AudioPattern SteppedTone;
                static const AudioPattern Blits;
                static const AudioPattern EbuLineup;
                static const AudioPattern Dialnorm;
                static const AudioPattern Iec60958;
};

inline const AudioPattern AudioPattern::Tone{0};
inline const AudioPattern AudioPattern::Silence{1};
inline const AudioPattern AudioPattern::LTC{2};
inline const AudioPattern AudioPattern::AvSync{3};
inline const AudioPattern AudioPattern::SrcProbe{4};
inline const AudioPattern AudioPattern::ChannelId{5};
inline const AudioPattern AudioPattern::PcmMarker{6};
inline const AudioPattern AudioPattern::WhiteNoise{7};
inline const AudioPattern AudioPattern::PinkNoise{8};
inline const AudioPattern AudioPattern::Chirp{9};
inline const AudioPattern AudioPattern::DualTone{10};
inline const AudioPattern AudioPattern::Sweep{11};
inline const AudioPattern AudioPattern::Polarity{12};
inline const AudioPattern AudioPattern::SteppedTone{13};
inline const AudioPattern AudioPattern::Blits{14};
inline const AudioPattern AudioPattern::EbuLineup{15};
inline const AudioPattern AudioPattern::Dialnorm{16};
inline const AudioPattern AudioPattern::Iec60958{17};

/**
 * @brief Well-known audio event marker categories.
 * @ingroup proav
 *
 * Tag carried by every entry in an @ref AudioMarkerList — the
 * @ref Metadata::AudioMarkers value attached to an audio payload.
 * Each entry locates a sample region within the carrying payload
 * (@c offset + @c length) and classifies the region with one of
 * these types so downstream stages can decide what to do with it
 * (display a glitch indicator, exclude from loudness measurements,
 * route through a concealment stage, …) without having to know
 * which backend produced the marker.
 *
 * - @c Unknown        — placeholder / category not assigned.  The
 *                       region is annotated but the producing
 *                       stage either could not classify it or has
 *                       not yet adopted the @ref AudioMarkerType
 *                       vocabulary.
 * - @c SilenceFill    — synthesized silence inserted by the
 *                       producer to bridge a gap in the source
 *                       timeline (e.g. NDI / RTP receiver gap
 *                       concealment).  The samples in the region
 *                       are guaranteed silent in the format-correct
 *                       sense.
 * - @c ConcealedLoss  — packet loss that was concealed by the
 *                       producer (PLC, FEC, last-good repeat, etc.)
 *                       — the region carries plausible but
 *                       reconstructed samples, not silence.
 * - @c Discontinuity  — boundary marker between two segments of
 *                       audio that should not be assumed temporally
 *                       contiguous (clock change, source switch,
 *                       resync).  Typically @c length == 0.
 * - @c Glitch         — the producer detected a sample-level anomaly
 *                       in the region (xrun, clipping, NaN, etc.).
 *                       The samples are passed through unmodified;
 *                       the marker is purely informational.
 *
 * Default value is @c Unknown.
 */
class AudioMarkerType : public TypedEnum<AudioMarkerType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioMarkerType", 0, {"Unknown", 0}, {"SilenceFill", 1},
                                           {"ConcealedLoss", 2}, {"Discontinuity", 3}, {"Glitch", 4});

                using TypedEnum<AudioMarkerType>::TypedEnum;

                static const AudioMarkerType Unknown;
                static const AudioMarkerType SilenceFill;
                static const AudioMarkerType ConcealedLoss;
                static const AudioMarkerType Discontinuity;
                static const AudioMarkerType Glitch;
};

inline const AudioMarkerType AudioMarkerType::Unknown{0};
inline const AudioMarkerType AudioMarkerType::SilenceFill{1};
inline const AudioMarkerType AudioMarkerType::ConcealedLoss{2};
inline const AudioMarkerType AudioMarkerType::Discontinuity{3};
inline const AudioMarkerType AudioMarkerType::Glitch{4};

/**
 * @brief Well-known Enum type for chroma subsampling modes.
 *
 * Mirrors @c JpegVideoEncoder::Subsampling in value and order.  Used as the
 * value type for @ref MediaConfig::JpegSubsampling and anywhere else a
 * simple 4:4:4 / 4:2:2 / 4:2:0 selection is needed.
 */
class ChromaSubsampling : public TypedEnum<ChromaSubsampling> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ChromaSubsampling", 1, {"YUV444", 0}, {"YUV422", 1},
                                           {"YUV420", 2}); // default: YUV422 (RFC 2435 JPEG-over-RTP compatible)

                using TypedEnum<ChromaSubsampling>::TypedEnum;

                static const ChromaSubsampling YUV444;
                static const ChromaSubsampling YUV422;
                static const ChromaSubsampling YUV420;
};

inline const ChromaSubsampling ChromaSubsampling::YUV444{0};
inline const ChromaSubsampling ChromaSubsampling::YUV422{1};
inline const ChromaSubsampling ChromaSubsampling::YUV420{2};

/**
 * @brief Well-known Enum type for audio sample formats.
 *
 * Mirrors @c AudioFormat::ID in value and order.  Used as the value
 * type for any config key that selects an audio sample format (e.g.
 * @c MediaConfig::OutputAudioDataType).
 *
 * The integer values match the @c AudioFormat::ID enumeration for
 * PCM interleaved formats so callers can convert in either direction
 * via a plain @c static_cast on @c Enum::value().  The string names
 * also match — e.g. @c "PCMI_S16LE", @c "PCMI_Float32LE" — so code
 * that round-trips through @c AudioFormat::lookup keeps working when
 * the same string is fed through the Enum lookup path.
 */
class AudioDataType : public TypedEnum<AudioDataType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioDataType", 1, {"Invalid", 0}, {"PCMI_Float32LE", 1},
                                           {"PCMI_Float32BE", 2}, {"PCMI_S8", 3}, {"PCMI_U8", 4}, {"PCMI_S16LE", 5},
                                           {"PCMI_U16LE", 6}, {"PCMI_S16BE", 7}, {"PCMI_U16BE", 8}, {"PCMI_S24LE", 9},
                                           {"PCMI_U24LE", 10}, {"PCMI_S24BE", 11}, {"PCMI_U24BE", 12},
                                           {"PCMI_S32LE", 13}, {"PCMI_U32LE", 14}, {"PCMI_S32BE", 15},
                                           {"PCMI_U32BE", 16}); // default: PCMI_Float32LE

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

inline const AudioDataType AudioDataType::Invalid{0};
inline const AudioDataType AudioDataType::PCMI_Float32LE{1};
inline const AudioDataType AudioDataType::PCMI_Float32BE{2};
inline const AudioDataType AudioDataType::PCMI_S8{3};
inline const AudioDataType AudioDataType::PCMI_U8{4};
inline const AudioDataType AudioDataType::PCMI_S16LE{5};
inline const AudioDataType AudioDataType::PCMI_U16LE{6};
inline const AudioDataType AudioDataType::PCMI_S16BE{7};
inline const AudioDataType AudioDataType::PCMI_U16BE{8};
inline const AudioDataType AudioDataType::PCMI_S24LE{9};
inline const AudioDataType AudioDataType::PCMI_U24LE{10};
inline const AudioDataType AudioDataType::PCMI_S24BE{11};
inline const AudioDataType AudioDataType::PCMI_U24BE{12};
inline const AudioDataType AudioDataType::PCMI_S32LE{13};
inline const AudioDataType AudioDataType::PCMI_U32LE{14};
inline const AudioDataType AudioDataType::PCMI_S32BE{15};
inline const AudioDataType AudioDataType::PCMI_U32BE{16};

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
                PROMEKI_REGISTER_ENUM_TYPE("CscPath", 0, {"Optimized", 0}, {"Scalar", 1}); // default: Optimized

                using TypedEnum<CscPath>::TypedEnum;

                static const CscPath Optimized;
                static const CscPath Scalar;
};

inline const CscPath CscPath::Optimized{0};
inline const CscPath CscPath::Scalar{1};

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
                PROMEKI_REGISTER_ENUM_TYPE("QuickTimeLayout", 1, {"Classic", 0},
                                           {"Fragmented", 1}); // default: Fragmented

                using TypedEnum<QuickTimeLayout>::TypedEnum;

                static const QuickTimeLayout Classic;
                static const QuickTimeLayout Fragmented;
};

inline const QuickTimeLayout QuickTimeLayout::Classic{0};
inline const QuickTimeLayout QuickTimeLayout::Fragmented{1};

/**
 * @brief Well-known Enum type for RTP sender pacing mode.
 *
 * Selects how the RTP sink stages space packets out over time.
 * Drives the @ref MediaConfig::RtpPacingMode config key and the
 * equivalent runtime path inside @c RtpMediaIO.
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
                PROMEKI_REGISTER_ENUM_TYPE("RtpPacingMode", 4, {"None", 0}, {"Userspace", 1}, {"KernelFq", 2},
                                           {"TxTime", 3}, {"Auto", 4}); // default: Auto

                using TypedEnum<RtpPacingMode>::TypedEnum;

                static const RtpPacingMode None;
                static const RtpPacingMode Userspace;
                static const RtpPacingMode KernelFq;
                static const RtpPacingMode TxTime;
                static const RtpPacingMode Auto;
};

inline const RtpPacingMode RtpPacingMode::None{0};
inline const RtpPacingMode RtpPacingMode::Userspace{1};
inline const RtpPacingMode RtpPacingMode::KernelFq{2};
inline const RtpPacingMode RtpPacingMode::TxTime{3};
inline const RtpPacingMode RtpPacingMode::Auto{4};

/**
 * @brief Well-known Enum type for the metadata-stream wire format over RTP.
 *
 * Selects how the @c RtpMediaIO metadata stream serializes
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
                PROMEKI_REGISTER_ENUM_TYPE("MetadataRtpFormat", 0, {"JsonMetadata", 0},
                                           {"St2110_40", 1}); // default: JsonMetadata

                using TypedEnum<MetadataRtpFormat>::TypedEnum;

                static const MetadataRtpFormat JsonMetadata;
                static const MetadataRtpFormat St2110_40;
};

inline const MetadataRtpFormat MetadataRtpFormat::JsonMetadata{0};
inline const MetadataRtpFormat MetadataRtpFormat::St2110_40{1};

/**
 * @brief Well-known Enum type for human-readable byte-count formatting.
 *
 * Selects the unit family used by @ref Units::fromByteCount when
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
 * String s = Units::fromByteCount(1048576, 3, ByteCountStyle::Binary);
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
                PROMEKI_REGISTER_ENUM_TYPE("ByteCountStyle", 0, {"Metric", 0}, {"Binary", 1}); // default: Metric

                using TypedEnum<ByteCountStyle>::TypedEnum;

                static const ByteCountStyle Metric; ///< Powers of 1000 (`KB`, `MB`, ...).
                static const ByteCountStyle Binary; ///< Powers of 1024 (`KiB`, `MiB`, ...).
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
                PROMEKI_REGISTER_ENUM_TYPE("ImgSeqPathMode", 0, {"Relative", 0}, {"Absolute", 1}); // default: Relative

                using TypedEnum<ImgSeqPathMode>::TypedEnum;

                static const ImgSeqPathMode Relative;
                static const ImgSeqPathMode Absolute;
};

inline const ImgSeqPathMode ImgSeqPathMode::Relative{0};
inline const ImgSeqPathMode ImgSeqPathMode::Absolute{1};

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
 * Default is @c TimecodePackFormat::Vitc, since the typical libpromeki use case is
 * stamping a frame's identity (including the HFR field/pair bit) into
 * the image itself, where there is no biphase mark to balance.
 */
class TimecodePackFormat : public TypedEnum<TimecodePackFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("TimecodePackFormat", 0, {"Vitc", 0}, {"Ltc", 1}); // default: Vitc

                using TypedEnum<TimecodePackFormat>::TypedEnum;

                static const TimecodePackFormat Vitc;
                static const TimecodePackFormat Ltc;
};

inline const TimecodePackFormat TimecodePackFormat::Vitc{0};
inline const TimecodePackFormat TimecodePackFormat::Ltc{1};

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
                PROMEKI_REGISTER_ENUM_TYPE("VideoScanMode", 0, {"Unknown", 0}, {"Progressive", 1}, {"Interlaced", 2},
                                           {"InterlacedEvenFirst", 3}, {"InterlacedOddFirst", 4},
                                           {"PsF", 5}); // default: Unknown

                using TypedEnum<VideoScanMode>::TypedEnum;

                static const VideoScanMode Unknown;
                static const VideoScanMode Progressive;
                static const VideoScanMode Interlaced;
                static const VideoScanMode InterlacedEvenFirst;
                static const VideoScanMode InterlacedOddFirst;
                static const VideoScanMode PsF;

                /**
                 * @brief Returns true if this scan mode represents an
                 * interlaced (two-field) raster &mdash; @c VideoScanMode::Interlaced,
                 * @c VideoScanMode::InterlacedEvenFirst, or @c VideoScanMode::InterlacedOddFirst.
                 * @c VideoScanMode::PsF is @em not considered interlaced by this
                 * helper: its wire format is interlaced but its
                 * content (and coded bitstream, when packed) is
                 * progressive.
                 */
                bool isInterlaced() const {
                        const int v = value();
                        return v == 2    /*Interlaced*/
                               || v == 3 /*InterlacedEvenFirst*/
                               || v == 4 /*InterlacedOddFirst*/;
                }
};

inline const VideoScanMode VideoScanMode::Unknown{0};
inline const VideoScanMode VideoScanMode::Progressive{1};
inline const VideoScanMode VideoScanMode::Interlaced{2};
inline const VideoScanMode VideoScanMode::InterlacedEvenFirst{3};
inline const VideoScanMode VideoScanMode::InterlacedOddFirst{4};
inline const VideoScanMode VideoScanMode::PsF{5};

/**
 * @brief Well-known Enum type for MediaIO open direction.
 *
 * Describes the role a @ref MediaIO instance plays in its pipeline:
 *
 * - @c Source    — the @ref MediaIO @em provides frames to the
 *                  caller (e.g. a file reader, a capture card, a
 *                  test pattern generator).
 * - @c Sink      — the @ref MediaIO @em accepts frames from the
 *                  caller (e.g. a file writer, a display, an
 *                  RTP sender).
 * - @c Transform — the @ref MediaIO both consumes and emits frames
 *                  in the same instance (a converter, mixer, or
 *                  passthrough filter).
 *
 * The mediaplay CLI's @c -i / @c -o flags are named for the
 * @em pipeline's input and output: @c -i wires a @c MediaIODirection::Source backend
 * into the pipeline and @c -o wires a @c MediaIODirection::Sink backend.
 */
class MediaIODirection : public TypedEnum<MediaIODirection> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("MediaIODirection", 0, {"Source", 0}, {"Sink", 1},
                                           {"Transform", 2}); // default: Source

                using TypedEnum<MediaIODirection>::TypedEnum;

                static const MediaIODirection Source;
                static const MediaIODirection Sink;
                static const MediaIODirection Transform;
};

inline const MediaIODirection MediaIODirection::Source{0};
inline const MediaIODirection MediaIODirection::Sink{1};
inline const MediaIODirection MediaIODirection::Transform{2};

/**
 * @brief Well-known Enum type for the coarse category of a
 *        @ref MediaPayload.
 *
 * Returned by @ref MediaPayload::kind for cheap dispatch without
 * RTTI — pipeline glue can @c switch on the kind before falling
 * back to @ref MediaPayload::as for field-level access on a
 * concrete subclass.
 *
 * - @c Video         — Any video payload (compressed or
 *                      uncompressed); see @ref VideoPayload.
 * - @c Audio         — Any audio payload (compressed or
 *                      uncompressed); see @ref AudioPayload.
 * - @c Metadata      — Timed metadata track payloads (ID3 in MP4,
 *                      KLV in MXF, GPMF in GoPro, …).  Reserved
 *                      for future expansion.
 * - @c Subtitle      — Subtitle / caption payloads (SRT, WebVTT,
 *                      PGS, DVB).  Reserved for future expansion.
 * - @c AncillaryData — SMPTE 291M ancillary data, SDI-carried
 *                      side channels, CEA-608 / 708 closed
 *                      captions.  Reserved for future expansion.
 * - @c Custom        — Project-specific payload kind not covered
 *                      by the values above; the concrete subclass
 *                      identifies itself via its C++ type.
 */
class MediaPayloadKind : public TypedEnum<MediaPayloadKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("MediaPayloadKind", 0, {"Video", 0}, {"Audio", 1}, {"Metadata", 2},
                                           {"Subtitle", 3}, {"AncillaryData", 4}, {"Custom", 5}); // default: Video

                using TypedEnum<MediaPayloadKind>::TypedEnum;

                static const MediaPayloadKind Video;
                static const MediaPayloadKind Audio;
                static const MediaPayloadKind Metadata;
                static const MediaPayloadKind Subtitle;
                static const MediaPayloadKind AncillaryData;
                static const MediaPayloadKind Custom;
};

inline const MediaPayloadKind MediaPayloadKind::Video{0};
inline const MediaPayloadKind MediaPayloadKind::Audio{1};
inline const MediaPayloadKind MediaPayloadKind::Metadata{2};
inline const MediaPayloadKind MediaPayloadKind::Subtitle{3};
inline const MediaPayloadKind MediaPayloadKind::AncillaryData{4};
inline const MediaPayloadKind MediaPayloadKind::Custom{5};

/**
 * @brief Well-known Enum type for the role of a compressed video
 *        access unit within its stream.
 *
 * Returned by @ref CompressedVideoPayload::frameType for pipelines
 * that need to distinguish independently decodable frames from
 * forward- or bidirectionally-predicted ones without parsing the
 * bitstream.  The accessor is @c virtual so codec-specific
 * subclasses (H.264 / HEVC / ProRes wrappers) can map their
 * internal slice / picture types onto this common vocabulary.
 *
 * - @c Unknown — Role is not known or not meaningful for this
 *                codec / packet.
 * - @c I       — Intra-coded frame — decodes standalone but may
 *                not reset the DPB (distinguished from @c IDR for
 *                bitstreams that allow non-IDR I-pictures).
 * - @c P       — Predictive frame — references prior frames only.
 * - @c B       — Bidirectionally-predicted frame — references both
 *                past and future frames in decode order.
 * - @c IDR     — Instantaneous Decoder Refresh — a keyframe that
 *                also flushes the decoded picture buffer and is a
 *                valid random-access entry point.
 * - @c BRef    — Referenced B-frame (used by HEVC / AV1 hierarchical
 *                coding); B-picture whose reconstruction is used by
 *                other B pictures, so it is @em not discardable.
 */
class FrameType : public TypedEnum<FrameType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("FrameType", 0, {"Unknown", 0}, {"I", 1}, {"P", 2}, {"B", 3}, {"IDR", 4},
                                           {"BRef", 5}); // default: Unknown

                using TypedEnum<FrameType>::TypedEnum;

                static const FrameType Unknown;
                static const FrameType I;
                static const FrameType P;
                static const FrameType B;
                static const FrameType IDR;
                static const FrameType BRef;
};

inline const FrameType FrameType::Unknown{0};
inline const FrameType FrameType::I{1};
inline const FrameType FrameType::P{2};
inline const FrameType FrameType::B{3};
inline const FrameType FrameType::IDR{4};
inline const FrameType FrameType::BRef{5};

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
                PROMEKI_REGISTER_ENUM_TYPE("AudioSourceHint", 0, {"Sidecar", 0}, {"Embedded", 1}); // default: Sidecar

                using TypedEnum<AudioSourceHint>::TypedEnum;

                static const AudioSourceHint Sidecar;
                static const AudioSourceHint Embedded;
};

inline const AudioSourceHint AudioSourceHint::Sidecar{0};
inline const AudioSourceHint AudioSourceHint::Embedded{1};

/**
 * @brief Well-known Enum type for V4L2 power line frequency filter.
 *
 * Maps to @c V4L2_CID_POWER_LINE_FREQUENCY menu items.
 */
class V4l2PowerLineMode : public TypedEnum<V4l2PowerLineMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("V4l2PowerLineMode", 3, {"Disabled", 0}, {"50Hz", 1}, {"60Hz", 2},
                                           {"Auto", 3}); // default: Auto

                using TypedEnum<V4l2PowerLineMode>::TypedEnum;

                static const V4l2PowerLineMode Disabled;
                static const V4l2PowerLineMode Hz50;
                static const V4l2PowerLineMode Hz60;
                static const V4l2PowerLineMode Auto;
};

inline const V4l2PowerLineMode V4l2PowerLineMode::Disabled{0};
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz50{1};
inline const V4l2PowerLineMode V4l2PowerLineMode::Hz60{2};
inline const V4l2PowerLineMode V4l2PowerLineMode::Auto{3};

/**
 * @brief Well-known Enum type for V4L2 auto exposure mode.
 *
 * Maps to @c V4L2_CID_EXPOSURE_AUTO menu items.
 */
class V4l2ExposureMode : public TypedEnum<V4l2ExposureMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("V4l2ExposureMode", 3, {"Auto", 0}, {"Manual", 1}, {"ShutterPriority", 2},
                                           {"AperturePriority", 3}); // default: AperturePriority

                using TypedEnum<V4l2ExposureMode>::TypedEnum;

                static const V4l2ExposureMode Auto;
                static const V4l2ExposureMode Manual;
                static const V4l2ExposureMode ShutterPriority;
                static const V4l2ExposureMode AperturePriority;
};

inline const V4l2ExposureMode V4l2ExposureMode::Auto{0};
inline const V4l2ExposureMode V4l2ExposureMode::Manual{1};
inline const V4l2ExposureMode V4l2ExposureMode::ShutterPriority{2};
inline const V4l2ExposureMode V4l2ExposureMode::AperturePriority{3};

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
                PROMEKI_REGISTER_ENUM_TYPE("SrcQuality", 1, {"SincBest", 0}, {"SincMedium", 1}, {"SincFastest", 2},
                                           {"Linear", 3}, {"ZeroOrderHold", 4}); // default: SincMedium

                using TypedEnum<SrcQuality>::TypedEnum;

                static const SrcQuality SincBest;
                static const SrcQuality SincMedium;
                static const SrcQuality SincFastest;
                static const SrcQuality Linear;
                static const SrcQuality ZeroOrderHold;
};

inline const SrcQuality SrcQuality::SincBest{0};
inline const SrcQuality SrcQuality::SincMedium{1};
inline const SrcQuality SrcQuality::SincFastest{2};
inline const SrcQuality SrcQuality::Linear{3};
inline const SrcQuality SrcQuality::ZeroOrderHold{4};

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
                PROMEKI_REGISTER_ENUM_TYPE("ClockEpoch", 1, {"PerStream", 0}, {"Correlated", 1},
                                           {"Absolute", 2}); // default: Correlated

                using TypedEnum<ClockEpoch>::TypedEnum;

                static const ClockEpoch PerStream;
                static const ClockEpoch Correlated;
                static const ClockEpoch Absolute;
};

inline const ClockEpoch ClockEpoch::PerStream{0};
inline const ClockEpoch ClockEpoch::Correlated{1};
inline const ClockEpoch ClockEpoch::Absolute{2};

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
                PROMEKI_REGISTER_ENUM_TYPE("EUI64Format", 0, {"OctetHyphen", 0}, {"OctetColon", 1},
                                           {"IPv6", 2}); // default: OctetHyphen

                using TypedEnum<EUI64Format>::TypedEnum;

                static const EUI64Format OctetHyphen;
                static const EUI64Format OctetColon;
                static const EUI64Format IPv6;
};

inline const EUI64Format EUI64Format::OctetHyphen{0};
inline const EUI64Format EUI64Format::OctetColon{1};
inline const EUI64Format EUI64Format::IPv6{2};

/**
 * @brief Well-known Enum type for @c InspectorMediaIO test selection.
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
                PROMEKI_REGISTER_ENUM_TYPE("InspectorTest", 0, {"ImageData", 0}, {"Ltc", 1}, {"TcSync", 2},
                                           {"Continuity", 3}, {"Timestamp", 4}, {"AudioSamples", 5},
                                           {"CaptureStats", 6});

                using TypedEnum<InspectorTest>::TypedEnum;

                static const InspectorTest ImageData;
                static const InspectorTest Ltc;
                static const InspectorTest TcSync;
                static const InspectorTest Continuity;
                static const InspectorTest Timestamp;
                static const InspectorTest AudioSamples;
                static const InspectorTest CaptureStats;
};

inline const InspectorTest InspectorTest::ImageData{0};
inline const InspectorTest InspectorTest::Ltc{1};
inline const InspectorTest InspectorTest::TcSync{2};
inline const InspectorTest InspectorTest::Continuity{3};
inline const InspectorTest InspectorTest::Timestamp{4};
inline const InspectorTest InspectorTest::AudioSamples{5};
inline const InspectorTest InspectorTest::CaptureStats{6};

/**
 * @brief Well-known Enum type for codec rate-control modes (audio + video).
 *
 * Selects the rate-control strategy a codec uses when producing a
 * bitstream.  Single shared enum so audio and video sides of the codec
 * API can describe and configure rate-control with the same vocabulary.
 *
 * - @c CBR — constant bitrate.  The encoder targets
 *   @ref MediaConfig::BitrateKbps with as little short-term variation
 *   as possible.  Use for live streaming, broadcast contribution, and
 *   any transport where bandwidth must be tightly bounded.
 * - @c VBR — variable bitrate.  The encoder targets an average
 *   bitrate but allows short-term variation to preserve quality on
 *   complex content.  Use for file storage where the average
 *   matters but instantaneous peaks do not.
 * - @c ABR — average bitrate.  Long-term average is held to the
 *   target while short-term rate is allowed to drift more freely than
 *   under VBR.  Common on audio codecs (MP3, AAC).
 * - @c CQP — constant quantization parameter.  The encoder ignores
 *   the bitrate target and instead holds quality constant; the
 *   resulting bitrate varies with content complexity.  Use for
 *   testing / quality analysis where reproducible quality matters
 *   more than bitrate.  Common on video codecs.
 */
class RateControlMode : public TypedEnum<RateControlMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RateControlMode", 1, {"CBR", 0}, {"VBR", 1}, {"ABR", 2},
                                           {"CQP", 3}); // default: VBR

                using TypedEnum<RateControlMode>::TypedEnum;

                static const RateControlMode CBR;
                static const RateControlMode VBR;
                static const RateControlMode ABR;
                static const RateControlMode CQP;
};

inline const RateControlMode RateControlMode::CBR{0};
inline const RateControlMode RateControlMode::VBR{1};
inline const RateControlMode RateControlMode::ABR{2};
inline const RateControlMode RateControlMode::CQP{3};

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
                PROMEKI_REGISTER_ENUM_TYPE("VideoEncoderPreset", 2, {"UltraLowLatency", 0}, {"LowLatency", 1},
                                           {"Balanced", 2}, {"HighQuality", 3}, {"Lossless", 4}); // default: Balanced

                using TypedEnum<VideoEncoderPreset>::TypedEnum;

                static const VideoEncoderPreset UltraLowLatency;
                static const VideoEncoderPreset LowLatency;
                static const VideoEncoderPreset Balanced;
                static const VideoEncoderPreset HighQuality;
                static const VideoEncoderPreset Lossless;
};

inline const VideoEncoderPreset VideoEncoderPreset::UltraLowLatency{0};
inline const VideoEncoderPreset VideoEncoderPreset::LowLatency{1};
inline const VideoEncoderPreset VideoEncoderPreset::Balanced{2};
inline const VideoEncoderPreset VideoEncoderPreset::HighQuality{3};
inline const VideoEncoderPreset VideoEncoderPreset::Lossless{4};

/**
 * @brief Well-known Enum type for the Opus encoder application mode.
 *
 * Maps directly onto libopus's @c OPUS_APPLICATION_* constants:
 *
 *  - @c Voip      → @c OPUS_APPLICATION_VOIP — best quality at a given
 *                   bitrate for voice; emphasises voice intelligibility
 *                   over musical fidelity.
 *  - @c Audio     → @c OPUS_APPLICATION_AUDIO — best quality at a given
 *                   bitrate for music and most non-voice content.
 *  - @c LowDelay  → @c OPUS_APPLICATION_RESTRICTED_LOWDELAY — disables
 *                   the speech-optimised mode and drops the additional
 *                   look-ahead, lowering algorithmic delay at a small
 *                   quality cost.
 *
 * Selected via @ref MediaConfig::OpusApplication; the codec default is
 * @c Audio so general-purpose pipelines get full-fidelity behaviour
 * without explicit configuration.
 */
class OpusApplication : public TypedEnum<OpusApplication> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("OpusApplication", 1, {"Voip", 0}, {"Audio", 1},
                                           {"LowDelay", 2}); // default: Audio

                using TypedEnum<OpusApplication>::TypedEnum;

                static const OpusApplication Voip;
                static const OpusApplication Audio;
                static const OpusApplication LowDelay;
};

inline const OpusApplication OpusApplication::Voip{0};
inline const OpusApplication OpusApplication::Audio{1};
inline const OpusApplication OpusApplication::LowDelay{2};

/**
 * @brief Well-known Enum type for video value range (aka quantization
 *        range / full-range flag).
 *
 * @c Limited means studio / broadcast / "video" range — 16..235 on 8-bit
 * Y'CbCr luma, 16..240 on the chroma channels, and the bit-depth
 * scaling of those values for 10/12/16-bit.  @c Full means the whole
 * digital range (0..255 on 8-bit, 0..2^N-1 in general).  @c Unknown is
 * the "auto-derive" / "not declared" default used by @ref PixelFormat
 * entries that pre-date the field being explicit, and by
 * @ref MediaConfig keys that want downstream code to infer the range
 * from the accompanying @ref PixelFormat.
 *
 * The numeric values are local to libpromeki and do @em not match any
 * codec-specific on-wire representation.  Encoders translate to
 * codec-native signalling (H.264/HEVC VUI @c videoFullRangeFlag, AV1
 * @c colorRange, etc.) at session init.
 */
class VideoRange : public TypedEnum<VideoRange> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoRange", 0, {"Unknown", 0}, {"Limited", 1},
                                           {"Full", 2}); // default: Unknown

                using TypedEnum<VideoRange>::TypedEnum;

                static const VideoRange Unknown;
                static const VideoRange Limited;
                static const VideoRange Full;
};

inline const VideoRange VideoRange::Unknown{0};
inline const VideoRange VideoRange::Limited{1};
inline const VideoRange VideoRange::Full{2};

/**
 * @brief Well-known Enum type for VUI / container color primaries.
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273 (and, by design,
 * the NV_ENC_VUI_COLOR_PRIMARIES / AV1 `color_primaries` enumerations
 * used in-bitstream by H.264 / HEVC / AV1).  Use this enum anywhere a
 * codec-agnostic color-primaries identifier is needed (VideoEncoder
 * VUI signalling, SDP `colorimetry=` parameters, etc.).
 *
 * Only the spec-registered values are exposed; reserved slots are
 * omitted.
 */
class ColorPrimaries : public TypedEnum<ColorPrimaries> {
        public:
                // The @c Auto numeric value (255) deliberately sits
                // outside the 0..22 H.273 value range so it can never
                // collide with a spec-registered primary.  Encoders
                // that see @c Auto resolve it by inspecting the input
                // PixelFormat's ColorModel at session init time.
                PROMEKI_REGISTER_ENUM_TYPE("ColorPrimaries", 255, {"BT709", 1}, {"Unspecified", 2}, {"BT470M", 4},
                                           {"BT470BG", 5}, {"SMPTE170M", 6}, {"SMPTE240M", 7}, {"Film", 8},
                                           {"BT2020", 9}, {"SMPTE428", 10}, {"SMPTE431", 11}, {"SMPTE432", 12},
                                           {"JEDEC_P22", 22}, {"Auto", 255}); // default: Auto

                using TypedEnum<ColorPrimaries>::TypedEnum;

                static const ColorPrimaries Auto;
                static const ColorPrimaries Unspecified;
                static const ColorPrimaries BT709;
                static const ColorPrimaries BT470M;
                static const ColorPrimaries BT470BG;
                static const ColorPrimaries SMPTE170M;
                static const ColorPrimaries SMPTE240M;
                static const ColorPrimaries Film;
                static const ColorPrimaries BT2020;
                static const ColorPrimaries SMPTE428;
                static const ColorPrimaries SMPTE431;
                static const ColorPrimaries SMPTE432;
                static const ColorPrimaries JEDEC_P22;
};

inline const ColorPrimaries ColorPrimaries::Auto{255};
inline const ColorPrimaries ColorPrimaries::Unspecified{2};
inline const ColorPrimaries ColorPrimaries::BT709{1};
inline const ColorPrimaries ColorPrimaries::BT470M{4};
inline const ColorPrimaries ColorPrimaries::BT470BG{5};
inline const ColorPrimaries ColorPrimaries::SMPTE170M{6};
inline const ColorPrimaries ColorPrimaries::SMPTE240M{7};
inline const ColorPrimaries ColorPrimaries::Film{8};
inline const ColorPrimaries ColorPrimaries::BT2020{9};
inline const ColorPrimaries ColorPrimaries::SMPTE428{10};
inline const ColorPrimaries ColorPrimaries::SMPTE431{11};
inline const ColorPrimaries ColorPrimaries::SMPTE432{12};
inline const ColorPrimaries ColorPrimaries::JEDEC_P22{22};

/**
 * @brief Well-known Enum type for VUI / container transfer characteristic
 *        (opto-electronic transfer function).
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273.  This covers all
 * of the common SDR and HDR curves: @c BT709 (Rec.709 gamma),
 * @c SMPTE2084 (PQ, for HDR10 / HDR10+ / Dolby Vision base layer), and
 * @c ARIB_STD_B67 (HLG).
 */
class TransferCharacteristics : public TypedEnum<TransferCharacteristics> {
        public:
                // The @c Auto numeric value (255) sits outside the
                // 0..18 H.273 value range.  NB: auto-derivation
                // currently cannot pick between SDR gamma, PQ, and HLG
                // — the library's ColorModel doesn't distinguish HDR
                // transfer curves yet — so @c Auto resolves to the
                // SDR curve matching the primaries and callers must
                // set an explicit @c SMPTE2084 / @c ARIB_STD_B67 for
                // HDR content.
                PROMEKI_REGISTER_ENUM_TYPE("TransferCharacteristics", 255, {"BT709", 1}, {"Unspecified", 2},
                                           {"Gamma22", 4}, {"Gamma28", 5}, {"SMPTE170M", 6}, {"SMPTE240M", 7},
                                           {"Linear", 8}, {"Log", 9}, {"LogSqrt", 10}, {"IEC61966_2_4", 11},
                                           {"BT1361", 12}, {"SRGB", 13}, {"BT2020_10", 14}, {"BT2020_12", 15},
                                           {"SMPTE2084", 16}, {"SMPTE428", 17}, {"ARIB_STD_B67", 18},
                                           {"Auto", 255}); // default: Auto

                using TypedEnum<TransferCharacteristics>::TypedEnum;

                static const TransferCharacteristics Auto;
                static const TransferCharacteristics Unspecified;
                static const TransferCharacteristics BT709;
                static const TransferCharacteristics Gamma22;
                static const TransferCharacteristics Gamma28;
                static const TransferCharacteristics SMPTE170M;
                static const TransferCharacteristics SMPTE240M;
                static const TransferCharacteristics Linear;
                static const TransferCharacteristics Log;
                static const TransferCharacteristics LogSqrt;
                static const TransferCharacteristics IEC61966_2_4;
                static const TransferCharacteristics BT1361;
                static const TransferCharacteristics SRGB;
                static const TransferCharacteristics BT2020_10;
                static const TransferCharacteristics BT2020_12;
                static const TransferCharacteristics SMPTE2084; ///< PQ (HDR10).
                static const TransferCharacteristics SMPTE428;
                static const TransferCharacteristics ARIB_STD_B67; ///< HLG.
};

inline const TransferCharacteristics TransferCharacteristics::Auto{255};
inline const TransferCharacteristics TransferCharacteristics::Unspecified{2};
inline const TransferCharacteristics TransferCharacteristics::BT709{1};
inline const TransferCharacteristics TransferCharacteristics::Gamma22{4};
inline const TransferCharacteristics TransferCharacteristics::Gamma28{5};
inline const TransferCharacteristics TransferCharacteristics::SMPTE170M{6};
inline const TransferCharacteristics TransferCharacteristics::SMPTE240M{7};
inline const TransferCharacteristics TransferCharacteristics::Linear{8};
inline const TransferCharacteristics TransferCharacteristics::Log{9};
inline const TransferCharacteristics TransferCharacteristics::LogSqrt{10};
inline const TransferCharacteristics TransferCharacteristics::IEC61966_2_4{11};
inline const TransferCharacteristics TransferCharacteristics::BT1361{12};
inline const TransferCharacteristics TransferCharacteristics::SRGB{13};
inline const TransferCharacteristics TransferCharacteristics::BT2020_10{14};
inline const TransferCharacteristics TransferCharacteristics::BT2020_12{15};
inline const TransferCharacteristics TransferCharacteristics::SMPTE2084{16};
inline const TransferCharacteristics TransferCharacteristics::SMPTE428{17};
inline const TransferCharacteristics TransferCharacteristics::ARIB_STD_B67{18};

/**
 * @brief Well-known Enum type for VUI / container matrix coefficients
 *        (luma-chroma derivation from the RGB primaries).
 *
 * Numeric values match ISO/IEC 23091-4 / ITU-T H.273.  @c RGB is used
 * when the bitstream stores RGB natively (e.g. HEVC RGB 4:4:4, AV1
 * subsampling_x=subsampling_y=0 RGB).
 */
class MatrixCoefficients : public TypedEnum<MatrixCoefficients> {
        public:
                // @c Auto (numeric 255) sits outside the 0..11 H.273
                // range.  Encoders resolve @c Auto from the input
                // PixelFormat's ColorModel (RGB models → @c RGB,
                // YCbCr_Rec709 → @c BT709, YCbCr_Rec2020 → @c BT2020_NCL,
                // etc.) at session init time.
                PROMEKI_REGISTER_ENUM_TYPE("MatrixCoefficients", 255, {"RGB", 0}, {"BT709", 1}, {"Unspecified", 2},
                                           {"FCC", 4}, {"BT470BG", 5}, {"SMPTE170M", 6}, {"SMPTE240M", 7}, {"YCgCo", 8},
                                           {"BT2020_NCL", 9}, {"BT2020_CL", 10}, {"SMPTE2085", 11},
                                           {"Auto", 255}); // default: Auto

                using TypedEnum<MatrixCoefficients>::TypedEnum;

                static const MatrixCoefficients Auto;
                static const MatrixCoefficients Unspecified;
                static const MatrixCoefficients RGB;
                static const MatrixCoefficients BT709;
                static const MatrixCoefficients FCC;
                static const MatrixCoefficients BT470BG;
                static const MatrixCoefficients SMPTE170M;
                static const MatrixCoefficients SMPTE240M;
                static const MatrixCoefficients YCgCo;
                static const MatrixCoefficients BT2020_NCL;
                static const MatrixCoefficients BT2020_CL;
                static const MatrixCoefficients SMPTE2085;
};

inline const MatrixCoefficients MatrixCoefficients::Auto{255};
inline const MatrixCoefficients MatrixCoefficients::Unspecified{2};
inline const MatrixCoefficients MatrixCoefficients::RGB{0};
inline const MatrixCoefficients MatrixCoefficients::BT709{1};
inline const MatrixCoefficients MatrixCoefficients::FCC{4};
inline const MatrixCoefficients MatrixCoefficients::BT470BG{5};
inline const MatrixCoefficients MatrixCoefficients::SMPTE170M{6};
inline const MatrixCoefficients MatrixCoefficients::SMPTE240M{7};
inline const MatrixCoefficients MatrixCoefficients::YCgCo{8};
inline const MatrixCoefficients MatrixCoefficients::BT2020_NCL{9};
inline const MatrixCoefficients MatrixCoefficients::BT2020_CL{10};
inline const MatrixCoefficients MatrixCoefficients::SMPTE2085{11};

/**
 * @brief Well-known Enum type for @ref NullPacingMediaIO pacing strategy.
 *
 * Selects how the null-pacing sink times its frame consumption.
 * Used as the value of @ref MediaConfig::NullPacingMode.
 *
 * - @c Wallclock — emit one frame per @c 1/TargetFps wall-clock
 *                  interval; frames arriving inside an active interval
 *                  are dropped (counted in @ref MediaIOStats::FramesDropped)
 *                  rather than queued.  This is the default and the
 *                  mode used by the demo "fake playback device".
 * - @c Free      — drain every incoming frame at the upstream's
 *                  natural rate.  Useful as a passthrough sink for
 *                  measuring the upstream stage in isolation.
 */
class NullPacingMode : public TypedEnum<NullPacingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("NullPacingMode", 0, {"Wallclock", 0}, {"Free", 1});

                using TypedEnum<NullPacingMode>::TypedEnum;

                static const NullPacingMode Wallclock;
                static const NullPacingMode Free;
};

inline const NullPacingMode NullPacingMode::Wallclock{0};
inline const NullPacingMode NullPacingMode::Free{1};

/**
 * @brief Well-known Enum type for the role a single audio channel plays.
 *
 * Each entry names a position in a multi-channel layout — the
 * "what does channel N carry?" answer that lets routers, downmixers,
 * and meters speak about specific channels independently of their
 * physical index.  Roles are deliberately pre-coordinated with the
 * standard SMPTE / Dolby / DTS / WAVEFORMATEXTENSIBLE naming so an
 * @ref AudioChannelMap can interoperate with foreign tooling.
 *
 * The integer values are the library's own and are @b not the
 * WAVEFORMATEXTENSIBLE @c dwChannelMask bit positions; map between
 * them at the wire-format boundary if needed.
 *
 *  - @c Unused    — reserved / unassigned channel.  Used as the
 *                   default for newly constructed maps so callers
 *                   must opt in to declaring a role.
 *  - @c Mono      — single-channel program audio (1.0 layout).
 *  - @c FrontLeft / @c FrontRight — front-pair stereo (2.0 / L/R).
 *  - @c FrontCenter — center channel (3.0+ layouts).
 *  - @c LFE       — low-frequency effects ("subwoofer").
 *  - @c BackLeft / @c BackRight — surround pair behind the listener
 *                   (5.1 SMPTE order, 7.1 rears).
 *  - @c BackCenter — single rear surround (6.1).
 *  - @c SideLeft / @c SideRight — side surround pair (7.1 sides).
 *  - @c FrontLeftOfCenter / @c FrontRightOfCenter — wide front pair
 *                   used in some film mixes between the L/R and center.
 *  - @c TopFrontLeft / @c TopFrontRight / @c TopFrontCenter
 *                  — overhead front (Atmos / Auro-3D heights).
 *  - @c TopBackLeft / @c TopBackRight / @c TopBackCenter
 *                  — overhead rear.
 *  - @c TopCenter — single top-of-room speaker (less common).
 *  - @c AmbisonicW / @c AmbisonicX / @c AmbisonicY / @c AmbisonicZ
 *                  — first-order ambisonic (FOA) channels in ACN
 *                   order: W is the omnidirectional component, XYZ
 *                   are the first-order spherical harmonics.
 *  - @c Aux0 … @c Aux7 — generic auxiliary channels for
 *                   non-positional or program-specific data
 *                   (commentary, descriptive audio, embedded SMPTE
 *                   337M data carriers, …).
 *
 * Default value is @c Unused.
 */
class ChannelRole : public TypedEnum<ChannelRole> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("ChannelRole", 0, {"Unused", 0}, {"Mono", 1}, {"FrontLeft", 2},
                                           {"FrontRight", 3}, {"FrontCenter", 4}, {"LFE", 5}, {"BackLeft", 6},
                                           {"BackRight", 7}, {"BackCenter", 8}, {"SideLeft", 9}, {"SideRight", 10},
                                           {"FrontLeftOfCenter", 11}, {"FrontRightOfCenter", 12}, {"TopFrontLeft", 13},
                                           {"TopFrontCenter", 14}, {"TopFrontRight", 15}, {"TopBackLeft", 16},
                                           {"TopBackCenter", 17}, {"TopBackRight", 18}, {"TopCenter", 19},
                                           {"AmbisonicW", 20}, {"AmbisonicX", 21}, {"AmbisonicY", 22},
                                           {"AmbisonicZ", 23}, {"Aux0", 24}, {"Aux1", 25}, {"Aux2", 26}, {"Aux3", 27},
                                           {"Aux4", 28}, {"Aux5", 29}, {"Aux6", 30}, {"Aux7", 31});

                using TypedEnum<ChannelRole>::TypedEnum;

                static const ChannelRole Unused;
                static const ChannelRole Mono;
                static const ChannelRole FrontLeft;
                static const ChannelRole FrontRight;
                static const ChannelRole FrontCenter;
                static const ChannelRole LFE;
                static const ChannelRole BackLeft;
                static const ChannelRole BackRight;
                static const ChannelRole BackCenter;
                static const ChannelRole SideLeft;
                static const ChannelRole SideRight;
                static const ChannelRole FrontLeftOfCenter;
                static const ChannelRole FrontRightOfCenter;
                static const ChannelRole TopFrontLeft;
                static const ChannelRole TopFrontCenter;
                static const ChannelRole TopFrontRight;
                static const ChannelRole TopBackLeft;
                static const ChannelRole TopBackCenter;
                static const ChannelRole TopBackRight;
                static const ChannelRole TopCenter;
                static const ChannelRole AmbisonicW;
                static const ChannelRole AmbisonicX;
                static const ChannelRole AmbisonicY;
                static const ChannelRole AmbisonicZ;
                static const ChannelRole Aux0;
                static const ChannelRole Aux1;
                static const ChannelRole Aux2;
                static const ChannelRole Aux3;
                static const ChannelRole Aux4;
                static const ChannelRole Aux5;
                static const ChannelRole Aux6;
                static const ChannelRole Aux7;
};

inline const ChannelRole ChannelRole::Unused{0};
inline const ChannelRole ChannelRole::Mono{1};
inline const ChannelRole ChannelRole::FrontLeft{2};
inline const ChannelRole ChannelRole::FrontRight{3};
inline const ChannelRole ChannelRole::FrontCenter{4};
inline const ChannelRole ChannelRole::LFE{5};
inline const ChannelRole ChannelRole::BackLeft{6};
inline const ChannelRole ChannelRole::BackRight{7};
inline const ChannelRole ChannelRole::BackCenter{8};
inline const ChannelRole ChannelRole::SideLeft{9};
inline const ChannelRole ChannelRole::SideRight{10};
inline const ChannelRole ChannelRole::FrontLeftOfCenter{11};
inline const ChannelRole ChannelRole::FrontRightOfCenter{12};
inline const ChannelRole ChannelRole::TopFrontLeft{13};
inline const ChannelRole ChannelRole::TopFrontCenter{14};
inline const ChannelRole ChannelRole::TopFrontRight{15};
inline const ChannelRole ChannelRole::TopBackLeft{16};
inline const ChannelRole ChannelRole::TopBackCenter{17};
inline const ChannelRole ChannelRole::TopBackRight{18};
inline const ChannelRole ChannelRole::TopCenter{19};
inline const ChannelRole ChannelRole::AmbisonicW{20};
inline const ChannelRole ChannelRole::AmbisonicX{21};
inline const ChannelRole ChannelRole::AmbisonicY{22};
inline const ChannelRole ChannelRole::AmbisonicZ{23};
inline const ChannelRole ChannelRole::Aux0{24};
inline const ChannelRole ChannelRole::Aux1{25};
inline const ChannelRole ChannelRole::Aux2{26};
inline const ChannelRole ChannelRole::Aux3{27};
inline const ChannelRole ChannelRole::Aux4{28};
inline const ChannelRole ChannelRole::Aux5{29};
inline const ChannelRole ChannelRole::Aux6{30};
inline const ChannelRole ChannelRole::Aux7{31};

/**
 * @brief Well-known Enum type for the NDI receiver bandwidth tier.
 *
 * Selects how much bandwidth an `NdiMediaIO` source asks the SDK
 * for at @c NDIlib_recv_create_v3 time.  Drives the
 * @ref MediaConfig::NdiBandwidth config key.  The integer values
 * are libpromeki-internal — the backend translates them to the
 * matching @c NDIlib_recv_bandwidth_e value when calling the SDK.
 *
 * - @c Highest        — full-quality video + audio + metadata.
 * - @c Lowest         — preview-quality video, full audio + metadata.
 * - @c AudioOnly      — audio + metadata, no video frames.
 * - @c MetadataOnly   — metadata only (PTZ / tally / KVM control).
 */
class NdiBandwidth : public TypedEnum<NdiBandwidth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("NdiBandwidth", 0, {"Highest", 0}, {"Lowest", 1}, {"AudioOnly", 2},
                                           {"MetadataOnly", 3}); // default: Highest

                using TypedEnum<NdiBandwidth>::TypedEnum;

                static const NdiBandwidth Highest;
                static const NdiBandwidth Lowest;
                static const NdiBandwidth AudioOnly;
                static const NdiBandwidth MetadataOnly;
};

inline const NdiBandwidth NdiBandwidth::Highest{0};
inline const NdiBandwidth NdiBandwidth::Lowest{1};
inline const NdiBandwidth NdiBandwidth::AudioOnly{2};
inline const NdiBandwidth NdiBandwidth::MetadataOnly{3};

/**
 * @brief Well-known Enum type for the NDI receiver color-format hint.
 *
 * Maps to `NDIlib_recv_color_format_e`.  Drives the
 * @ref MediaConfig::NdiColorFormat config key, controlling what
 * FourCC family the SDK delivers for a given source.
 *
 * - @c Best       — keep the source's native FourCC where possible.
 *                   Right choice for high-bit-depth pipelines (P216
 *                   stays P216, etc.) — the SDK won't quietly
 *                   down-convert.  Note: the Advanced SDK delivers
 *                   PA16 (4:2:2:4 16-bit planar+alpha) under this
 *                   mode which libpromeki does not yet decode.
 * - @c Fastest    — (**default**) minimize the SDK's per-frame work.
 *                   Returns the format on the wire (UYVY for 8-bit,
 *                   P216 for 10/12/16-bit) — both are handled by
 *                   the capture loop.  Avoids the PA16 delivery
 *                   that @c Best produces with the Advanced SDK.
 * - @c UyvyBgra   — 8-bit YUV 4:2:2 (UYVY) for opaque frames,
 *                   BGRA for sources that carry alpha.
 * - @c UyvyRgba   — same as above with RGBA instead of BGRA.
 * - @c BgrxBgra   — BGRX for opaque, BGRA for alpha.
 * - @c RgbxRgba   — RGBX for opaque, RGBA for alpha.
 */
class NdiColorFormat : public TypedEnum<NdiColorFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("NdiColorFormat", 0, {"Best", 0}, {"Fastest", 1}, {"UyvyBgra", 2},
                                           {"UyvyRgba", 3}, {"BgrxBgra", 4},
                                           {"RgbxRgba", 5}); // default: Fastest (see MediaConfig::NdiColorFormat)

                using TypedEnum<NdiColorFormat>::TypedEnum;

                static const NdiColorFormat Best;
                static const NdiColorFormat Fastest;
                static const NdiColorFormat UyvyBgra;
                static const NdiColorFormat UyvyRgba;
                static const NdiColorFormat BgrxBgra;
                static const NdiColorFormat RgbxRgba;
};

inline const NdiColorFormat NdiColorFormat::Best{0};
inline const NdiColorFormat NdiColorFormat::Fastest{1};
inline const NdiColorFormat NdiColorFormat::UyvyBgra{2};
inline const NdiColorFormat NdiColorFormat::UyvyRgba{3};
inline const NdiColorFormat NdiColorFormat::BgrxBgra{4};
inline const NdiColorFormat NdiColorFormat::RgbxRgba{5};

/**
 * @brief Well-known Enum type for the NDI receive-side bit-depth tag.
 *
 * NDI's P216 wire format always carries 16-bit-container 4:2:2 data,
 * but the actual semantic precision (10 / 12 / 16) is *not*
 * signalled by the FourCC.  This enum lets a caller who knows the
 * upstream source's true precision ask the receiver to tag emitted
 * frames with a narrower promeki PixelFormat::ID, avoiding a
 * downstream 16→10 conversion.  Drives the
 * @ref MediaConfig::NdiReceiveBitDepth config key.
 *
 * - @c Auto    — receiver tags frames as 16-bit (precision-honest).
 *                Default.
 * - @c Bits10  — tag P216 frames as
 *                @c YUV10_422_SemiPlanar_LE_Rec709.  Caller-side
 *                promise: the upstream is producing 10-bit content.
 * - @c Bits12  — same, for 12-bit content
 *                (@c YUV12_422_SemiPlanar_LE_Rec709).
 * - @c Bits16  — explicit form of @c Auto for callers that want to
 *                document the choice.
 */
class NdiReceiveBitDepth : public TypedEnum<NdiReceiveBitDepth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("NdiReceiveBitDepth", 0, {"Auto", 0}, {"Bits10", 10}, {"Bits12", 12},
                                           {"Bits16", 16}); // default: Auto

                using TypedEnum<NdiReceiveBitDepth>::TypedEnum;

                static const NdiReceiveBitDepth Auto;
                static const NdiReceiveBitDepth Bits10;
                static const NdiReceiveBitDepth Bits12;
                static const NdiReceiveBitDepth Bits16;
};

inline const NdiReceiveBitDepth NdiReceiveBitDepth::Auto{0};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits10{10};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits12{12};
inline const NdiReceiveBitDepth NdiReceiveBitDepth::Bits16{16};

/** @} */

PROMEKI_NAMESPACE_END

/**
 * @brief Hash specialization for @ref promeki::ChannelRole.
 *
 * The role's compile-time-fixed @c Type means the integer value
 * uniquely identifies the role within its space, so hashing the
 * value directly is sufficient and well-distributed for typical
 * role tables (small, dense integer ranges).
 */
template <> struct std::hash<promeki::ChannelRole> {
                size_t operator()(const promeki::ChannelRole &v) const noexcept { return std::hash<int>()(v.value()); }
};
