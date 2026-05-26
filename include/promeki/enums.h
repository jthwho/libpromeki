/**
 * @file      enums.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
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
 * @brief Well-known Enum type for HDR tone-mapping policy on CSC pipelines.
 *
 * Used as the value type for the @ref MediaConfig::CscToneMapping
 * config key.  @c Auto enables ITU-R BT.2390 perceptual tone-mapping
 * automatically whenever the pipeline crosses an HDR boundary
 * (source is PQ / HLG, destination is SDR) — the default since
 * SDR clipping is rarely the desired behaviour for HDR-to-SDR
 * conversions.  @c Enabled forces tone-mapping on regardless of
 * the colorimetry of either end (callers who know they need
 * compression even between two HDR targets with mismatched peak
 * luminance).  @c Disabled bypasses tone-mapping entirely — the
 * pipeline lets the existing transfer / gamut chain produce
 * whatever clipping naturally falls out.
 */
class CscToneMapping : public TypedEnum<CscToneMapping> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("CscToneMapping", 0, {"Auto", 0}, {"Enabled", 1}, {"Disabled", 2});

                using TypedEnum<CscToneMapping>::TypedEnum;

                static const CscToneMapping Auto;
                static const CscToneMapping Enabled;
                static const CscToneMapping Disabled;
};

inline const CscToneMapping CscToneMapping::Auto{0};
inline const CscToneMapping CscToneMapping::Enabled{1};
inline const CscToneMapping CscToneMapping::Disabled{2};

/**
 * @brief Well-known Enum type for HDR tone-mapping operator selection.
 *
 * Used as the value type for the @ref MediaConfig::CscToneMapOperator
 * config key.  The enum reserves slots for all the operators the
 * library plans to support so callers can pin a choice today and the
 * runtime picks the matching kernel when it lands.  Until the kernel
 * for a given operator is registered, the pipeline falls back to
 * @ref Bt2390 with a one-shot warning.
 *
 * - @c Bt2390  — ITU-R BT.2390-9 Annex B.2.5 EETF.  Per-channel Hermite
 *               spline in PQ-encoded space.  Broadcast / display
 *               standard, simple and well-behaved.  Default.
 * - @c Reinhard — `L / (1 + L)` operator in linear scene-referred
 *               space.  Cheap, perceptually reasonable for typical
 *               content; popular in games.  Not yet implemented.
 * - @c Hable    — Uncharted 2 filmic curve in linear space (shoulder
 *               + toe).  Popular for game / cinematic content.  Not
 *               yet implemented.
 * - @c Aces     — Academy Color Encoding System RRT+ODT in linear
 *               space.  Film-industry standard; usually shipped via
 *               Stephen Hill's polynomial fit.  Not yet implemented.
 * - @c Bt2446a  — ITU-R BT.2446 Method A — broadcast HDR-to-HDR
 *               peak-luminance compression.  Not yet implemented.
 */
class CscToneMapOperator : public TypedEnum<CscToneMapOperator> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("CscToneMapOperator", 0, {"Bt2390", 0}, {"Reinhard", 1}, {"Hable", 2},
                                           {"Aces", 3}, {"Bt2446a", 4});

                using TypedEnum<CscToneMapOperator>::TypedEnum;

                static const CscToneMapOperator Bt2390;
                static const CscToneMapOperator Reinhard;
                static const CscToneMapOperator Hable;
                static const CscToneMapOperator Aces;
                static const CscToneMapOperator Bt2446a;
};

inline const CscToneMapOperator CscToneMapOperator::Bt2390{0};
inline const CscToneMapOperator CscToneMapOperator::Reinhard{1};
inline const CscToneMapOperator CscToneMapOperator::Hable{2};
inline const CscToneMapOperator CscToneMapOperator::Aces{3};
inline const CscToneMapOperator CscToneMapOperator::Bt2446a{4};

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
 * - @c Userspace — pace by sleeping between sends (the per-stream
 *                 TX thread + @c Cadence helper).  Works
 *                 everywhere without kernel configuration but ties
 *                 up the TX thread during the pacing window.
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
 * @brief Well-known Enum type for the SDP @c ts-refclk source.
 *
 * Selects which clock-reference attribute the writer emits in its
 * SDP and seeds onto every active stream, per RFC 7273 / SMPTE
 * ST 2110-10:
 *
 * - @c Auto     — emit @c localmac for the autodetected primary
 *                 interface MAC; if @ref MediaConfig::RtpPtpGrandmaster
 *                 is non-null, upgrade to @c Ptp automatically.  This
 *                 is the default.
 * - @c LocalMac — force @c ts-refclk:localmac=&lt;mac&gt;; use
 *                 @ref MediaConfig::RtpRefClockLocalMac to override
 *                 the autodetected MAC.
 * - @c Ptp      — emit
 *                 @c ts-refclk:ptp=&lt;profile&gt;:&lt;gmid&gt;:&lt;domain&gt;
 *                 from @ref MediaConfig::RtpPtpProfile,
 *                 @ref MediaConfig::RtpPtpGrandmaster, and
 *                 @ref MediaConfig::RtpPtpDomain.  Required for
 *                 SMPTE ST 2110 deployments.
 * - @c None     — suppress @c ts-refclk emission; receivers fall
 *                 back to "trust the SR pair" tracking.
 */
class RtpRefClockMode : public TypedEnum<RtpRefClockMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtpRefClockMode", 0, {"Auto", 0}, {"LocalMac", 1},
                                           {"Ptp", 2}, {"None", 3}); // default: Auto

                using TypedEnum<RtpRefClockMode>::TypedEnum;

                static const RtpRefClockMode Auto;
                static const RtpRefClockMode LocalMac;
                static const RtpRefClockMode Ptp;
                static const RtpRefClockMode None;
};

inline const RtpRefClockMode RtpRefClockMode::Auto{0};
inline const RtpRefClockMode RtpRefClockMode::LocalMac{1};
inline const RtpRefClockMode RtpRefClockMode::Ptp{2};
inline const RtpRefClockMode RtpRefClockMode::None{3};

/**
 * @brief Well-known Enum type for the RFC 7273 @c mediaclk SDP attribute.
 *
 * RFC 7273 §5 distinguishes two ways a sender signals how its media
 * clock relates to the reference clock identified by @c ts-refclk:
 *
 * - @c mediaclk:direct=&lt;offset&gt; — the media clock is locked to
 *   the reference clock with a fixed RTP-timestamp offset.  A
 *   receiver can compute the wallclock instant of every sample from
 *   the on-wire RTP-TS and the SDP offset alone.  This is the
 *   default for synchronous capture paths (SDI ingest, AES67 audio
 *   capture, anywhere the wire format is sample-locked to the
 *   reference grid).
 *
 * - @c mediaclk:sender — the media clock is asynchronous to the
 *   reference clock.  The receiver must use RTCP Sender Reports to
 *   recover the sender's clock; the @c ts-refclk identifies the
 *   sender's reference frame but does not anchor the media clock to
 *   it.  Right for sources whose framing rate floats relative to
 *   PTP (free-running encoders, network-fed transcoders, anything
 *   where the source clock is not disciplined to the same PTP grid
 *   the wire-side advertises).
 *
 * Drives the @ref MediaConfig::RtpVideoMediaClkMode /
 * @ref MediaConfig::RtpAudioMediaClkMode /
 * @ref MediaConfig::RtpDataMediaClkMode config keys and the
 * @c RtpMediaIO::buildSdp emission path.
 *
 * @par Modes
 *  - @c Auto — pick based on the stream's ts-refclk decision.  When
 *               a reference clock is advertised (Ptp or LocalMac),
 *               emit @c mediaclk:direct=&lt;offset&gt;; with @c None
 *               omit the @c mediaclk attribute entirely.  This is
 *               the default and matches today's behaviour.
 *  - @c Direct — force @c mediaclk:direct=&lt;offset&gt; emission.
 *                Offset comes from the per-stream
 *                @ref RtpMediaClock::mediaClkDirectOffset (0 for a
 *                natural PTP anchor) with the @c
 *                MediaConfig::RtpMediaClkOffset legacy override.
 *  - @c Sender — emit bare @c mediaclk:sender (no parameters).  Use
 *                for sources whose media clock is asynchronous to
 *                the reference clock.
 */
class RtpMediaClkMode : public TypedEnum<RtpMediaClkMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtpMediaClkMode", 0, {"Auto", 0}, {"Direct", 1},
                                           {"Sender", 2}); // default: Auto

                using TypedEnum<RtpMediaClkMode>::TypedEnum;

                static const RtpMediaClkMode Auto;
                static const RtpMediaClkMode Direct;
                static const RtpMediaClkMode Sender;
};

inline const RtpMediaClkMode RtpMediaClkMode::Auto{0};
inline const RtpMediaClkMode RtpMediaClkMode::Direct{1};
inline const RtpMediaClkMode RtpMediaClkMode::Sender{2};

/**
 * @brief Well-known Enum type for the SMPTE ST 2110-21 sender type.
 *
 * ST 2110-21:2022 §7.1 defines three sender shapes that differ in
 * how aggressively packets are spread across the active portion of
 * the frame interval:
 *
 * - @c TypeN — Narrow, gapped.  Packets land only inside the
 *   active line interval (R_ACTIVE × T_FRAME); inter-packet gap
 *   carries no traffic.  Tightest VRX bound (worst-case
 *   1500×8 / MAXUDP bytes) and lowest receiver-side jitter
 *   budget.  Requires hardware-grade pacing (NIC TXTIME with
 *   sub-µs precision).
 * - @c TypeNL — Narrow, linear.  Same VRX_FULL / CMAX bounds as
 *   Type N but packets are spread linearly across the entire
 *   frame interval (T_RS_l = T_FRAME / N_PACKETS).  Achievable
 *   on stock Linux with SO_TXTIME + a real-time-scheduled
 *   userspace TX thread.
 * - @c TypeW — Wide, linear.  Looser VRX bound
 *   (1500×720 / MAXUDP) and CMAX floor of 16 — accommodates
 *   stock kernel fair-queue pacing without TXTIME.  Default for
 *   any sender that doesn't claim narrow.
 *
 * Plus two policy values:
 *
 * - @c Auto — derive from the bound scheduler / pacing mode at
 *   open time.  @c RtpPacingMode::KernelFq / @c Userspace map
 *   to @c TypeW; @c TxTime maps to @c TypeNL; @c None maps to
 *   @c Unknown.
 * - @c Unknown — the sender cannot honestly claim a type.
 *   Suppresses the @c TP fmtp emission so receivers fall back to
 *   "treat as Type A" (RFC 4175 §B / ST 2110-21 §7.2).
 *
 * Drives @ref MediaConfig::RtpVideoSenderType /
 * @ref MediaConfig::RtpAudioSenderType /
 * @ref MediaConfig::RtpDataSenderType and the @c TP fmtp emission
 * in @c RtpMediaIO::buildSdp.
 */
class RtpSenderType : public TypedEnum<RtpSenderType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtpSenderType", 0, {"Auto", 0}, {"Unknown", 1},
                                           {"TypeN", 2}, {"TypeNL", 3},
                                           {"TypeW", 4}); // default: Auto

                using TypedEnum<RtpSenderType>::TypedEnum;

                static const RtpSenderType Auto;
                static const RtpSenderType Unknown;
                static const RtpSenderType TypeN;
                static const RtpSenderType TypeNL;
                static const RtpSenderType TypeW;
};

inline const RtpSenderType RtpSenderType::Auto{0};
inline const RtpSenderType RtpSenderType::Unknown{1};
inline const RtpSenderType RtpSenderType::TypeN{2};
inline const RtpSenderType RtpSenderType::TypeNL{3};
inline const RtpSenderType RtpSenderType::TypeW{4};

/**
 * @brief Well-known Enum type for RFC 9134 @c packetmode (K bit).
 *
 * RFC 9134 §4.3 packet-header K bit:
 *  - @c Codestream (K=0) — the codestream is split into MTU-sized
 *    fragments without regard to slice / header boundaries.
 *    Simplest sender; receivers must reassemble before decode.
 *    The library's default.
 *  - @c Slice (K=1) — each RTP packet carries one or more
 *    @em complete JPEG XS slices, never crossing a slice
 *    boundary.  Enables ultra-low-latency receivers to start
 *    decoding before the entire frame has arrived; requires the
 *    sender to walk the codestream's SLH markers and group
 *    slices into MTU-sized packets.
 */
