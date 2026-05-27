/**
 * @file      enums_audio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Audio sample-format, channel-role, and audio test/marker enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>
#include <functional>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioMarkerType", "Audio Marker Type", 0,
                                           {"Unknown", 0, "Unknown / Unclassified"},
                                           {"SilenceFill", 1, "Synthesized Silence Fill"},
                                           {"ConcealedLoss", 2, "Concealed Packet Loss"},
                                           {"Discontinuity", 3, "Timeline Discontinuity"},
                                           {"Glitch", 4, "Detected Glitch"});

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioDataType", "Audio Sample Format", 1,
                                           {"Invalid", 0, "Invalid"},
                                           {"PCMI_Float32LE", 1, "PCM 32-bit Float, Interleaved, Little-Endian"},
                                           {"PCMI_Float32BE", 2, "PCM 32-bit Float, Interleaved, Big-Endian"},
                                           {"PCMI_S8", 3, "PCM Signed 8-bit, Interleaved"},
                                           {"PCMI_U8", 4, "PCM Unsigned 8-bit, Interleaved"},
                                           {"PCMI_S16LE", 5, "PCM Signed 16-bit, Interleaved, Little-Endian"},
                                           {"PCMI_U16LE", 6, "PCM Unsigned 16-bit, Interleaved, Little-Endian"},
                                           {"PCMI_S16BE", 7, "PCM Signed 16-bit, Interleaved, Big-Endian"},
                                           {"PCMI_U16BE", 8, "PCM Unsigned 16-bit, Interleaved, Big-Endian"},
                                           {"PCMI_S24LE", 9, "PCM Signed 24-bit, Interleaved, Little-Endian"},
                                           {"PCMI_U24LE", 10, "PCM Unsigned 24-bit, Interleaved, Little-Endian"},
                                           {"PCMI_S24BE", 11, "PCM Signed 24-bit, Interleaved, Big-Endian"},
                                           {"PCMI_U24BE", 12, "PCM Unsigned 24-bit, Interleaved, Big-Endian"},
                                           {"PCMI_S32LE", 13, "PCM Signed 32-bit, Interleaved, Little-Endian"},
                                           {"PCMI_U32LE", 14, "PCM Unsigned 32-bit, Interleaved, Little-Endian"},
                                           {"PCMI_S32BE", 15, "PCM Signed 32-bit, Interleaved, Big-Endian"},
                                           {"PCMI_U32BE", 16, "PCM Unsigned 32-bit, Interleaved, Big-Endian"}); // default: PCMI_Float32LE

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioWireFormat", "Audio Wire Format", 0,
                                           {"Auto", 0, "Automatic (L16 / L24 by Sample Width)"},
                                           {"L16", 1, "16-bit Linear PCM (L16)"},
                                           {"L24", 2, "24-bit Linear PCM (L24)"}); // default: Auto

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioConformanceLevel", "ST 2110-30 Conformance Level", 0,
                                           {"None", 0, "Non-Conformant"},
                                           {"A", 1, "Level A (48 kHz / 1 ms / 1-8 ch)"},
                                           {"AX", 2, "Level AX (96 kHz / 1 ms / 1-4 ch)"},
                                           {"B", 3, "Level B (48 kHz / 125 us / 1-8 ch)"},
                                           {"BX", 4, "Level BX (96 kHz / 125 us / 1-8 ch)"},
                                           {"C", 5, "Level C (48 kHz / 125 us / 9-64 ch)"},
                                           {"CX", 6, "Level CX (96 kHz / 125 us / 9-32 ch)"}); // default: None

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AudioSourceHint", "Audio Source Preference", 0,
                                           {"Sidecar", 0, "Sidecar Audio File"},
                                           {"Embedded", 1, "Embedded Per-Frame Audio"}); // default: Sidecar

                using TypedEnum<AudioSourceHint>::TypedEnum;

                static const AudioSourceHint Sidecar;
                static const AudioSourceHint Embedded;
};

inline const AudioSourceHint AudioSourceHint::Sidecar{0};
inline const AudioSourceHint AudioSourceHint::Embedded{1};

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SrcQuality", "Sample Rate Conversion Quality", 1,
                                           {"SincBest", 0, "Best Sinc (Highest Quality)"},
                                           {"SincMedium", 1, "Medium Sinc (Balanced)"},
                                           {"SincFastest", 2, "Fastest Sinc (Low Latency)"},
                                           {"Linear", 3, "Linear Interpolation"},
                                           {"ZeroOrderHold", 4, "Zero-Order Hold (Nearest Sample)"}); // default: SincMedium

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ChannelRole", "Audio Channel Role", 0,
                                           {"Unused", 0, "Unused / Unassigned"},
                                           {"Mono", 1, "Mono"},
                                           {"FrontLeft", 2, "Front Left"},
                                           {"FrontRight", 3, "Front Right"},
                                           {"FrontCenter", 4, "Front Center"},
                                           {"LFE", 5, "Low-Frequency Effects (LFE)"},
                                           {"BackLeft", 6, "Back Left"},
                                           {"BackRight", 7, "Back Right"},
                                           {"BackCenter", 8, "Back Center"},
                                           {"SideLeft", 9, "Side Left"},
                                           {"SideRight", 10, "Side Right"},
                                           {"FrontLeftOfCenter", 11, "Front Left of Center"},
                                           {"FrontRightOfCenter", 12, "Front Right of Center"},
                                           {"TopFrontLeft", 13, "Top Front Left"},
                                           {"TopFrontCenter", 14, "Top Front Center"},
                                           {"TopFrontRight", 15, "Top Front Right"},
                                           {"TopBackLeft", 16, "Top Back Left"},
                                           {"TopBackCenter", 17, "Top Back Center"},
                                           {"TopBackRight", 18, "Top Back Right"},
                                           {"TopCenter", 19, "Top Center"},
                                           {"AmbisonicW", 20, "Ambisonic W (Omnidirectional)"},
                                           {"AmbisonicX", 21, "Ambisonic X"},
                                           {"AmbisonicY", 22, "Ambisonic Y"},
                                           {"AmbisonicZ", 23, "Ambisonic Z"},
                                           {"Aux0", 24, "Auxiliary 0"},
                                           {"Aux1", 25, "Auxiliary 1"},
                                           {"Aux2", 26, "Auxiliary 2"},
                                           {"Aux3", 27, "Auxiliary 3"},
                                           {"Aux4", 28, "Auxiliary 4"},
                                           {"Aux5", 29, "Auxiliary 5"},
                                           {"Aux6", 30, "Auxiliary 6"},
                                           {"Aux7", 31, "Auxiliary 7"},
                                           {"LeftTotal", 32, "Left Total (Lt, Matrix Stereo)"},
                                           {"RightTotal", 33, "Right Total (Rt, Matrix Stereo)"});

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("OpusApplication", "Opus Application Mode", 1,
                                           {"Voip", 0, "VoIP (Voice Optimized)"},
                                           {"Audio", 1, "Audio (Music / General)"},
                                           {"LowDelay", 2, "Restricted Low Delay"}); // default: Audio

                using TypedEnum<OpusApplication>::TypedEnum;

                static const OpusApplication Voip;
                static const OpusApplication Audio;
                static const OpusApplication LowDelay;
};

inline const OpusApplication OpusApplication::Voip{0};
inline const OpusApplication OpusApplication::Audio{1};
inline const OpusApplication OpusApplication::LowDelay{2};

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
