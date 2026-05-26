/**
 * @file      enums_tpg.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Test pattern generator (video / audio / burn-in) enums.
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
 * Library-provided @ref Enum types that are commonly used as config
 * values throughout the ProAV pipeline.  Each class inherits from
 * @ref TypedEnum "TypedEnum<Self>" so its values carry a compile-time
 * type identity.  These were originally a single @c enums.h header;
 * they now live in per-subsystem @c enums_<group>.h headers so a change
 * to one group does not trigger a rebuild of every consumer.
 */

/** @addtogroup wellknownenums */
/** @{ */

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("VideoPattern", "Video Test Pattern", 0,
                                                   {"ColorBars", 0, "Color Bars (100%)"},
                                                   {"ColorBars75", 1, "Color Bars (75%)"},
                                                   {"Ramp", 2, "Luminance Ramp"},
                                                   {"Grid", 3, "Grid"},
                                                   {"Crosshatch", 4, "Crosshatch"},
                                                   {"Checkerboard", 5, "Checkerboard"},
                                                   {"SolidColor", 6, "Solid Color"},
                                                   {"White", 7, "White Field"},
                                                   {"Black", 8, "Black Field"},
                                                   {"Noise", 9, "Random Noise"},
                                                   {"ZonePlate", 10, "Zone Plate"},
                                                   {"ColorChecker", 11, "Color Checker Chart"},
                                                   {"SMPTE219", 12, "SMPTE RP 219 Bars"},
                                                   {"AvSync", 13, "A/V Sync Marker"},
                                                   {"MultiBurst", 14, "Multiburst"},
                                                   {"LimitRange", 15, "Range Limit Check"},
                                                   {"CircularZone", 16, "Circular Zone Plate"},
                                                   {"Alignment", 17, "Alignment Grid"},
                                                   {"SDIPathEQ", 18, "SDI Pathological (Equalizer)"},
                                                   {"SDIPathPLL", 19, "SDI Pathological (PLL)"}); // default: ColorBars

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("BurnPosition", "Burn-In Position", 4,
                                                   {"TopLeft", 0, "Top Left"},
                                                   {"TopCenter", 1, "Top Center"},
                                                   {"TopRight", 2, "Top Right"},
                                                   {"BottomLeft", 3, "Bottom Left"},
                                                   {"BottomCenter", 4, "Bottom Center"},
                                                   {"BottomRight", 5, "Bottom Right"},
                                                   {"Center", 6, "Center"}); // default: BottomCenter

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioPattern", "Audio Test Pattern", 0,
                                                   {"Tone", 0, "Sine Tone"},
                                                   {"Silence", 1, "Silence"},
                                                   {"LTC", 2, "Linear Timecode (LTC)"},
                                                   {"AvSync", 3, "A/V Sync Tone Burst"},
                                                   {"SrcProbe", 4, "Sample-Rate Probe (997 Hz)"},
                                                   {"ChannelId", 5, "Channel ID Tone"},
                                                   {"PcmMarker", 6, "PCM Marker"},
                                                   {"WhiteNoise", 7, "White Noise"},
                                                   {"PinkNoise", 8, "Pink Noise"},
                                                   {"Chirp", 9, "Logarithmic Chirp"},
                                                   {"DualTone", 10, "Dual Tone (SMPTE IMD)"},
                                                   {"Sweep", 11, "Linear Sweep"},
                                                   {"Polarity", 12, "Polarity Pulse"},
                                                   {"SteppedTone", 13, "Stepped Tone"},
                                                   {"Blits", 14, "EBU BLITS Sequence"},
                                                   {"EbuLineup", 15, "EBU Line-Up Tone"},
                                                   {"Dialnorm", 16, "Dialnorm Pink Noise"},
                                                   {"Iec60958", 17, "IEC 60958 Channel Status"}); // default: Tone

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

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