class JxsPacketMode : public TypedEnum<JxsPacketMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("JxsPacketMode", 0, {"Codestream", 0},
                                           {"Slice", 1}); // default: Codestream

                using TypedEnum<JxsPacketMode>::TypedEnum;

                static const JxsPacketMode Codestream;
                static const JxsPacketMode Slice;
};

inline const JxsPacketMode JxsPacketMode::Codestream{0};
inline const JxsPacketMode JxsPacketMode::Slice{1};

/**
 * @brief Well-known Enum type for RFC 9134 @c transmode (T bit).
 *
 * RFC 9134 §4.3 packet-header T bit:
 *  - @c OutOfOrderAllowed (T=0) — the sender emits packets in
 *    codestream order but receivers may reorder before decode.
 *    Useful with reorder buffers that can absorb network
 *    permutations.
 *  - @c SequentialOnly (T=1) — packets MUST arrive in sequence
 *    for decode to succeed.  The default the RFC mandates when
 *    the parameter is absent from the fmtp.
 */
class JxsTransMode : public TypedEnum<JxsTransMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("JxsTransMode", 1, {"OutOfOrderAllowed", 0},
                                           {"SequentialOnly", 1}); // default: SequentialOnly

                using TypedEnum<JxsTransMode>::TypedEnum;

                static const JxsTransMode OutOfOrderAllowed;
                static const JxsTransMode SequentialOnly;
};

inline const JxsTransMode JxsTransMode::OutOfOrderAllowed{0};
inline const JxsTransMode JxsTransMode::SequentialOnly{1};

/**
 * @brief Well-known Enum type for the JPEG XS profile (ISO 21122-2).
 *
 * Each value's CamelCase identifier maps to a canonical SDP
 * @c profile= wire token after RFC 9134 §7.1's "any white space
 * Unicode character in the profile name SHALL be omitted" rule;
 * the wire mapping lives in @c imagedesc.cpp 's
 * @c jxsProfileToFmtp / @c jxsProfileFromFmtp helpers.  Example:
 * @c Main422_10 → @c "Main422.10".
 *
 * @c Unspecified (the default) suppresses @c profile= emission.
 */
class JxsProfile : public TypedEnum<JxsProfile> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("JxsProfile", 0, {"Unspecified", 0},
                                           {"Light422_10", 1}, {"Light444_12", 2},
                                           {"LightSubline422_10", 3}, {"Main422_10", 4},
                                           {"Main444_12", 5}, {"Main4444_12", 6},
                                           {"High444_12", 7}, {"High4444_12", 8},
                                           {"Tdc422_10", 9});

                using TypedEnum<JxsProfile>::TypedEnum;

                static const JxsProfile Unspecified;
                static const JxsProfile Light422_10;
                static const JxsProfile Light444_12;
                static const JxsProfile LightSubline422_10;
                static const JxsProfile Main422_10;
                static const JxsProfile Main444_12;
                static const JxsProfile Main4444_12;
                static const JxsProfile High444_12;
                static const JxsProfile High4444_12;
                static const JxsProfile Tdc422_10;
};

inline const JxsProfile JxsProfile::Unspecified{0};
inline const JxsProfile JxsProfile::Light422_10{1};
inline const JxsProfile JxsProfile::Light444_12{2};
inline const JxsProfile JxsProfile::LightSubline422_10{3};
inline const JxsProfile JxsProfile::Main422_10{4};
inline const JxsProfile JxsProfile::Main444_12{5};
inline const JxsProfile JxsProfile::Main4444_12{6};
inline const JxsProfile JxsProfile::High444_12{7};
inline const JxsProfile JxsProfile::High4444_12{8};
inline const JxsProfile JxsProfile::Tdc422_10{9};

/**
 * @brief Well-known Enum type for the JPEG XS level (ISO 21122-2).
 *
 * Each value's identifier maps to a canonical SDP @c level=
 * wire token (e.g. @c Lvl4k_2 → @c "4k-2") in @c imagedesc.cpp.
 * @c Unspecified suppresses @c level= emission.
 */
class JxsLevel : public TypedEnum<JxsLevel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("JxsLevel", 0, {"Unspecified", 0}, {"Lvl1k_1", 1},
                                           {"Lvl2k_1", 2}, {"Lvl4k_1", 3}, {"Lvl4k_2", 4},
                                           {"Lvl4k_3", 5}, {"Lvl8k_1", 6}, {"Lvl8k_2", 7},
                                           {"Lvl8k_3", 8}, {"Lvl10k_1", 9});

                using TypedEnum<JxsLevel>::TypedEnum;

                static const JxsLevel Unspecified;
                static const JxsLevel Lvl1k_1;
                static const JxsLevel Lvl2k_1;
                static const JxsLevel Lvl4k_1;
                static const JxsLevel Lvl4k_2;
                static const JxsLevel Lvl4k_3;
                static const JxsLevel Lvl8k_1;
                static const JxsLevel Lvl8k_2;
                static const JxsLevel Lvl8k_3;
                static const JxsLevel Lvl10k_1;
};

inline const JxsLevel JxsLevel::Unspecified{0};
inline const JxsLevel JxsLevel::Lvl1k_1{1};
inline const JxsLevel JxsLevel::Lvl2k_1{2};
inline const JxsLevel JxsLevel::Lvl4k_1{3};
inline const JxsLevel JxsLevel::Lvl4k_2{4};
inline const JxsLevel JxsLevel::Lvl4k_3{5};
inline const JxsLevel JxsLevel::Lvl8k_1{6};
inline const JxsLevel JxsLevel::Lvl8k_2{7};
inline const JxsLevel JxsLevel::Lvl8k_3{8};
inline const JxsLevel JxsLevel::Lvl10k_1{9};

/**
 * @brief Well-known Enum type for the JPEG XS sublevel (ISO 21122-2).
 *
 * Each value's identifier maps to a canonical SDP @c sublevel=
 * wire token (e.g. @c Sublev3bpp → @c "Sublev3bpp") in
 * @c imagedesc.cpp.  @c Unspecified suppresses @c sublevel=
 * emission.
 */
class JxsSublevel : public TypedEnum<JxsSublevel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("JxsSublevel", 0, {"Unspecified", 0}, {"Full", 1},
                                           {"Sublev3bpp", 2}, {"Sublev6bpp", 3},
                                           {"Sublev9bpp", 4}, {"Sublev12bpp", 5});

                using TypedEnum<JxsSublevel>::TypedEnum;

                static const JxsSublevel Unspecified;
                static const JxsSublevel Full;
                static const JxsSublevel Sublev3bpp;
                static const JxsSublevel Sublev6bpp;
                static const JxsSublevel Sublev9bpp;
                static const JxsSublevel Sublev12bpp;
};

inline const JxsSublevel JxsSublevel::Unspecified{0};
inline const JxsSublevel JxsSublevel::Full{1};
inline const JxsSublevel JxsSublevel::Sublev3bpp{2};
inline const JxsSublevel JxsSublevel::Sublev6bpp{3};
inline const JxsSublevel JxsSublevel::Sublev9bpp{4};
inline const JxsSublevel JxsSublevel::Sublev12bpp{5};

/**
 * @brief Well-known Enum type for the SMPTE ST 2110-10 @c TSMODE SDP fmtp parameter.
 *
 * ST 2110-10 §7.9 / §8.7 describe how a sender labels its RTP
 * timestamps so a downstream receiver knows whether to align essences
 * by @c (NTP, RTP-TS) anchor pairs, by sample instant, or to treat the
 * stamp as freshly minted.
 *
 * - @c Samp — RTP-TS reflects the original sample instant; the
 *             sender passed @ref Frame::captureTime through unmodified.
 * - @c New  — RTP-TS was created anew at egress (synthetic generators
 *             such as TPG, videogen).
 * - @c Pres — RTP-TS was preserved from input that did not signal
 *             @c SAMP (CSC / mixer / receive-process-send devices).
 */
class RtpTsMode : public TypedEnum<RtpTsMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtpTsMode", 0, {"Samp", 0}, {"New", 1},
                                           {"Pres", 2}); // default: Samp

                using TypedEnum<RtpTsMode>::TypedEnum;

                static const RtpTsMode Samp;
                static const RtpTsMode New;
                static const RtpTsMode Pres;
};

inline const RtpTsMode RtpTsMode::Samp{0};
inline const RtpTsMode RtpTsMode::New{1};
inline const RtpTsMode RtpTsMode::Pres{2};

/**
 * @brief Well-known Enum type for the AES67 / ST 2110-30 PCM wire format.
 *
 * Selects the on-wire RTP encoding for audio streams emitted by
 * @ref MediaConfig::AudioRtpDestination.  Drives the @ref MediaConfig::
 * RtpAudioWireFormat key.  The on-wire encoding name (@c L16 / @c L24)
 * is what flows into the SDP @c rtpmap attribute (RFC 3551 §4.5.11 /
 * RFC 3190 §4); the storage format used by the per-stream packetizer
 * FIFO is the matching big-endian PCM format
 * (@c PCMI_S16BE / @c PCMI_S24BE).
 *
 * - @c Auto — pick L24 when the upstream @c AudioDesc carries
 *             24-bit samples, otherwise L16.  Matches the AES67 §7.1
 *             "L16 for 44.1 / 48 kHz, L24 for 96 kHz" guidance for
 *             pipelines that do not pin the wire format explicitly.
 * - @c L16  — 16-bit linear, big-endian (RFC 3551 §4.5.11).
 *             Required by AES67 §7.1 at 44.1 kHz; the only choice for
 *             ST 2110-30 Level A senders.
 * - @c L24  — 24-bit linear, big-endian (RFC 3190 §4).  Required by
 *             AES67 §7.1 at 96 kHz; the canonical choice for
 *             ST 2110-30 Level AX / BX / CX senders.
 */
class AudioWireFormat : public TypedEnum<AudioWireFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioWireFormat", 0,
                                           {"Auto", 0}, {"L16", 1},
                                           {"L24", 2}); // default: Auto

                using TypedEnum<AudioWireFormat>::TypedEnum;

                static const AudioWireFormat Auto;
                static const AudioWireFormat L16;
                static const AudioWireFormat L24;
};

inline const AudioWireFormat AudioWireFormat::Auto{0};
inline const AudioWireFormat AudioWireFormat::L16{1};
inline const AudioWireFormat AudioWireFormat::L24{2};

/**
 * @brief Well-known Enum type for the ST 2110-30 §7 conformance levels.
 *
 * ST 2110-30:2025 §7 (Tables 2 and 3) define six sender conformance
 * levels by (sample rate, packet time, channel count) tuples.  A
 * sender's claimed level is computed from its configured stream
 * shape and surfaced via @ref RtpMediaIO::StatsAudioConformanceLevel
 * so monitoring code can verify that an interconnected receiver's
 * declared level is compatible.
 *
 * - @c None — the configured (rate, ptime, channels) combination is
 *             outside every Table 2 row.  The stream still functions
 *             but cannot be advertised as ST 2110-30 conformant.
 * - @c A    — 48 kHz / 1 ms / 1-8 channels.  Mandatory baseline for
 *             every ST 2110-30 sender and receiver.
 * - @c AX   — 96 kHz / 1 ms / 1-4 channels.
 * - @c B    — 48 kHz / 125 µs / 1-8 channels.
 * - @c BX   — 96 kHz / 125 µs / 1-8 channels.
 * - @c C    — 48 kHz / 125 µs / 9-64 channels (high channel count).
 * - @c CX   — 96 kHz / 125 µs / 9-32 channels.
 */
class AudioConformanceLevel : public TypedEnum<AudioConformanceLevel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AudioConformanceLevel", 0,
                                           {"None", 0}, {"A", 1}, {"AX", 2},
                                           {"B", 3}, {"BX", 4}, {"C", 5},
                                           {"CX", 6}); // default: None

                using TypedEnum<AudioConformanceLevel>::TypedEnum;

                static const AudioConformanceLevel None;
                static const AudioConformanceLevel A;
                static const AudioConformanceLevel AX;
                static const AudioConformanceLevel B;
                static const AudioConformanceLevel BX;
                static const AudioConformanceLevel C;
                static const AudioConformanceLevel CX;

                /**
                 * @brief Computes the ST 2110-30:2025 §7 Table 2 sender
                 *        conformance level for a stream shape.
                 *
                 * Returns the lowest matching level (@c A before
                 * @c AX, @c B before @c BX, @c C before @c CX) when
                 * the (rate, packet time, channels) tuple matches a
                 * Table 2 row; returns @c None when the combination
                 * is outside every conformance row (caller should
                 * warn but not reject — AES67 still permits the
                 * combination, just not as a ST 2110-30 level).
                 *
                 * @param sampleRateHz   The stream's digital audio
                 *                       sample rate in Hz.
                 * @param packetTimeUs   The AES67 packet time in
                 *                       microseconds (1000 for Level
                 *                       A / AX; 125 for B / BX / C /
                 *                       CX).
                 * @param channels       The number of audio channels
                 *                       in the stream.
                 *
                 * @return The matching @ref AudioConformanceLevel,
                 *         or @c None when no row in Table 2 matches.
                 */
                static AudioConformanceLevel compute(int sampleRateHz,
                                                    int packetTimeUs,
                                                    int channels);
};

