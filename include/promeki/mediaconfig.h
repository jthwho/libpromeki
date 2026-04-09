/**
 * @file      mediaconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Phantom tag for the shared media config @ref StringRegistry.
 * @ingroup media
 *
 * Every class that consumes configuration in the media subsystem shares
 * this tag so a single key name resolves to the same @ref MediaConfig::ID
 * everywhere: @ref MediaIO backends, @ref Image::convert,
 * @ref CSCPipeline, and any future media component that exposes knobs.
 */
struct MediaConfigTag {};

/**
 * @brief Generic name/value configuration container for the media subsystem.
 * @ingroup media
 *
 * Thin subclass of @ref VariantDatabase "VariantDatabase<MediaConfigTag>"
 * that adds a canonical catalog of well-known @ref ID constants for every
 * configurable knob in libpromeki.  All media components — @ref MediaIO
 * backends, @ref Image::convert, @ref CSCPipeline — share this single
 * registry so:
 *
 * - A key set at one layer can be forwarded to another without any
 *   translation or key mapping.
 * - There is exactly one place to look up the string name, default
 *   value type, and semantic of a knob.
 * - New callers get autocomplete and a compile-time spelling check on
 *   every key they touch.
 *
 * Follows the same layout as @ref Metadata: a class inheriting from
 * @ref VariantDatabase so standard key IDs can live as
 * @c static @c inline @c const members while instance methods
 * (@c set / @c get / @c contains / @c merge / JSON + stream
 * serialization) are inherited from the base unchanged.
 *
 * @par Example
 * @code
 * MediaConfig cfg;
 * cfg.set(MediaConfig::Filename,     String("/tmp/clip.mov"));
 * cfg.set(MediaConfig::VideoSize,    Size2Du32(1920, 1080));
 * cfg.set(MediaConfig::JpegQuality,  95);
 * cfg.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV444);
 *
 * Image jpeg = rgb.convert(PixelDesc(PixelDesc::JPEG_RGB8_sRGB),
 *                          rgb.metadata(), cfg);
 * @endcode
 */