inline const AudioConformanceLevel AudioConformanceLevel::None{0};
inline const AudioConformanceLevel AudioConformanceLevel::A{1};
inline const AudioConformanceLevel AudioConformanceLevel::AX{2};
inline const AudioConformanceLevel AudioConformanceLevel::B{3};
inline const AudioConformanceLevel AudioConformanceLevel::BX{4};
inline const AudioConformanceLevel AudioConformanceLevel::C{5};
inline const AudioConformanceLevel AudioConformanceLevel::CX{6};

inline AudioConformanceLevel AudioConformanceLevel::compute(int sampleRateHz,
                                                            int packetTimeUs,
                                                            int channels) {
        // Table 2 (Senders): a stream qualifies for a level when its
        // (rate, ptime, channels) tuple falls within that row.  Walk
        // from lowest level to highest so the lowest matching level
        // wins (Level A is preferred over AX for the same 48k/1ms
        // case so single-receiver Level A boxes interoperate).
        if (channels < 1) return None;
        if (sampleRateHz == 48000 && packetTimeUs == 1000 && channels >= 1 && channels <= 8) {
                return A;
        }
        if (sampleRateHz == 96000 && packetTimeUs == 1000 && channels >= 1 && channels <= 4) {
                return AX;
        }
        if (sampleRateHz == 48000 && packetTimeUs == 125 && channels >= 1 && channels <= 8) {
                return B;
        }
        if (sampleRateHz == 96000 && packetTimeUs == 125 && channels >= 1 && channels <= 8) {
                return BX;
        }
        if (sampleRateHz == 48000 && packetTimeUs == 125 && channels >= 9 && channels <= 64) {
                return C;
        }
        if (sampleRateHz == 96000 && packetTimeUs == 125 && channels >= 9 && channels <= 32) {
                return CX;
        }
        return None;
}

/**
 * @brief Well-known Enum type for the ST 2110-20 @c sampling SDP fmtp parameter.
 *
 * Lists the colour-difference sub-sampling structures defined by
 * SMPTE ST 2110-20:2022 §7.4.1.  Identifiers and value names follow
 * the project's CamelCase convention; the @c YCbCr / @c CLYCbCr /
 * @c ICtCp letter casing already matches the spec's own mixed-case
 * spelling.  The wire form (e.g. @c YCbCr-4:2:2) lives one layer up
 * in the SDP fmtp builder / parser — same pattern used by
 * @ref RtpTsMode (@c Samp / @c New / @c Pres → wire @c SAMP / @c NEW /
 * @c PRES).
 */
class St2110Sampling : public TypedEnum<St2110Sampling> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110Sampling", 0,
                                           {"Invalid", 0},
                                           {"YCbCr444", 1}, {"YCbCr422", 2}, {"YCbCr420", 3},
                                           {"CLYCbCr444", 4}, {"CLYCbCr422", 5}, {"CLYCbCr420", 6},
                                           {"ICtCp444", 7}, {"ICtCp422", 8}, {"ICtCp420", 9},
                                           {"Rgb", 10}, {"Xyz", 11},
                                           {"Key", 12}); // default: Invalid

                using TypedEnum<St2110Sampling>::TypedEnum;

                static const St2110Sampling Invalid;
                static const St2110Sampling YCbCr444;
                static const St2110Sampling YCbCr422;
                static const St2110Sampling YCbCr420;
                static const St2110Sampling CLYCbCr444;
                static const St2110Sampling CLYCbCr422;
                static const St2110Sampling CLYCbCr420;
                static const St2110Sampling ICtCp444;
                static const St2110Sampling ICtCp422;
                static const St2110Sampling ICtCp420;
                static const St2110Sampling Rgb;
                static const St2110Sampling Xyz;
                static const St2110Sampling Key;
};

inline const St2110Sampling St2110Sampling::Invalid{0};
inline const St2110Sampling St2110Sampling::YCbCr444{1};
inline const St2110Sampling St2110Sampling::YCbCr422{2};
inline const St2110Sampling St2110Sampling::YCbCr420{3};
inline const St2110Sampling St2110Sampling::CLYCbCr444{4};
inline const St2110Sampling St2110Sampling::CLYCbCr422{5};
inline const St2110Sampling St2110Sampling::CLYCbCr420{6};
inline const St2110Sampling St2110Sampling::ICtCp444{7};
inline const St2110Sampling St2110Sampling::ICtCp422{8};
inline const St2110Sampling St2110Sampling::ICtCp420{9};
inline const St2110Sampling St2110Sampling::Rgb{10};
inline const St2110Sampling St2110Sampling::Xyz{11};
inline const St2110Sampling St2110Sampling::Key{12};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c depth SDP fmtp parameter.
 *
 * Lists the per-sample bit depths defined by SMPTE ST 2110-20:2022
 * §7.4.2.  Wire form is @c "8" / @c "10" / @c "12" / @c "16" /
 * @c "16f" — emitted by the SDP layer.  The project-side
 * identifiers @c Bits8 / @c Bits10 / etc. avoid the leading-digit
 * problem.
 */
class St2110Depth : public TypedEnum<St2110Depth> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110Depth", 0,
                                           {"Invalid", 0},
                                           {"Bits8", 1}, {"Bits10", 2}, {"Bits12", 3}, {"Bits16", 4},
                                           {"Bits16f", 5}); // default: Invalid

                using TypedEnum<St2110Depth>::TypedEnum;

                static const St2110Depth Invalid;
                static const St2110Depth Bits8;
                static const St2110Depth Bits10;
                static const St2110Depth Bits12;
                static const St2110Depth Bits16;
                static const St2110Depth Bits16f;
};

inline const St2110Depth St2110Depth::Invalid{0};
inline const St2110Depth St2110Depth::Bits8{1};
inline const St2110Depth St2110Depth::Bits10{2};
inline const St2110Depth St2110Depth::Bits12{3};
inline const St2110Depth St2110Depth::Bits16{4};
inline const St2110Depth St2110Depth::Bits16f{5};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c colorimetry SDP fmtp parameter.
 *
 * Lists the colorimetric specifications defined by SMPTE ST 2110-20:2022
 * §7.5.  Wire form is all-uppercase (@c BT601, @c BT709, @c ST2065-1,
 * etc.) — emitted by the SDP layer.
 */
class St2110Colorimetry : public TypedEnum<St2110Colorimetry> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110Colorimetry", 0,
                                           {"Invalid", 0},
                                           {"Bt601", 1}, {"Bt709", 2}, {"Bt2020", 3}, {"Bt2100", 4},
                                           {"St2065_1", 5}, {"St2065_3", 6}, {"Unspecified", 7},
                                           {"Xyz", 8}, {"Alpha", 9}); // default: Invalid

                using TypedEnum<St2110Colorimetry>::TypedEnum;

                static const St2110Colorimetry Invalid;
                static const St2110Colorimetry Bt601;
                static const St2110Colorimetry Bt709;
                static const St2110Colorimetry Bt2020;
                static const St2110Colorimetry Bt2100;
                static const St2110Colorimetry St2065_1;
                static const St2110Colorimetry St2065_3;
                static const St2110Colorimetry Unspecified;
                static const St2110Colorimetry Xyz;
                static const St2110Colorimetry Alpha;
};

inline const St2110Colorimetry St2110Colorimetry::Invalid{0};
inline const St2110Colorimetry St2110Colorimetry::Bt601{1};
inline const St2110Colorimetry St2110Colorimetry::Bt709{2};
inline const St2110Colorimetry St2110Colorimetry::Bt2020{3};
inline const St2110Colorimetry St2110Colorimetry::Bt2100{4};
inline const St2110Colorimetry St2110Colorimetry::St2065_1{5};
inline const St2110Colorimetry St2110Colorimetry::St2065_3{6};
inline const St2110Colorimetry St2110Colorimetry::Unspecified{7};
inline const St2110Colorimetry St2110Colorimetry::Xyz{8};
inline const St2110Colorimetry St2110Colorimetry::Alpha{9};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c TCS SDP fmtp parameter.
 *
 * Lists the Transfer Characteristic System values defined by SMPTE
 * ST 2110-20:2022 §7.6.  Default value on the wire is @c SDR
 * (§7.6: "If the @c TCS value is not specified, receivers shall
 * assume the value @c SDR").  Wire form is all-uppercase
 * (@c SDR, @c BT2100LINPQ, @c ST2115LOGS3, etc.) — emitted by the
 * SDP layer.
 */
class St2110Tcs : public TypedEnum<St2110Tcs> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110Tcs", 1,
                                           {"Invalid", 0},
                                           {"Sdr", 1}, {"Pq", 2}, {"Hlg", 3}, {"Linear", 4},
                                           {"Bt2100LinPq", 5}, {"Bt2100LinHlg", 6},
                                           {"St2065_1", 7}, {"St428_1", 8},
                                           {"Density", 9}, {"St2115LogS3", 10},
                                           {"Unspecified", 11}); // default: Sdr

                using TypedEnum<St2110Tcs>::TypedEnum;

                static const St2110Tcs Invalid;
                static const St2110Tcs Sdr;
                static const St2110Tcs Pq;
                static const St2110Tcs Hlg;
                static const St2110Tcs Linear;
                static const St2110Tcs Bt2100LinPq;
                static const St2110Tcs Bt2100LinHlg;
                static const St2110Tcs St2065_1;
                static const St2110Tcs St428_1;
                static const St2110Tcs Density;
                static const St2110Tcs St2115LogS3;
                static const St2110Tcs Unspecified;
};

inline const St2110Tcs St2110Tcs::Invalid{0};
inline const St2110Tcs St2110Tcs::Sdr{1};
inline const St2110Tcs St2110Tcs::Pq{2};
inline const St2110Tcs St2110Tcs::Hlg{3};
inline const St2110Tcs St2110Tcs::Linear{4};
inline const St2110Tcs St2110Tcs::Bt2100LinPq{5};
inline const St2110Tcs St2110Tcs::Bt2100LinHlg{6};
inline const St2110Tcs St2110Tcs::St2065_1{7};
inline const St2110Tcs St2110Tcs::St428_1{8};
inline const St2110Tcs St2110Tcs::Density{9};
inline const St2110Tcs St2110Tcs::St2115LogS3{10};
inline const St2110Tcs St2110Tcs::Unspecified{11};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c RANGE SDP fmtp parameter.
 *
 * §7.3: @c NARROW or @c FULL when paired with BT.2100 colorimetry;
 * @c NARROW, @c FULL, or @c FULLPROTECT in any other context.
 * Default @c NARROW (§7.3: "In the absence of this parameter,
 * @c NARROW shall be the assumed value in either case").  Wire form
 * is all-uppercase — emitted by the SDP layer.
 */
class St2110Range : public TypedEnum<St2110Range> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110Range", 1,
                                           {"Invalid", 0},
                                           {"Narrow", 1}, {"Full", 2},
                                           {"FullProtect", 3}); // default: Narrow

                using TypedEnum<St2110Range>::TypedEnum;

                static const St2110Range Invalid;
                static const St2110Range Narrow;
                static const St2110Range Full;
                static const St2110Range FullProtect;
};

inline const St2110Range St2110Range::Invalid{0};
inline const St2110Range St2110Range::Narrow{1};
inline const St2110Range St2110Range::Full{2};
inline const St2110Range St2110Range::FullProtect{3};

/**
 * @brief Well-known Enum type for the ST 2110-20 @c PM (Packing Mode) SDP fmtp parameter.
 *
 * §6.3: General Packing Mode (wire @c 2110GPM) is the default and
 * allows any pgroup-aligned packetization; Block Packing Mode
 * (wire @c 2110BPM) constrains every packet to a multiple-of-180-
 * octet payload.  The wire form starts with a leading digit, so the
 * project-side identifiers are @c Gpm / @c Bpm.
 */
class St2110PackingMode : public TypedEnum<St2110PackingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("St2110PackingMode", 1,
                                           {"Invalid", 0},
                                           {"Gpm", 1},
                                           {"Bpm", 2}); // default: Gpm

                using TypedEnum<St2110PackingMode>::TypedEnum;

                static const St2110PackingMode Invalid;
                static const St2110PackingMode Gpm;
                static const St2110PackingMode Bpm;
};

inline const St2110PackingMode St2110PackingMode::Invalid{0};
inline const St2110PackingMode St2110PackingMode::Gpm{1};
inline const St2110PackingMode St2110PackingMode::Bpm{2};

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
 * - @c Ltc        — decode LTC from the audio track (independent
 *                   feature; not required by @c AvSync).
 * - @c AvSync     — A/V sync offset in samples, derived from the
 *                   shared frame-number marker that
 *                   @ref ImageDataEncoder and @ref AudioDataEncoder
 *                   both stamp.  Implies @c ImageData + @c AudioData.
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
 * - @c AudioData    — decode the @ref AudioDataEncoder
 *                     @c [stream:8][channel:8][frame:48] codeword from
 *                     every audio channel, validate CRC + sync nibble,
 *                     and flag mismatched channel bytes as
 *                     @ref InspectorDiscontinuity::AudioChannelMismatch.
 *                     Default-on; opt out when the upstream carries no
 *                     @c PcmMarker channels.
 */
class InspectorTest : public TypedEnum<InspectorTest> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("InspectorTest", 0, {"ImageData", 0}, {"Ltc", 1}, {"AvSync", 2},
                                           {"Continuity", 3}, {"Timestamp", 4}, {"AudioSamples", 5},
                                           {"CaptureStats", 6}, {"AudioData", 7}, {"AncData", 8});

                using TypedEnum<InspectorTest>::TypedEnum;

                static const InspectorTest ImageData;
                static const InspectorTest Ltc;
                static const InspectorTest AvSync;
                static const InspectorTest Continuity;
                static const InspectorTest Timestamp;
                static const InspectorTest AudioSamples;
                static const InspectorTest CaptureStats;
                static const InspectorTest AudioData;
                static const InspectorTest AncData;
};

inline const InspectorTest InspectorTest::ImageData{0};
inline const InspectorTest InspectorTest::Ltc{1};
inline const InspectorTest InspectorTest::AvSync{2};
inline const InspectorTest InspectorTest::Continuity{3};
inline const InspectorTest InspectorTest::Timestamp{4};
inline const InspectorTest InspectorTest::AudioSamples{5};
inline const InspectorTest InspectorTest::CaptureStats{6};
inline const InspectorTest InspectorTest::AudioData{7};
inline const InspectorTest InspectorTest::AncData{8};

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
                                           {"Aux4", 28}, {"Aux5", 29}, {"Aux6", 30}, {"Aux7", 31},
                                           {"LeftTotal", 32}, {"RightTotal", 33});

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
                /// @brief Matrix-stereo left-total channel (Lt of
                ///        LtRt, Dolby-Pro-Logic-style matrix-encoded
                ///        stereo).  Distinct from @c FrontLeft so a
                ///        ST 2110-30 @c LtRt grouping round-trips
                ///        losslessly through the SDP @c channel-order
                ///        attribute.
                static const ChannelRole LeftTotal;
                /// @brief Matrix-stereo right-total channel (Rt of
                ///        LtRt).
                static const ChannelRole RightTotal;
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
inline const ChannelRole ChannelRole::LeftTotal{32};
inline const ChannelRole ChannelRole::RightTotal{33};

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

/**
 * @brief Coarse category of a @ref NetworkInterface.
 *
 * Backends populate this from OS metadata (Linux sysfs, BSD/macOS
 * @c if_data.ifi_type, Windows @c IfType, …) so applications can
 * filter without parsing per-platform identifiers.
 *
 * - @c Unknown      — Backend could not classify the interface.
 * - @c Ethernet     — Wired Ethernet (IEEE 802.3).
 * - @c Wifi         — Wireless 802.11.
 * - @c Loopback     — The OS loopback (@c lo, @c lo0, etc.).
 * - @c Tunnel       — Generic tunnel (tun, ip6tnl, sit, gre, …).
 * - @c Bridge       — Software bridge over other interfaces.
 * - @c Vlan         — 802.1Q VLAN trunk member.
 * - @c Virtual      — Bonding/teaming aggregator or other software-
 *                     synthesized interface that doesn't fit a more
 *                     specific category.
 * - @c Cellular     — Mobile broadband (LTE, 5G, …).
 * - @c PointToPoint — Generic point-to-point link (PPP, etc.).
 */
class NetworkInterfaceKind : public TypedEnum<NetworkInterfaceKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("NetworkInterfaceKind", 0, {"Unknown", 0}, {"Ethernet", 1}, {"Wifi", 2},
                                           {"Loopback", 3}, {"Tunnel", 4}, {"Bridge", 5}, {"Vlan", 6}, {"Virtual", 7},
                                           {"Cellular", 8}, {"PointToPoint", 9}); // default: Unknown

                using TypedEnum<NetworkInterfaceKind>::TypedEnum;

                static const NetworkInterfaceKind Unknown;
                static const NetworkInterfaceKind Ethernet;
                static const NetworkInterfaceKind Wifi;
                static const NetworkInterfaceKind Loopback;
                static const NetworkInterfaceKind Tunnel;
                static const NetworkInterfaceKind Bridge;
                static const NetworkInterfaceKind Vlan;
                static const NetworkInterfaceKind Virtual;
                static const NetworkInterfaceKind Cellular;
                static const NetworkInterfaceKind PointToPoint;
};

inline const NetworkInterfaceKind NetworkInterfaceKind::Unknown{0};
inline const NetworkInterfaceKind NetworkInterfaceKind::Ethernet{1};
inline const NetworkInterfaceKind NetworkInterfaceKind::Wifi{2};
inline const NetworkInterfaceKind NetworkInterfaceKind::Loopback{3};
inline const NetworkInterfaceKind NetworkInterfaceKind::Tunnel{4};
inline const NetworkInterfaceKind NetworkInterfaceKind::Bridge{5};
inline const NetworkInterfaceKind NetworkInterfaceKind::Vlan{6};
inline const NetworkInterfaceKind NetworkInterfaceKind::Virtual{7};
inline const NetworkInterfaceKind NetworkInterfaceKind::Cellular{8};
inline const NetworkInterfaceKind NetworkInterfaceKind::PointToPoint{9};

/**
 * @brief Well-known Enum type for the RTMP role of a session.
 *
 * Shared by @c RtmpHandshake, @c RtmpSession, the future
 * @c RtmpServer, and any other layer that needs to pick a side.
 * Declared once here rather than as a private nested enum on each
 * class so the role can be threaded through MediaConfig keys and
 * Variant payloads without losing type identity.
 */
class RtmpRole : public TypedEnum<RtmpRole> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtmpRole", 0, {"Client", 0}, {"Server", 1}); // default: Client

                using TypedEnum<RtmpRole>::TypedEnum;

                static const RtmpRole Client;
                static const RtmpRole Server;
};

inline const RtmpRole RtmpRole::Client{0};
inline const RtmpRole RtmpRole::Server{1};

/**
 * @brief Well-known Enum type for the RTMP handshake mode.
 *
 * Drives both @c RtmpHandshake::setMode and the
 * @c MediaConfig::RtmpHandshakeMode key.
 *
 * - @c Auto    — try Complex first, fall back to Simple on the first
 *                peer-side rejection / disconnect.  Default — matches
 *                the OBS / FFmpeg compatibility layer.
 * - @c Simple  — RTMP 1.0 §5.2.1 / FLV-spec handshake (1-byte version
 *                + 1536-byte random nonce).  Some destinations reject
 *                this form (Wowza, some nginx-rtmp builds, historically
 *                Twitch); use @c Complex / @c Auto on those.
 * - @c Complex — Adobe FMS3 "digest+key" handshake.  HMAC-SHA256 over
 *                the C1/S1 payload with the GenuineFP / GenuineFMS keys.
 *                Required by some destinations.
 */
class RtmpHandshakeMode : public TypedEnum<RtmpHandshakeMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtmpHandshakeMode", 0, {"Auto", 0}, {"Simple", 1},
                                           {"Complex", 2}); // default: Auto

                using TypedEnum<RtmpHandshakeMode>::TypedEnum;

                static const RtmpHandshakeMode Auto;
                static const RtmpHandshakeMode Simple;
                static const RtmpHandshakeMode Complex;
};

inline const RtmpHandshakeMode RtmpHandshakeMode::Auto{0};
inline const RtmpHandshakeMode RtmpHandshakeMode::Simple{1};
inline const RtmpHandshakeMode RtmpHandshakeMode::Complex{2};

/**
 * @brief Well-known Enum type for the @c RtmpMediaIO sink-side video pacing source.
 *
 * Drives @c RtmpMediaIO's strand-side @c PacingGate clock-binding policy.
 * RTMP is single-TCP-stream and has no kernel-pacing analog, so without
 * an explicit gate a synthetic feeder (TPG, file relay) bursts a full
 * GOP onto the wire on every IDR.  This enum picks where the gate's
 * clock comes from.
 *
 * - @c Internal — bind the gate to a built-in @c WallClock paced
 *                 against @c MediaConfig::FrameRate (or the
 *                 @c pendingMediaDesc frame rate).  The default — most
 *                 live destinations want approximately real-time
 *                 cadence and the strand has no other backpressure
 *                 source besides the bounded MessageQueue.
 * - @c External — leave the gate unbound at @c Open.  Stays a no-op
 *                 until @c executeCmd(MediaIOCommandSetClock) arrives
 *                 (typically forwarded from an upstream capture
 *                 board's port-group clock through the planner).  No
 *                 fallback to internal wall clock — the gate is a
 *                 no-op until the external clock binds.
 * - @c None     — never arm the gate.  Strand floods the per-kind
 *                 PayloadQueues at the upstream rate; backpressure
 *                 comes only from the bounded MessageQueue.  Fast-
 *                 pump file ingest mode.
 *
 * An @c executeCmd(MediaIOCommandSetClock) with a non-null clock
 * always wins over @c Internal once it arrives.  A null clock from
 * @c setClock re-arms back to the @c Internal policy when the
 * configured mode is @c Internal, or stays unbound otherwise.
 */
class RtmpVideoPacing : public TypedEnum<RtmpVideoPacing> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("RtmpVideoPacing", 0, {"Internal", 0}, {"External", 1},
                                           {"None", 2}); // default: Internal

                using TypedEnum<RtmpVideoPacing>::TypedEnum;

                static const RtmpVideoPacing Internal;
                static const RtmpVideoPacing External;
                static const RtmpVideoPacing None;
};

inline const RtmpVideoPacing RtmpVideoPacing::Internal{0};
inline const RtmpVideoPacing RtmpVideoPacing::External{1};
inline const RtmpVideoPacing RtmpVideoPacing::None{2};

/**
 * @brief Well-known Enum classifying an @ref AncFormat by broad
 *        content category.
 *
 * Lets sinks declare "I carry Captions + Timecode only" by category
 * rather than enumerating every format ID, and drives inspector
 * grouping and metadata-stamping rules.
 *
 * - @c Unknown        — Default / uninitialised.
 * - @c Captions       — CEA-708, CEA-608 closed captions.
 * - @c Subtitles      — RDD 8 / OP-47 subtitling distribution
 *                       (distinct from line-21 / CEA-x captions).
 * - @c Timecode       — ATC LTC / VITC, future HDMI / NDI timecode.
 * - @c Splice         — SCTE-104, SCTE-35 ad / program markers.
 * - @c Aspect         — AFD, Bar Data, AVI InfoFrame aspect bits.
 * - @c Hdr            — SMPTE 2086 static, HDR10+ (ST 2094-40)
 *                       dynamic, Dolby Vision RPU.
 * - @c AudioMetadata  — SMPTE 2020 Dolby metadata, HDMI Audio
 *                       InfoFrame.
 * - @c Display        — Non-HDR display hints (HDMI AVI / SPD).
 * - @c Geolocation    — MISB ST 0601 KLV and friends.
 * - @c PayloadId      — SMPTE ST 352 Video Payload Identifier.
 * - @c Klv            — Generic SMPTE ST 336 KLV / MISB sensor or
 *                       analytics data that is not specifically
 *                       geolocation (which keeps its own bucket).
 * - @c Sei            — H.264 / HEVC SEI user-data registered or
 *                       unregistered messages carried in-band as
 *                       ANC (distinct from a CEA-708 NAL fragment,
 *                       which lives under @c Captions).
 * - @c Vbi            — ST 2031 line-21 / VBI carriage.
 * - @c UserDefined    — Application-supplied category.
 */