class MediaConfig : public VariantDatabase<MediaConfigTag> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<MediaConfigTag>;

                using Base::Base;

                // ============================================================
                // Common / core
                // ============================================================

                /// @brief String — path or URL to the media resource.
                static inline const ID Filename{"Filename"};
                /// @brief String — registered backend type name (e.g. "TPG", "ImageFile").
                static inline const ID Type{"Type"};
                /// @brief FrameRate — stream or target frame rate.
                static inline const ID FrameRate{"FrameRate"};

                // ============================================================
                // Video — shared across backends
                // ============================================================

                /// @brief bool — enable video generation / decode.
                static inline const ID VideoEnabled{"VideoEnabled"};
                /// @brief Size2Du32 — image dimensions.
                static inline const ID VideoSize{"VideoSize"};
                /// @brief PixelDesc — stage video pixel description (target format for
                /// generators, hint for headerless readers).
                static inline const ID VideoPixelFormat{"VideoPixelFormat"};
                /// @brief int — 0-based video track index to use (-1 = auto).
                static inline const ID VideoTrack{"VideoTrack"};

                // ============================================================
                // Video test pattern generator
                // ============================================================

                /// @brief Enum @ref VideoPattern — selected test pattern.
                static inline const ID VideoPattern{"VideoPattern"};
                /// @brief Color — fill color for @c SolidColor pattern.
                static inline const ID VideoSolidColor{"VideoSolidColor"};
                /// @brief double — horizontal motion pixels/frame.
                static inline const ID VideoMotion{"VideoMotion"};

                // ============================================================
                // Video burn-in overlay
                // ============================================================

                /// @brief bool — enable text burn-in.
                static inline const ID VideoBurnEnabled{"VideoBurnEnabled"};
                /// @brief String — TrueType / OpenType font path for burn-in.
                static inline const ID VideoBurnFontPath{"VideoBurnFontPath"};
                /// @brief int — burn-in font size in pixels.
                static inline const ID VideoBurnFontSize{"VideoBurnFontSize"};
                /// @brief String — static burn-in text (auto when empty).
                static inline const ID VideoBurnText{"VideoBurnText"};
                /// @brief Enum @ref BurnPosition — on-screen position.
                static inline const ID VideoBurnPosition{"VideoBurnPosition"};
                /// @brief Color — burn-in text color.
                static inline const ID VideoBurnTextColor{"VideoBurnTextColor"};
                /// @brief Color — burn-in background color.
                static inline const ID VideoBurnBgColor{"VideoBurnBgColor"};
                /// @brief bool — draw background rectangle behind burn-in text.
                static inline const ID VideoBurnDrawBg{"VideoBurnDrawBg"};

                // ============================================================
                // Audio — shared across backends
                // ============================================================

                /// @brief bool — enable audio generation / decode.
                static inline const ID AudioEnabled{"AudioEnabled"};
                /// @brief float — audio sample rate in Hz.
                static inline const ID AudioRate{"AudioRate"};
                /// @brief int — audio channel count.
                static inline const ID AudioChannels{"AudioChannels"};
                /// @brief int — 0-based audio track index to use (-1 = auto).
                static inline const ID AudioTrack{"AudioTrack"};

                // ============================================================
                // Audio test pattern generator
                // ============================================================

                /// @brief Enum @ref AudioPattern — selected audio mode.
                static inline const ID AudioMode{"AudioMode"};
                /// @brief double — tone frequency in Hz.
                static inline const ID AudioToneFrequency{"AudioToneFrequency"};
                /// @brief double — tone level in dBFS.
                static inline const ID AudioToneLevel{"AudioToneLevel"};
                /// @brief double — LTC burn-in level in dBFS.
                static inline const ID AudioLtcLevel{"AudioLtcLevel"};
                /// @brief int — LTC channel index (-1 = all).
                static inline const ID AudioLtcChannel{"AudioLtcChannel"};

                // ============================================================
                // Timecode
                // ============================================================

                /// @brief bool — enable timecode generation.
                static inline const ID TimecodeEnabled{"TimecodeEnabled"};
                /// @brief String — starting timecode (SMPTE "HH:MM:SS:FF" form).
                static inline const ID TimecodeStart{"TimecodeStart"};
                /// @brief Timecode — pre-built starting timecode (alternative to @c TimecodeStart).
                static inline const ID TimecodeValue{"TimecodeValue"};
                /// @brief bool — drop-frame flag for 29.97 / 59.94 timecode.
                static inline const ID TimecodeDropFrame{"TimecodeDropFrame"};

                // ============================================================
                // Converter (MediaIOTask_Converter)
                // ============================================================

                /// @brief PixelDesc — target pixel description for the converter
                /// stage (@c Invalid = video pass-through).
                static inline const ID OutputPixelDesc{"OutputPixelDesc"};
                /// @brief Enum @ref AudioDataType — target audio sample format
                /// (@c Invalid = audio pass-through).
                static inline const ID OutputAudioDataType{"OutputAudioDataType"};
                /// @brief int — internal FIFO capacity in frames.
                static inline const ID Capacity{"Capacity"};

                // ============================================================
                // JPEG codec
                // ============================================================

                /// @brief int — JPEG quality 1-100 (codec default: 85).
                static inline const ID JpegQuality{"JpegQuality"};
                /// @brief Enum @ref ChromaSubsampling — JPEG chroma subsampling
                /// (codec default: 4:2:2, RFC 2435 compatible).
                static inline const ID JpegSubsampling{"JpegSubsampling"};

                // ============================================================
                // CSC pipeline
                // ============================================================

                /// @brief Enum @ref CscPath — CSC processing path selection
                /// (@c Optimized default, @c Scalar for debug / reference).
                static inline const ID CscPath{"CscPath"};

                // ============================================================
                // Image file sequence (MediaIOTask_ImageFile)
                // ============================================================

                /// @brief int — explicit @ref ImageFile::ID, bypasses extension probe.
                static inline const ID ImageFileID{"ImageFileID"};
                /// @brief int — first frame index for a sequence writer.
                static inline const ID SequenceHead{"SequenceHead"};

                // ============================================================
                // QuickTime / ISO-BMFF (MediaIOTask_QuickTime)
                // ============================================================

                /// @brief Enum QuickTimeLayout — writer on-disk layout.
                static inline const ID QuickTimeLayout{"QuickTimeLayout"};
                /// @brief int — video frames per fragment (fragmented writer).
                static inline const ID QuickTimeFragmentFrames{"QuickTimeFragmentFrames"};
                /// @brief bool — call @c fdatasync after each flush.
                static inline const ID QuickTimeFlushSync{"QuickTimeFlushSync"};

                // ============================================================
                // SDL display sink (mediaplay)
                // ============================================================

                /// @brief bool — pace video to real time (false = run as fast as possible).
                static inline const ID SdlPaced{"SdlPaced"};
                /// @brief bool — open audio output alongside the video window.
                static inline const ID SdlAudioEnabled{"SdlAudioEnabled"};
                /// @brief Size2Du32 — initial SDL window size.
                static inline const ID SdlWindowSize{"SdlWindowSize"};
                /// @brief String — SDL window title bar text.
                static inline const ID SdlWindowTitle{"SdlWindowTitle"};

                // ============================================================
                // RTP sink (MediaIOTask_Rtp)
                //
                // Media descriptor keys (VideoSize, VideoPixelFormat,
                // AudioRate, AudioChannels, FrameRate, etc.) are
                // reused from the sections above.  The keys below are
                // specifically the RTP transport and per-stream
                // endpoint plumbing.  An empty / null destination on
                // a given stream means that stream is not
                // transmitted.
                // ============================================================

                // --- Transport-global ---
                /// @brief SocketAddress — local bind address for all RTP streams in this sink.
                static inline const ID RtpLocalAddress{"RtpLocalAddress"};
                /// @brief String — SDP @c s= line (session name).
                static inline const ID RtpSessionName{"RtpSessionName"};
                /// @brief String — SDP @c o= originator username.
                static inline const ID RtpSessionOrigin{"RtpSessionOrigin"};
                /// @brief Enum @ref RtpPacingMode — pacing mechanism used for all streams.
                static inline const ID RtpPacingMode{"RtpPacingMode"};
                /// @brief int — multicast TTL applied to the transport.
                static inline const ID RtpMulticastTTL{"RtpMulticastTTL"};
                /// @brief String — multicast outgoing interface name (empty = default).
                static inline const ID RtpMulticastInterface{"RtpMulticastInterface"};
                /// @brief String — if non-empty, the MediaIO opens this file and
                /// writes the generated SDP session description to it at open time.
                static inline const ID RtpSaveSdpPath{"RtpSaveSdpPath"};

                // --- Video stream ---
                /// @brief SocketAddress — destination for the video stream. Empty = disabled.
                static inline const ID VideoRtpDestination{"VideoRtpDestination"};
                /// @brief int — RTP payload type (0-127).
                static inline const ID VideoRtpPayloadType{"VideoRtpPayloadType"};
                /// @brief int — RTP timestamp clock rate in Hz (default 90000).
                static inline const ID VideoRtpClockRate{"VideoRtpClockRate"};
                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID VideoRtpSsrc{"VideoRtpSsrc"};
                /// @brief int — DSCP marking for the video stream (default 46 / EF).
                static inline const ID VideoRtpDscp{"VideoRtpDscp"};
                /// @brief int — target bitrate in bits/sec (0 = compute from descriptor).
                static inline const ID VideoRtpTargetBitrate{"VideoRtpTargetBitrate"};

                // --- Audio stream ---
                /// @brief SocketAddress — destination for the audio stream. Empty = disabled.
                static inline const ID AudioRtpDestination{"AudioRtpDestination"};
                /// @brief int — RTP payload type (0-127).
                static inline const ID AudioRtpPayloadType{"AudioRtpPayloadType"};
                /// @brief int — RTP clock rate in Hz (default matches @c AudioRate).
                static inline const ID AudioRtpClockRate{"AudioRtpClockRate"};
                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID AudioRtpSsrc{"AudioRtpSsrc"};
                /// @brief int — DSCP marking for the audio stream (default 34 / AF41).
                static inline const ID AudioRtpDscp{"AudioRtpDscp"};
                /// @brief int — packet time in microseconds (AES67 default 1000).
                static inline const ID AudioRtpPacketTimeUs{"AudioRtpPacketTimeUs"};

                // --- Data / metadata stream ---
                /// @brief bool — enable transmission of per-frame Metadata.
                static inline const ID DataEnabled{"DataEnabled"};
                /// @brief SocketAddress — destination for the metadata stream. Empty = disabled.
                static inline const ID DataRtpDestination{"DataRtpDestination"};
                /// @brief int — RTP payload type (0-127).
                static inline const ID DataRtpPayloadType{"DataRtpPayloadType"};
                /// @brief int — RTP clock rate in Hz (default 90000).
                static inline const ID DataRtpClockRate{"DataRtpClockRate"};
                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID DataRtpSsrc{"DataRtpSsrc"};
                /// @brief int — DSCP marking for the metadata stream.
                static inline const ID DataRtpDscp{"DataRtpDscp"};
                /// @brief Enum @ref MetadataRtpFormat — wire format for the metadata stream.
                static inline const ID DataRtpFormat{"DataRtpFormat"};
};

/**
 * @brief Strongly-typed key into a @ref MediaConfig.
 * @ingroup media
 *
 * Convenience alias so call sites that want to declare their own extra
 * keys (e.g. a user-defined converter subclass) can say
 * @c MediaConfigID{"MyKey"} without having to spell out the full
 * nested type.
 */
using MediaConfigID = MediaConfig::ID;

PROMEKI_NAMESPACE_END