class AncCategory : public TypedEnum<AncCategory> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncCategory", 0, {"Unknown", 0}, {"Captions", 1}, {"Timecode", 2},
                                           {"Splice", 3}, {"Aspect", 4}, {"Hdr", 5}, {"AudioMetadata", 6},
                                           {"Display", 7}, {"Geolocation", 8}, {"PayloadId", 9},
                                           {"UserDefined", 10}, {"Subtitles", 11}, {"Klv", 12}, {"Sei", 13},
                                           {"Vbi", 14}, {"Control", 15}); // default: Unknown

                using TypedEnum<AncCategory>::TypedEnum;

                static const AncCategory Unknown;
                static const AncCategory Captions;
                static const AncCategory Timecode;
                static const AncCategory Splice;
                static const AncCategory Aspect;
                static const AncCategory Hdr;
                static const AncCategory AudioMetadata;
                static const AncCategory Display;
                static const AncCategory Geolocation;
                static const AncCategory PayloadId;
                static const AncCategory UserDefined;
                static const AncCategory Subtitles;
                static const AncCategory Klv;
                static const AncCategory Sei;
                static const AncCategory Vbi;
                /// @brief In-band control / management packets (Packet
                ///        Marked for Deletion ST 291-1 §6.3, EDH per
                ///        RP 165, status descriptors, …).  Distinct
                ///        from content-carrying categories so a caller
                ///        iterating `registeredIDsForCategory(Unknown)`
                ///        does not surface control packets.
                static const AncCategory Control;
};

inline const AncCategory AncCategory::Unknown{0};
inline const AncCategory AncCategory::Captions{1};
inline const AncCategory AncCategory::Timecode{2};
inline const AncCategory AncCategory::Splice{3};
inline const AncCategory AncCategory::Aspect{4};
inline const AncCategory AncCategory::Hdr{5};
inline const AncCategory AncCategory::AudioMetadata{6};
inline const AncCategory AncCategory::Display{7};
inline const AncCategory AncCategory::Geolocation{8};
inline const AncCategory AncCategory::PayloadId{9};
inline const AncCategory AncCategory::UserDefined{10};
inline const AncCategory AncCategory::Subtitles{11};
inline const AncCategory AncCategory::Klv{12};
inline const AncCategory AncCategory::Sei{13};
inline const AncCategory AncCategory::Vbi{14};
inline const AncCategory AncCategory::Control{15};

/**
 * @brief Well-known Enum naming the wire transport an @ref AncPacket
 *        currently rides on.
 *
 * Distinct from @ref AncCategory and @ref AncFormat (the logical
 * "what kind of data is this"): @c AncFormat::Cea708 is closed-
 * caption content regardless of whether it is currently riding
 * inside an ST 291 packet, an NDI XML metadata frame, or an AMF0
 * script tag.
 *
 * - @c Invalid        — Default / uninitialised.
 * - @c St291          — ST 291 ancillary packet.  SDI VANC / HANC
 *                       and RFC 8331 ST 2110-40 both consume this
 *                       transport directly.
 * - @c NdiXml         — NDI metadata frame body (UTF-8 XML).
 * - @c RtmpAmf        — RTMP AMF0 script tag value
 *                       (@c onCaptionInfo, @c onCuePoint,
 *                       @c onMetaData, …).
 * - @c HdmiInfoFrame  — HDMI InfoFrame body.
 * - @c MpegTsPrivate  — MPEG-TS private section / table
 *                       (SCTE-35 splice_info, KLV, ARIB).
 * - @c HlsSei         — H.264 / HEVC SEI user-data registered
 *                       message (CEA-708 NAL fragments for HLS
 *                       muxers).
 */
class AncTransport : public TypedEnum<AncTransport> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncTransport", 0, {"Invalid", 0}, {"St291", 1}, {"NdiXml", 2},
                                           {"RtmpAmf", 3}, {"HdmiInfoFrame", 4}, {"MpegTsPrivate", 5},
                                           {"HlsSei", 6}); // default: Invalid

                using TypedEnum<AncTransport>::TypedEnum;

                static const AncTransport Invalid;
                static const AncTransport St291;
                static const AncTransport NdiXml;
                static const AncTransport RtmpAmf;
                static const AncTransport HdmiInfoFrame;
                static const AncTransport MpegTsPrivate;
                static const AncTransport HlsSei;
};

inline const AncTransport AncTransport::Invalid{0};
inline const AncTransport AncTransport::St291{1};
inline const AncTransport AncTransport::NdiXml{2};
inline const AncTransport AncTransport::RtmpAmf{3};
inline const AncTransport AncTransport::HdmiInfoFrame{4};
inline const AncTransport AncTransport::MpegTsPrivate{5};
inline const AncTransport AncTransport::HlsSei{6};

/**
 * @brief Well-known Enum naming the ST 2110-40 transmission model
 *        a sender advertises in SDP @c TM= fmtp.
 *
 * Per ST 2110-40:2023 §6 the receiver-side timing model is one of:
 *
 *  - @c Unsignalled — Sender omits the @c TM fmtp parameter
 *    entirely.  SSN remains pinned at @c ST2110-40:2018 because the
 *    :2023 revision §7 SSN/TM coupling rule (TM SHALL pair with
 *    @c SSN=:2023) does not allow @c TM= to be signalled under the
 *    :2018 form.  This is the default and matches a -10 sender that
 *    doesn't care which timing model receivers assume.
 *  - @c Lltm        — Low-Latency Transmission Model (§6.4).  Sender
 *    SHALL transmit each RTP packet no later than
 *    @c T_FST + T_EPO + T_D with
 *    <tt>T_D = 8 / (FrameRate * TotalLines)</tt>.  Requires
 *    PacketScheduler per-packet deadlines (SO_TXTIME) — the SDP
 *    signalling lands here so receivers can opt into the
 *    LLTM-conformant behaviour; the actual deadline injection is
 *    gated on @c RtpPacingMode::TxTime being available.
 *  - @c Ctm         — Compatible Transmission Model (§6.5).
 *    @c T_D = 1 ms; trivially achievable with kernel pacing.  A
 *    sender signals @c Ctm when it can guarantee the looser bound
 *    but does not have the LLTM hardware path.
 *
 * @see AncDesc::transmissionModel
 */
class AncTransmissionModel : public TypedEnum<AncTransmissionModel> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncTransmissionModel", 0, {"Unsignalled", 0}, {"Lltm", 1},
                                           {"Ctm", 2}); // default: Unsignalled

                using TypedEnum<AncTransmissionModel>::TypedEnum;

                static const AncTransmissionModel Unsignalled;
                static const AncTransmissionModel Lltm;
                static const AncTransmissionModel Ctm;
};

inline const AncTransmissionModel AncTransmissionModel::Unsignalled{0};
inline const AncTransmissionModel AncTransmissionModel::Lltm{1};
inline const AncTransmissionModel AncTransmissionModel::Ctm{2};

/**
 * @brief Well-known Enum controlling ANC translator output verbosity.
 *
 * Used as the value of the
 * @c AncTranslateConfig::Fidelity key.  Honoured by translators
 * emitting onto transports with multiple valid representations
 * (NDI XML, RTMP AMF, JSON sidecars).
 *
 * - @c Default — Translator picks its preferred form (normally
 *                equivalent to @c Strict).
 * - @c Strict  — Minimum required fields for a valid emit.
 * - @c Full    — Every optional field, most verbose form.
 */
class AncFidelity : public TypedEnum<AncFidelity> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncFidelity", 0, {"Default", 0}, {"Strict", 1},
                                           {"Full", 2}); // default: Default

                using TypedEnum<AncFidelity>::TypedEnum;

                static const AncFidelity Default;
                static const AncFidelity Strict;
                static const AncFidelity Full;
};

inline const AncFidelity AncFidelity::Default{0};
inline const AncFidelity AncFidelity::Strict{1};
inline const AncFidelity AncFidelity::Full{2};

/**
 * @brief Well-known Enum governing how @ref AncTransport::St291
 *        builders handle the per-packet checksum.
 *
 * Used as the value of the @c AncTranslateConfig::Checksum key.
 *
 * - @c PreserveOrRecompute — Use the stored checksum when it is
 *                            valid; recompute when missing or
 *                            wrong.  Default; preserves byte-exact
 *                            replay for captured packets.
 * - @c AlwaysRecompute     — Recompute on every output.
 * - @c StrictValidate      — Fail with @c Error::InvalidChecksum
 *                            when the stored checksum does not
 *                            match the recomputed one.
 */
class AncChecksumPolicy : public TypedEnum<AncChecksumPolicy> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncChecksumPolicy", 0, {"PreserveOrRecompute", 0}, {"AlwaysRecompute", 1},
                                           {"StrictValidate", 2}); // default: PreserveOrRecompute

                using TypedEnum<AncChecksumPolicy>::TypedEnum;

                static const AncChecksumPolicy PreserveOrRecompute;
                static const AncChecksumPolicy AlwaysRecompute;
                static const AncChecksumPolicy StrictValidate;
};

inline const AncChecksumPolicy AncChecksumPolicy::PreserveOrRecompute{0};
inline const AncChecksumPolicy AncChecksumPolicy::AlwaysRecompute{1};
inline const AncChecksumPolicy AncChecksumPolicy::StrictValidate{2};

/**
 * @brief Well-known Enum controlling ANC translator behaviour when
 *        the input cannot be represented in the target transport.
 *
 * Used as the value of the @c AncTranslateConfig::OnUnsupported key.
 *
 * - @c Skip       — Return @c Error::NotSupported; the caller
 *                   (sink backend) drops the packet and logs once
 *                   per format.
 * - @c BestEffort — Default.  Emit what can be represented and
 *                   return OK; the translator documents what was
 *                   preserved.
 * - @c Fail       — Hard error with a descriptive message.
 */
class AncOnUnsupported : public TypedEnum<AncOnUnsupported> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("AncOnUnsupported", 1, {"Skip", 0}, {"BestEffort", 1},
                                           {"Fail", 2}); // default: BestEffort

                using TypedEnum<AncOnUnsupported>::TypedEnum;

                static const AncOnUnsupported Skip;
                static const AncOnUnsupported BestEffort;
                static const AncOnUnsupported Fail;
};

inline const AncOnUnsupported AncOnUnsupported::Skip{0};
inline const AncOnUnsupported AncOnUnsupported::BestEffort{1};
inline const AncOnUnsupported AncOnUnsupported::Fail{2};

/**
 * @brief Well-known nine-position anchor for subtitle placement.
 *
 * The displayed-block anchor used by every subtitle format that
 * exposes positioning.  Values 1..9 match the ASS / SSA
 * @c {\anN} numpad convention exactly so SRT files carrying the
 * ASS extension can value-cast directly:
 *
 * @code
 * 1 = BottomLeft   2 = BottomCenter   3 = BottomRight
 * 4 = MiddleLeft   5 = MiddleCenter   6 = MiddleRight
 * 7 = TopLeft      8 = TopCenter      9 = TopRight
 * @endcode
 *
 * @c Default (0) means "no anchor explicitly specified by the
 * source file" — renderers fall back to their own default (almost
 * universally @c BottomCenter for caption-style subtitles).  Used
 * by @ref Subtitle::anchor.
 */
class SubtitleAnchor : public TypedEnum<SubtitleAnchor> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleAnchor", 0, {"Default", 0}, {"BottomLeft", 1},
                                           {"BottomCenter", 2}, {"BottomRight", 3}, {"MiddleLeft", 4},
                                           {"MiddleCenter", 5}, {"MiddleRight", 6}, {"TopLeft", 7},
                                           {"TopCenter", 8}, {"TopRight", 9}); // default: Default

                using TypedEnum<SubtitleAnchor>::TypedEnum;

                static const SubtitleAnchor Default;
                static const SubtitleAnchor BottomLeft;
                static const SubtitleAnchor BottomCenter;
                static const SubtitleAnchor BottomRight;
                static const SubtitleAnchor MiddleLeft;
                static const SubtitleAnchor MiddleCenter;
                static const SubtitleAnchor MiddleRight;
                static const SubtitleAnchor TopLeft;
                static const SubtitleAnchor TopCenter;
                static const SubtitleAnchor TopRight;
};

inline const SubtitleAnchor SubtitleAnchor::Default{0};
inline const SubtitleAnchor SubtitleAnchor::BottomLeft{1};
inline const SubtitleAnchor SubtitleAnchor::BottomCenter{2};
inline const SubtitleAnchor SubtitleAnchor::BottomRight{3};
inline const SubtitleAnchor SubtitleAnchor::MiddleLeft{4};
inline const SubtitleAnchor SubtitleAnchor::MiddleCenter{5};
inline const SubtitleAnchor SubtitleAnchor::MiddleRight{6};
inline const SubtitleAnchor SubtitleAnchor::TopLeft{7};
inline const SubtitleAnchor SubtitleAnchor::TopCenter{8};
inline const SubtitleAnchor SubtitleAnchor::TopRight{9};

/**
 * @brief Where a subtitle renderer should look for the active cue.
 *
 * Used by @ref SubtitleBurnMediaIO's
 * @c MediaConfig::VideoSubtitleBurnSources key as an *ordered*
 * preference list — the renderer queries each source in turn and
 * paints the first cue it finds.  An empty list disables rendering
 * entirely.
 *
 *  - @c Metadata — read @c Metadata::Subtitle off the frame.  Cheap
 *    and zero-coupling; works for any upstream that stamps cues
 *    (the TPG SubRip path, future file readers, etc.).
 *  - @c Cea608Anc — decode CEA-608 captions from the frame's
 *    @c AncPayloads via the stateful @ref Cea608Decoder.  Useful
 *    when subtitles arrive embedded in ANC (broadcast capture,
 *    RTP-40, NDI).  v1 supports the @c CC1 pop-on subset that
 *    @ref Cea608Decoder implements.
 *  - @c Cea708Anc — decode CEA-708 DTVCC captions from the frame's
 *    @c AncPayloads via the stateful @ref Cea708Decoder.  Walks
 *    the @c cc_type=2/3 triples of every CDP, runs the configured
 *    service block through the 8-window state machine, and surfaces
 *    @ref Cea708Decoder::displayedCue.  Defaults to service 1 (the
 *    primary English caption service); other services need the
 *    decoder configured explicitly (out of scope for v1 — exposed
 *    later through a renderer config key when a real multi-service
 *    stream lands).
 *
 * Future sources (@c HlsSei, @c RtmpAmf, @c NdiXml, file-driven
 * SubRip side-channel) slot in by adding new enum values and
 * matching handlers in @ref SubtitleBurnMediaIO.
 */
class SubtitleSource : public TypedEnum<SubtitleSource> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleSource", 1, {"Metadata", 1}, {"Cea608Anc", 2},
                                           {"Cea708Anc", 3});

                using TypedEnum<SubtitleSource>::TypedEnum;

                static const SubtitleSource Metadata;
                static const SubtitleSource Cea608Anc;
                static const SubtitleSource Cea708Anc;
};

inline const SubtitleSource SubtitleSource::Metadata{1};
inline const SubtitleSource SubtitleSource::Cea608Anc{2};
inline const SubtitleSource SubtitleSource::Cea708Anc{3};

/**
 * @brief Caption display-mode selector (per-cue).
 * @ingroup proav
 *
 * Both CEA-608 and CEA-708 wire formats support three caption display
 * modes; this enum carries the mode in a codec-agnostic way so the
 * @ref Subtitle data model can round-trip the producer's choice
 * across format adapters and back to the encoders.
 *
 *  - @c Default — "let the encoder decide".  Encoders fall back to
 *    their @c Config-level default (typically @c PopOn).
 *  - @c PopOn — pre-recorded mode.  Cue text is loaded into hidden
 *    memory and swapped to display at @c cue.start, cleared at
 *    @c cue.end.  Standard for offline-authored captions.
 *  - @c PaintOn — live mode.  Cue text is written directly to
 *    displayed memory character-by-character.  No swap — chars
 *    appear as transmitted.
 *  - @c RollUp — continuous scrolling captions.  Each cue is
 *    appended as a new row at the bottom; existing rows scroll up.
 *    Common in live broadcast.
 */
class CaptionMode : public TypedEnum<CaptionMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("CaptionMode", 0, {"Default", 0}, {"PopOn", 1}, {"PaintOn", 2},
                                           {"RollUp", 3}); // default: Default

                using TypedEnum<CaptionMode>::TypedEnum;

                static const CaptionMode Default;
                static const CaptionMode PopOn;
                static const CaptionMode PaintOn;
                static const CaptionMode RollUp;
};

inline const CaptionMode CaptionMode::Default{0};
inline const CaptionMode CaptionMode::PopOn{1};
inline const CaptionMode CaptionMode::PaintOn{2};
inline const CaptionMode CaptionMode::RollUp{3};

/**
 * @brief Per-span edge style (CEA-708 SetPenAttributes).
 * @ingroup proav
 *
 * Modelled directly after the 708 @c edge_type field in
 * @c SetPenAttributes — six broadcast-defined edge effects plus
 * @c None (no edge).  CEA-608 has no edge concept; 608 encoders
 * drop the field with a one-shot warning.
 */
class SubtitleEdgeStyle : public TypedEnum<SubtitleEdgeStyle> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleEdgeStyle", 0, {"None", 0}, {"Raised", 1}, {"Depressed", 2},
                                           {"Uniform", 3}, {"ShadowLeft", 4},
                                           {"ShadowRight", 5}); // default: None

                using TypedEnum<SubtitleEdgeStyle>::TypedEnum;

                static const SubtitleEdgeStyle None;
                static const SubtitleEdgeStyle Raised;
                static const SubtitleEdgeStyle Depressed;
                static const SubtitleEdgeStyle Uniform;
                static const SubtitleEdgeStyle ShadowLeft;
                static const SubtitleEdgeStyle ShadowRight;
};

inline const SubtitleEdgeStyle SubtitleEdgeStyle::None{0};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Raised{1};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Depressed{2};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::Uniform{3};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::ShadowLeft{4};
inline const SubtitleEdgeStyle SubtitleEdgeStyle::ShadowRight{5};

/**
 * @brief Per-component opacity selector (CEA-708 SetPenColor).
 * @ingroup proav
 *
 * Mirrors the 708 @c fg_opacity / @c bg_opacity / @c edge_opacity
 * fields.  @c Solid is the conventional default (opaque).
 * @c Flash blinks the component at ~1 Hz on the wire.
 * @c Translucent is ~50% alpha.  @c Transparent omits the
 * component entirely.
 *
 * CEA-608 has no opacity wire field; 608 encoders treat every
 * component as @c Solid and warn-and-drop on @c Translucent /
 * @c Transparent / @c Flash.
 */
class SubtitleOpacity : public TypedEnum<SubtitleOpacity> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleOpacity", 0, {"Solid", 0}, {"Flash", 1}, {"Translucent", 2},
                                           {"Transparent", 3}); // default: Solid

                using TypedEnum<SubtitleOpacity>::TypedEnum;

                static const SubtitleOpacity Solid;
                static const SubtitleOpacity Flash;
                static const SubtitleOpacity Translucent;
                static const SubtitleOpacity Transparent;
};

inline const SubtitleOpacity SubtitleOpacity::Solid{0};
inline const SubtitleOpacity SubtitleOpacity::Flash{1};
inline const SubtitleOpacity SubtitleOpacity::Translucent{2};
inline const SubtitleOpacity SubtitleOpacity::Transparent{3};

/**
 * @brief Per-span font-face tag (CEA-708 SetPenAttributes).
 * @ingroup proav
 *
 * Eight font tags from the 708 @c font_tag field.  Renderers map
 * these to concrete TrueType faces at paint time.  CEA-608 has no
 * font-face concept; 608 encoders drop the field with a one-shot
 * warning when set to anything other than @c Default.
 */
class SubtitleFontFace : public TypedEnum<SubtitleFontFace> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleFontFace", 0, {"Default", 0}, {"MonoSerif", 1},
                                           {"ProportionalSerif", 2}, {"MonoSans", 3}, {"ProportionalSans", 4},
                                           {"Casual", 5}, {"Cursive", 6},
                                           {"SmallCaps", 7}); // default: Default

                using TypedEnum<SubtitleFontFace>::TypedEnum;

                static const SubtitleFontFace Default;
                static const SubtitleFontFace MonoSerif;
                static const SubtitleFontFace ProportionalSerif;
                static const SubtitleFontFace MonoSans;
                static const SubtitleFontFace ProportionalSans;
                static const SubtitleFontFace Casual;
                static const SubtitleFontFace Cursive;
                static const SubtitleFontFace SmallCaps;
};

inline const SubtitleFontFace SubtitleFontFace::Default{0};
inline const SubtitleFontFace SubtitleFontFace::MonoSerif{1};
inline const SubtitleFontFace SubtitleFontFace::ProportionalSerif{2};
inline const SubtitleFontFace SubtitleFontFace::MonoSans{3};
inline const SubtitleFontFace SubtitleFontFace::ProportionalSans{4};
inline const SubtitleFontFace SubtitleFontFace::Casual{5};
inline const SubtitleFontFace SubtitleFontFace::Cursive{6};
inline const SubtitleFontFace SubtitleFontFace::SmallCaps{7};

/**
 * @brief Pen-size attribute for a styled subtitle / caption span.
 * @ingroup proav
 *
 * Mirrors the @c pen_size field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.1): three discrete sizes the caption
 * author can request, with the receiver free to substitute its own
 * sizing.  CEA-608 has no concept of pen size — 608 encoders ignore
 * the field.
 *
 *  - @c Standard — the receiver's default size (the only size 708
 *    receivers are required to implement).
 *  - @c Small / @c Large — author hints; receivers may honour or
 *    fall back to Standard.
 */
class SubtitlePenSize : public TypedEnum<SubtitlePenSize> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitlePenSize", 1, {"Small", 0}, {"Standard", 1},
                                           {"Large", 2}); // default: Standard

                using TypedEnum<SubtitlePenSize>::TypedEnum;

                static const SubtitlePenSize Small;
                static const SubtitlePenSize Standard;
                static const SubtitlePenSize Large;
};

inline const SubtitlePenSize SubtitlePenSize::Small{0};
inline const SubtitlePenSize SubtitlePenSize::Standard{1};
inline const SubtitlePenSize SubtitlePenSize::Large{2};

/**
 * @brief Pen-offset (subscript / normal / superscript) attribute.
 * @ingroup proav
 *
 * Mirrors the @c offset field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.4): three discrete positions for the
 * character cell relative to the row baseline.  CEA-608 has no
 * concept of subscript / superscript — 608 encoders ignore the
 * field.
 *
 *  - @c Subscript — text offset downward from the baseline.
 *  - @c Normal — default, no offset.
 *  - @c Superscript — text offset upward from the baseline.
 */
class SubtitlePenOffset : public TypedEnum<SubtitlePenOffset> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitlePenOffset", 1, {"Subscript", 0}, {"Normal", 1},
                                           {"Superscript", 2}); // default: Normal

                using TypedEnum<SubtitlePenOffset>::TypedEnum;

                static const SubtitlePenOffset Subscript;
                static const SubtitlePenOffset Normal;
                static const SubtitlePenOffset Superscript;
};

inline const SubtitlePenOffset SubtitlePenOffset::Subscript{0};
inline const SubtitlePenOffset SubtitlePenOffset::Normal{1};
inline const SubtitlePenOffset SubtitlePenOffset::Superscript{2};

/**
 * @brief Semantic role tag for a styled subtitle / caption span.
 * @ingroup proav
 *
 * Mirrors the @c text_tag field of the CEA-708-E @c SetPenAttributes
 * command (§8.10.5.9 / §8.5.9): a 4-bit hint describing what the
 * upcoming text *represents*, independent of its visual styling.
 * Renderers and accessibility tools can use the tag to (e.g.) speak
 * source-ID prefixes in a different voice, hide lyrics from a captions
 * filter, or suppress invisible metadata.  Receivers that ignore the
 * field still display the text correctly — this is a hint, not a
 * format directive.
 *
 *  - @c Dialog — default; ordinary spoken dialog.
 *  - @c SourceId — speaker identification (e.g. "JOHN:").
 *  - @c ElectronicallyReproduced — phone, robot, PA system, etc.
 *  - @c DialogOtherLanguage — speech in a non-program language.
 *  - @c Voiceover — narration over scene audio.
 *  - @c AudibleTranslation — voiceover of foreign dialog.
 *  - @c SubtitleTranslation — written translation of foreign dialog.
 *  - @c VoiceDescription — descriptive video service (DVS).
 *  - @c Lyrics — song lyrics.
 *  - @c EffectDescription — sound effect description (e.g. "[barking]").
 *  - @c ScoreDescription — music description (e.g. "[ominous music]").
 *  - @c Expletive — bleeped or censored word.
 *  - @c Reserved12 / @c Reserved13 / @c Reserved14 — undefined by the spec.
 *  - @c NotDisplayed — metadata payload; receivers should not render it
 *    (used for hidden control / search-index data).
 *
 * CEA-608 has no text-tag concept; 608 encoders drop the field.
 */
class SubtitleTextTag : public TypedEnum<SubtitleTextTag> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SubtitleTextTag", 0, {"Dialog", 0}, {"SourceId", 1},
                                           {"ElectronicallyReproduced", 2}, {"DialogOtherLanguage", 3},
                                           {"Voiceover", 4}, {"AudibleTranslation", 5},
                                           {"SubtitleTranslation", 6}, {"VoiceDescription", 7},
                                           {"Lyrics", 8}, {"EffectDescription", 9},
                                           {"ScoreDescription", 10}, {"Expletive", 11},
                                           {"Reserved12", 12}, {"Reserved13", 13}, {"Reserved14", 14},
                                           {"NotDisplayed", 15}); // default: Dialog

                using TypedEnum<SubtitleTextTag>::TypedEnum;

                static const SubtitleTextTag Dialog;
                static const SubtitleTextTag SourceId;
                static const SubtitleTextTag ElectronicallyReproduced;
                static const SubtitleTextTag DialogOtherLanguage;
                static const SubtitleTextTag Voiceover;
                static const SubtitleTextTag AudibleTranslation;
                static const SubtitleTextTag SubtitleTranslation;
                static const SubtitleTextTag VoiceDescription;
                static const SubtitleTextTag Lyrics;
                static const SubtitleTextTag EffectDescription;
                static const SubtitleTextTag ScoreDescription;
                static const SubtitleTextTag Expletive;
                static const SubtitleTextTag Reserved12;
                static const SubtitleTextTag Reserved13;
                static const SubtitleTextTag Reserved14;
                static const SubtitleTextTag NotDisplayed;
};

inline const SubtitleTextTag SubtitleTextTag::Dialog{0};
inline const SubtitleTextTag SubtitleTextTag::SourceId{1};
inline const SubtitleTextTag SubtitleTextTag::ElectronicallyReproduced{2};
inline const SubtitleTextTag SubtitleTextTag::DialogOtherLanguage{3};
inline const SubtitleTextTag SubtitleTextTag::Voiceover{4};
inline const SubtitleTextTag SubtitleTextTag::AudibleTranslation{5};
inline const SubtitleTextTag SubtitleTextTag::SubtitleTranslation{6};
inline const SubtitleTextTag SubtitleTextTag::VoiceDescription{7};
inline const SubtitleTextTag SubtitleTextTag::Lyrics{8};
inline const SubtitleTextTag SubtitleTextTag::EffectDescription{9};
inline const SubtitleTextTag SubtitleTextTag::ScoreDescription{10};
inline const SubtitleTextTag SubtitleTextTag::Expletive{11};
inline const SubtitleTextTag SubtitleTextTag::Reserved12{12};
inline const SubtitleTextTag SubtitleTextTag::Reserved13{13};
inline const SubtitleTextTag SubtitleTextTag::Reserved14{14};
inline const SubtitleTextTag SubtitleTextTag::NotDisplayed{15};

/**
 * @brief Closed-caption codec selector for ANC emission paths.
 * @ingroup proav
 *
 * Selects which CEA caption stream(s) a producer (e.g. the TPG) emits
 * into a @ref Cea708Cdp.  The CDP's @c cc_data list can carry both
 * line-21 byte pairs (CEA-608, @c cc_type=0/1) and DTVCC triples
 * (CEA-708, @c cc_type=2/3) in the same packet, which is how real
 * broadcast captioning rides — so all three values are first-class:
 *
 *  - @c Cea608 — line-21 only.  @ref Cea608Encoder drives one
 *    byte-pair per frame; consumers fall back to legacy 608
 *    decoders.  This is the default to preserve the historical
 *    TPG output shape.
 *  - @c Cea708 — DTVCC only.  @ref Cea708Encoder emits per-cue
 *    Define/Display/Hide window transactions; consumers use
 *    @ref Cea708Decoder.
 *  - @c Both — both encoders feed the same per-frame @c CcDataList.
 *    The 608 byte pair and the 708 triples ride together in a
 *    single CDP, mirroring SDI broadcast practice.
 */
class CaptionCodec : public TypedEnum<CaptionCodec> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("CaptionCodec", 0, {"Cea608", 0}, {"Cea708", 1},
                                           {"Both", 2}); // default: Cea608

                using TypedEnum<CaptionCodec>::TypedEnum;

                static const CaptionCodec Cea608;
                static const CaptionCodec Cea708;
                static const CaptionCodec Both;
};

inline const CaptionCodec CaptionCodec::Cea608{0};
inline const CaptionCodec CaptionCodec::Cea708{1};
inline const CaptionCodec CaptionCodec::Both{2};

/**
 * @brief Operating mode a @ref TranscriptionEngine session runs in.
 *
 * Used as the @ref TranscriptionConfig::mode field to tell the engine
 * whether the caller expects interim hypotheses as audio arrives or
 * only the final transcript once the input stream has been fully
 * submitted.
 *
 * - @c Streaming — the engine may emit partial cues during
 *                  @c submitFrame as it accumulates audio.  Partial
 *                  cues carry @ref Subtitle::partial @c true; the
 *                  same span of audio typically re-emits as a
 *                  finalised cue (with @ref Subtitle::partial
 *                  @c false) once an endpoint heuristic fires
 *                  (silence gap, punctuation, VAD trailing edge).
 *                  Useful for live caption overlays and low-latency
 *                  UI feedback.
 * - @c Batch     — the engine accumulates audio silently and only
 *                  starts emitting cues after @c flush.  Cues are
 *                  always finalised (@ref Subtitle::partial
 *                  @c false).  Useful for offline transcription
 *                  where global rescoring / two-pass decoders
 *                  produce measurably better results than streaming.
 *
 * Engines that only support one mode reject the other at
 * @c configure with @c Error::NotSupported; the
 * @ref TranscriptionEngine::BackendRecord declares the supported
 * modes so callers can pick a compatible backend up front.
 */
class TranscriptionMode : public TypedEnum<TranscriptionMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("TranscriptionMode", 0, {"Streaming", 0},
                                           {"Batch", 1}); // default: Streaming

                using TypedEnum<TranscriptionMode>::TypedEnum;

                static const TranscriptionMode Streaming;
                static const TranscriptionMode Batch;
};

inline const TranscriptionMode TranscriptionMode::Streaming{0};
inline const TranscriptionMode TranscriptionMode::Batch{1};

/**
 * @brief How a @ref TranscriptionConfig selects audio for transcription.
 *
 * The engine receives multichannel PCM through
 * @ref TranscriptionEngine::submitFrame and must decide which sample
 * stream actually feeds the speech-to-text decoder.  This enum picks
 * the selection strategy; the @c TranscriptionConfig carries the
 * accompanying payload (an @ref AudioChannelMap for role-based
 * selection, an @c int for single-channel selection, or nothing for
 * full downmix).
 *
 * - @c ChannelMap   — use the engine-specific downmix of the channels
 *                     whose @ref ChannelRole appears in the configured
 *                     @ref AudioChannelMap.  Example: @c {FrontCenter}
 *                     to listen only to the dialog stem in a 5.1
 *                     bed; @c {Mono} to pick out a Commentary track
 *                     in a multi-stream buffer.
 * - @c ChannelIndex — pick exactly one channel by its zero-based
 *                     index in the source @ref PcmAudioPayload.  Use
 *                     when the caller already knows the physical
 *                     channel layout.
 * - @c DownmixAll   — the engine sums every channel to mono and
 *                     transcribes that.  Useful as a degraded
 *                     fallback when the caller has no role / index
 *                     information.
 */
class TranscriptionChannelMode : public TypedEnum<TranscriptionChannelMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("TranscriptionChannelMode", 0, {"ChannelMap", 0},
                                           {"ChannelIndex", 1},
                                           {"DownmixAll", 2}); // default: ChannelMap

                using TypedEnum<TranscriptionChannelMode>::TypedEnum;

                static const TranscriptionChannelMode ChannelMap;
                static const TranscriptionChannelMode ChannelIndex;
                static const TranscriptionChannelMode DownmixAll;
};

inline const TranscriptionChannelMode TranscriptionChannelMode::ChannelMap{0};
inline const TranscriptionChannelMode TranscriptionChannelMode::ChannelIndex{1};
inline const TranscriptionChannelMode TranscriptionChannelMode::DownmixAll{2};

/**
 * @brief Physical connector kind on a video device.
 *
 * Identifies the family of the physical socket a video signal enters
 * or leaves through, independent of the link standard (SDI cable
 * count, HDMI spec level, etc.) running on the wire.  Paired with a
 * 1-based connector index on @ref VideoPortRef to name "the second
 * SDI input" or "the HDMI output" on a device.
 *
 * - @c Auto         — unspecified / defer to the backend.
 * - @c Sdi          — coaxial SDI BNC connector (SD/HD/3G/6G/12G).
 * - @c Hdmi         — Type-A HDMI connector.
 * - @c DisplayPort  — Standard or Mini DisplayPort.
 * - @c Composite    — analog composite (NTSC / PAL / SECAM).
 * - @c Component    — analog YPbPr / RGsB component video.
 * - @c SVideo       — analog S-Video (Y/C 4-pin mini-DIN).
 * - @c Sfp          — SFP / SFP+ cage carrying SDI-over-IP or
 *                     ST 2022-6 / ST 2110-20 traffic.
 */
class VideoConnectorKind : public TypedEnum<VideoConnectorKind> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoConnectorKind", 0, {"Auto", 0}, {"Sdi", 1}, {"Hdmi", 2},
                                           {"DisplayPort", 3}, {"Composite", 4}, {"Component", 5},
                                           {"SVideo", 6}, {"Sfp", 7}); // default: Auto

                using TypedEnum<VideoConnectorKind>::TypedEnum;

                static const VideoConnectorKind Auto;
                static const VideoConnectorKind Sdi;
                static const VideoConnectorKind Hdmi;
                static const VideoConnectorKind DisplayPort;
                static const VideoConnectorKind Composite;
                static const VideoConnectorKind Component;
                static const VideoConnectorKind SVideo;
                static const VideoConnectorKind Sfp;
};

inline const VideoConnectorKind VideoConnectorKind::Auto{0};
inline const VideoConnectorKind VideoConnectorKind::Sdi{1};
inline const VideoConnectorKind VideoConnectorKind::Hdmi{2};
inline const VideoConnectorKind VideoConnectorKind::DisplayPort{3};
inline const VideoConnectorKind VideoConnectorKind::Composite{4};
inline const VideoConnectorKind VideoConnectorKind::Component{5};
inline const VideoConnectorKind VideoConnectorKind::SVideo{6};
inline const VideoConnectorKind VideoConnectorKind::Sfp{7};

/**
 * @brief Link standard for an SDI signal carrier.
 *
 * Short identifiers spell out the link topology and the rate/standard
 * fragment: @c SL_ for single-link, @c DL_ for dual-link, @c QL_ for
 * quad-link.  The trailing fragment names the SMPTE family the
 * standard belongs to (HD / 3GA / 3GB / 3G / 6G / 12G / 24G) and, for
 * the quad-link variants, the sub-image mapping (Square Division vs.
 * 2-Sample Interleave).  The Doxygen description for each value
 * carries the underlying SMPTE document number.
 *
 * - @c Auto       — defer to the backend / source.
 * - @c SL_SD      — SD-SDI single-link (SMPTE ST 259), 270 Mbps.
 * - @c SL_HD      — HD-SDI single-link (SMPTE ST 292), 1.485 Gbps.
 * - @c DL_HD      — HD-SDI dual-link (SMPTE ST 372), 2 × 1.485 Gbps.
 * - @c SL_3GA     — 3G-SDI single-link Level A (SMPTE ST 425-1), 2.97 Gbps.
 * - @c SL_3GB     — 3G-SDI single-link Level B carrying two HD streams.
 * - @c DL_3GB     — 3G-SDI Level B mapped onto two physical links
 *                   (one logical stream split across two cables).
 * - @c DL_3G      — Dual-link 3G-SDI (SMPTE ST 425-2), 2 × 2.97 Gbps.
 * - @c QL_3G_SQD  — Quad-link 3G-SDI Square Division (SMPTE ST 425-3).
 * - @c QL_3G_2SI  — Quad-link 3G-SDI 2-Sample Interleave (SMPTE ST 425-5).
 * - @c SL_6G      — 6G-SDI single-link (SMPTE ST 2081), 5.94 Gbps.
 * - @c SL_12G     — 12G-SDI single-link (SMPTE ST 2082), 11.88 Gbps.
 * - @c SL_24G     — 24G-SDI single-link (SMPTE ST 2083), 23.76 Gbps.
 */
class SdiLinkStandard : public TypedEnum<SdiLinkStandard> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SdiLinkStandard", 0,
                                           {"Auto",       0},
                                           {"SL_SD",      1},
                                           {"SL_HD",      2},
                                           {"DL_HD",      3},
                                           {"SL_3GA",     4},
                                           {"SL_3GB",     5},
                                           {"DL_3GB",     6},
                                           {"DL_3G",      7},
                                           {"QL_3G_SQD",  8},
                                           {"QL_3G_2SI",  9},
                                           {"SL_6G",     10},
                                           {"SL_12G",    11},
                                           {"SL_24G",    12}); // default: Auto

                using TypedEnum<SdiLinkStandard>::TypedEnum;

                static const SdiLinkStandard Auto;
                static const SdiLinkStandard SL_SD;
                static const SdiLinkStandard SL_HD;
                static const SdiLinkStandard DL_HD;
                static const SdiLinkStandard SL_3GA;
                static const SdiLinkStandard SL_3GB;
                static const SdiLinkStandard DL_3GB;
                static const SdiLinkStandard DL_3G;
                static const SdiLinkStandard QL_3G_SQD;
                static const SdiLinkStandard QL_3G_2SI;
                static const SdiLinkStandard SL_6G;
                static const SdiLinkStandard SL_12G;
                static const SdiLinkStandard SL_24G;
};

inline const SdiLinkStandard SdiLinkStandard::Auto{0};
inline const SdiLinkStandard SdiLinkStandard::SL_SD{1};
inline const SdiLinkStandard SdiLinkStandard::SL_HD{2};
inline const SdiLinkStandard SdiLinkStandard::DL_HD{3};
inline const SdiLinkStandard SdiLinkStandard::SL_3GA{4};
inline const SdiLinkStandard SdiLinkStandard::SL_3GB{5};
inline const SdiLinkStandard SdiLinkStandard::DL_3GB{6};
inline const SdiLinkStandard SdiLinkStandard::DL_3G{7};
inline const SdiLinkStandard SdiLinkStandard::QL_3G_SQD{8};
inline const SdiLinkStandard SdiLinkStandard::QL_3G_2SI{9};
inline const SdiLinkStandard SdiLinkStandard::SL_6G{10};
inline const SdiLinkStandard SdiLinkStandard::SL_12G{11};
inline const SdiLinkStandard SdiLinkStandard::SL_24G{12};

/**
 * @brief Canonical SMPTE SDI wire-payload formats.
 *
 * Names the discrete bit-depth + sampling combinations the SDI spec
 * family standardises as on-the-wire payloads — orthogonal to the
 * @ref SdiLinkStandard (which says how many cables and at what rate)
 * and to the @ref PixelFormat (which describes framebuffer
 * memory layout, including padding and packing that does not survive
 * the framestore↔wire boundary).
 *
 * Use @ref sdiBitsPerPixel (in @c sdistandards.h) to get the
 * intrinsic per-pixel bit count for wire-bandwidth math, and
 * @ref sdiWireFormatFor (in @c sdiwireinference.h) to map a
 * framebuffer @ref PixelFormat to the wire format that naturally
 * carries it after the on-board pack/unpack step.
 *
 * - @c Auto         — unspecified.  Wire format is whatever the
 *                     standard / backend defaults to.  Backends
 *                     typically substitute @c YCbCr_422_10 for
 *                     single-link SDI when this is set.
 * - @c YCbCr_422_10 — 10-bit Y'CbCr 4:2:2.  The canonical SDI
 *                     payload — single-link SD / HD / 3G / 6G /
 *                     12G / 24G all carry this natively.
 * - @c YCbCr_422_12 — 12-bit Y'CbCr 4:2:2.  Higher-bit-depth
 *                     payload — single-link variants on 6G+ or
 *                     dual-link @c DL_3G.
 * - @c YCbCr_444_10 — 10-bit Y'CbCr 4:4:4.  Dual-link / 12G+
 *                     payload — full chroma, no subsampling.
 * - @c YCbCr_444_12 — 12-bit Y'CbCr 4:4:4.
 * - @c RGB_444_10   — 10-bit R'G'B' 4:4:4.  Dual-link / 12G+
 *                     payload for RGB-native production paths.
 * - @c RGB_444_12   — 12-bit R'G'B' 4:4:4.
 * - @c RGBA_444_10  — 10-bit R'G'B'A' 4:4:4:4.  RGB with key
 *                     alpha; used by some fill/key SDI pipelines.
 *
 * @note No @c RGBA_444_12 entry exists.  The SDI spec family does
 *       not standardise 12-bit RGBA as a single-link wire payload —
 *       12-bit production paths that need a key channel typically
 *       carry @c RGB_444_12 on one link and the alpha on a parallel
 *       link (fill/key pair) or as a sidecar.  Use @c RGB_444_12
 *       for the RGB component and route the alpha separately.
 */
class SdiWireFormat : public TypedEnum<SdiWireFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("SdiWireFormat", 0,
                                           {"Auto",         0},
                                           {"YCbCr_422_10", 1},
                                           {"YCbCr_422_12", 2},
                                           {"YCbCr_444_10", 3},
                                           {"YCbCr_444_12", 4},
                                           {"RGB_444_10",   5},
                                           {"RGB_444_12",   6},
                                           {"RGBA_444_10",  7}); // default: Auto

                using TypedEnum<SdiWireFormat>::TypedEnum;

                static const SdiWireFormat Auto;
                static const SdiWireFormat YCbCr_422_10;
                static const SdiWireFormat YCbCr_422_12;
                static const SdiWireFormat YCbCr_444_10;
                static const SdiWireFormat YCbCr_444_12;
                static const SdiWireFormat RGB_444_10;
                static const SdiWireFormat RGB_444_12;
                static const SdiWireFormat RGBA_444_10;
};

inline const SdiWireFormat SdiWireFormat::Auto{0};
inline const SdiWireFormat SdiWireFormat::YCbCr_422_10{1};
inline const SdiWireFormat SdiWireFormat::YCbCr_422_12{2};
inline const SdiWireFormat SdiWireFormat::YCbCr_444_10{3};
inline const SdiWireFormat SdiWireFormat::YCbCr_444_12{4};
inline const SdiWireFormat SdiWireFormat::RGB_444_10{5};
inline const SdiWireFormat SdiWireFormat::RGB_444_12{6};
inline const SdiWireFormat SdiWireFormat::RGBA_444_10{7};

/**
 * @brief HDMI specification version hint for an HDMI signal carrier.
 *
 * Tracks the version of the HDMI / CTA spec the source / sink is
 * announcing (or negotiated to).  Used as a hint on
 * @ref HdmiSignalConfig — the on-wire bandwidth is dictated by the
 * @ref VideoFormat in play; the version hint tells the backend
 * which feature subset (HDR static / dynamic metadata, ALLM, eARC,
 * FRL vs. TMDS, …) to advertise.
 *
 * - @c Auto    — defer to the device's EDID / capability discovery.
 * - @c Hdmi14  — HDMI 1.4b feature set (max 8.16 Gbps TMDS).
 * - @c Hdmi20  — HDMI 2.0/2.0b feature set (max 17.82 Gbps TMDS).
 * - @c Hdmi21  — HDMI 2.1 feature set (FRL up to 48 Gbps,
 *                Dynamic HDR, ALLM, VRR, eARC, …).
 */
class HdmiSpecVersion : public TypedEnum<HdmiSpecVersion> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("HdmiSpecVersion", 0,
                                           {"Auto",   0},
                                           {"Hdmi14", 1},
                                           {"Hdmi20", 2},
                                           {"Hdmi21", 3}); // default: Auto

                using TypedEnum<HdmiSpecVersion>::TypedEnum;

                static const HdmiSpecVersion Auto;
                static const HdmiSpecVersion Hdmi14;
                static const HdmiSpecVersion Hdmi20;
                static const HdmiSpecVersion Hdmi21;
};

inline const HdmiSpecVersion HdmiSpecVersion::Auto{0};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi14{1};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi20{2};
inline const HdmiSpecVersion HdmiSpecVersion::Hdmi21{3};

/**
 * @brief Source of a device's reference clock.
 *
 * Names the origin of the timing the device locks its outputs to.
 * Stored on @ref VideoReferenceConfig along with the rate family and
 * (when @c FromSignal) the input port the lock is sourced from.
 *
 * - @c FreeRun     — no external reference; the device generates its
 *                    own clock from a local oscillator.
 * - @c Genlock     — lock to a black-burst / tri-level reference
 *                    arriving on the device's dedicated REF / GENLOCK
 *                    BNC input.
 * - @c External    — lock to a generic external reference input
 *                    whose semantics the backend interprets.
 * - @c FromSignal  — lock to the signal arriving on one of the
 *                    device's own connectors (named by
 *                    @ref VideoReferenceConfig::signalPort).
 * - @c Ptp         — lock to a PTP / IEEE 1588 grandmaster (future).
 * - @c Word        — lock to a word-clock input (future).
 */
class VideoReferenceSource : public TypedEnum<VideoReferenceSource> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoReferenceSource", 0,
                                           {"FreeRun",    0},
                                           {"Genlock",    1},
                                           {"External",   2},
                                           {"FromSignal", 3},
                                           {"Ptp",        4},
                                           {"Word",       5}); // default: FreeRun

                using TypedEnum<VideoReferenceSource>::TypedEnum;

                static const VideoReferenceSource FreeRun;
                static const VideoReferenceSource Genlock;
                static const VideoReferenceSource External;
                static const VideoReferenceSource FromSignal;
                static const VideoReferenceSource Ptp;
                static const VideoReferenceSource Word;
};

inline const VideoReferenceSource VideoReferenceSource::FreeRun{0};
inline const VideoReferenceSource VideoReferenceSource::Genlock{1};
inline const VideoReferenceSource VideoReferenceSource::External{2};
inline const VideoReferenceSource VideoReferenceSource::FromSignal{3};
inline const VideoReferenceSource VideoReferenceSource::Ptp{4};
inline const VideoReferenceSource VideoReferenceSource::Word{5};

/**
 * @brief Rate family for a video reference clock.
 *
 * SDI / HDMI reference clocks come in two families derived from the
 * 148.5 MHz master oscillator: the integer-Hz family (24 / 25 / 30
 * / 50 / 60 fps) clocked at 148.5 MHz exactly, and the NTSC-derived
 * fractional family (23.976 / 29.97 / 59.94 fps) clocked at
 * 148.5 / 1.001 MHz.  The family pins down which lattice the device
 * generates; the actual frame rate within that family is supplied
 * by @ref MediaConfig::VideoFormat / @ref FrameRate.
 *
 * - @c Auto        — defer to the negotiated @ref VideoFormat.
 * - @c Integer     — 148.5 MHz family (24 / 25 / 30 / 50 / 60).
 * - @c Fractional  — 148.5/1.001 MHz family (23.976 / 29.97 / 59.94).
 */
class VideoReferenceRateFamily : public TypedEnum<VideoReferenceRateFamily> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE("VideoReferenceRateFamily", 0,
                                           {"Auto",       0},
                                           {"Integer",    1},
                                           {"Fractional", 2}); // default: Auto

                using TypedEnum<VideoReferenceRateFamily>::TypedEnum;

                static const VideoReferenceRateFamily Auto;
                static const VideoReferenceRateFamily Integer;
                static const VideoReferenceRateFamily Fractional;
};

inline const VideoReferenceRateFamily VideoReferenceRateFamily::Auto{0};
inline const VideoReferenceRateFamily VideoReferenceRateFamily::Integer{1};
inline const VideoReferenceRateFamily VideoReferenceRateFamily::Fractional{2};

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

#endif // PROMEKI_ENABLE_CORE
