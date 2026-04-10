/**
 * @file      mediaconfig.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>
#include <promeki/enums.h>

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
 * Each ID is declared via @ref declareID with a mandatory @ref VariantSpec
 * that captures the accepted type(s), default value, numeric range, and
 * human-readable description.  Use @ref spec to query the spec for any ID.
 *
 * @par Example
 * @code
 * MediaConfig cfg;
 * cfg.set(MediaConfig::Filename,     String("/tmp/clip.mov"));
 * cfg.set(MediaConfig::VideoSize,    Size2Du32(1920, 1080));
 * cfg.set(MediaConfig::JpegQuality,  95);
 * cfg.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV444);
 *
 * // Introspect a key
 * const VariantSpec *s = MediaConfig::spec(MediaConfig::JpegQuality);
 * // s->description()  => "JPEG quality 1-100."
 * // s->rangeMin()     => 1
 * // s->rangeMax()     => 100
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
                static inline const ID Filename = declareID("Filename",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Path or URL to the media resource."));

                /// @brief String — registered backend type name (e.g. "TPG", "ImageFile").
                static inline const ID Type = declareID("Type",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Registered backend type name."));

                /// @brief FrameRate — stream or target frame rate.
                static inline const ID FrameRate = declareID("FrameRate",
                        VariantSpec().setType(Variant::TypeFrameRate)
                                .setDefault(promeki::FrameRate())
                                .setDescription("Stream or target frame rate."));

                // ============================================================
                // Video — shared across backends
                // ============================================================

                /// @brief bool — enable video generation / decode.
                static inline const ID VideoEnabled = declareID("VideoEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable video generation or decode."));

                /// @brief Size2Du32 — image dimensions.
                static inline const ID VideoSize = declareID("VideoSize",
                        VariantSpec().setType(Variant::TypeSize2D)
                                .setDefault(Size2Du32())
                                .setDescription("Image dimensions."));

                /// @brief PixelDesc — stage video pixel description (target format for
                /// generators, hint for headerless readers).
                static inline const ID VideoPixelFormat = declareID("VideoPixelFormat",
                        VariantSpec().setType(Variant::TypePixelDesc)
                                .setDefault(PixelDesc())
                                .setDescription("Video pixel description."));

                /// @brief int — 0-based video track index to use (-1 = auto).
                static inline const ID VideoTrack = declareID("VideoTrack",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setMin(int32_t(-1))
                                .setDescription("0-based video track index (-1 = auto)."));

                // ============================================================
                // Video test pattern generator
                // ============================================================

                /// @brief Enum @ref VideoPattern — selected test pattern.
                static inline const ID VideoPattern = declareID("VideoPattern",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoPattern::ColorBars)
                                .setEnumType(promeki::VideoPattern::Type)
                                .setDescription("Selected video test pattern."));

                /// @brief Color — fill color for @c SolidColor pattern.
                static inline const ID VideoSolidColor = declareID("VideoSolidColor",
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Fill color for the SolidColor pattern."));

                /// @brief double — horizontal motion pixels/frame.
                static inline const ID VideoMotion = declareID("VideoMotion",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.0)
                                .setDescription("Horizontal motion in pixels per frame."));

                // ============================================================
                // Video burn-in overlay
                // ============================================================

                /// @brief bool — enable text burn-in.
                static inline const ID VideoBurnEnabled = declareID("VideoBurnEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable text burn-in overlay."));

                /// @brief String — TrueType / OpenType font path for burn-in.
                static inline const ID VideoBurnFontPath = declareID("VideoBurnFontPath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("TrueType or OpenType font path for burn-in."));

                /// @brief int — burn-in font size in pixels.
                static inline const ID VideoBurnFontSize = declareID("VideoBurnFontSize",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Burn-in font size in pixels."));

                /// @brief String — static burn-in text (auto when empty).
                static inline const ID VideoBurnText = declareID("VideoBurnText",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Static burn-in text (auto when empty)."));

                /// @brief Enum @ref BurnPosition — on-screen position.
                static inline const ID VideoBurnPosition = declareID("VideoBurnPosition",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(BurnPosition::BottomCenter)
                                .setEnumType(BurnPosition::Type)
                                .setDescription("On-screen burn-in text position."));

                /// @brief Color — burn-in text color.
                static inline const ID VideoBurnTextColor = declareID("VideoBurnTextColor",
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Burn-in text color."));

                /// @brief Color — burn-in background color.
                static inline const ID VideoBurnBgColor = declareID("VideoBurnBgColor",
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Burn-in background color."));

                /// @brief bool — draw background rectangle behind burn-in text.
                static inline const ID VideoBurnDrawBg = declareID("VideoBurnDrawBg",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Draw background rectangle behind burn-in text."));

                // ============================================================
                // Audio — shared across backends
                // ============================================================

                /// @brief bool — enable audio generation / decode.
                static inline const ID AudioEnabled = declareID("AudioEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable audio generation or decode."));

                /// @brief float — audio sample rate in Hz.
                static inline const ID AudioRate = declareID("AudioRate",
                        VariantSpec().setType(Variant::TypeFloat)
                                .setDefault(0.0f)
                                .setMin(0.0f)
                                .setDescription("Audio sample rate in Hz."));

                /// @brief int — audio channel count.
                static inline const ID AudioChannels = declareID("AudioChannels",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio channel count."));

                /// @brief int — 0-based audio track index to use (-1 = auto).
                static inline const ID AudioTrack = declareID("AudioTrack",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setMin(int32_t(-1))
                                .setDescription("0-based audio track index (-1 = auto)."));

                // ============================================================
                // Audio test pattern generator
                // ============================================================

                /// @brief Enum @ref AudioPattern — selected audio mode.
                static inline const ID AudioMode = declareID("AudioMode",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(AudioPattern::Tone)
                                .setEnumType(AudioPattern::Type)
                                .setDescription("Selected audio test pattern."));

                /// @brief double — tone frequency in Hz.
                static inline const ID AudioToneFrequency = declareID("AudioToneFrequency",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1000.0)
                                .setMin(0.0)
                                .setDescription("Tone frequency in Hz."));

                /// @brief double — tone level in dBFS.
                static inline const ID AudioToneLevel = declareID("AudioToneLevel",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(-20.0)
                                .setMax(0.0)
                                .setDescription("Tone level in dBFS."));

                /// @brief double — LTC burn-in level in dBFS.
                static inline const ID AudioLtcLevel = declareID("AudioLtcLevel",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(-20.0)
                                .setMax(0.0)
                                .setDescription("LTC burn-in level in dBFS."));

                /// @brief int — LTC channel index (-1 = all).
                static inline const ID AudioLtcChannel = declareID("AudioLtcChannel",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setMin(int32_t(-1))
                                .setDescription("LTC channel index (-1 = all)."));

                // ============================================================
                // Timecode
                // ============================================================

                /// @brief bool — enable timecode generation.
                static inline const ID TimecodeEnabled = declareID("TimecodeEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable timecode generation."));

                /// @brief String — starting timecode (SMPTE "HH:MM:SS:FF" form).
                static inline const ID TimecodeStart = declareID("TimecodeStart",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Starting timecode in SMPTE HH:MM:SS:FF form."));

                /// @brief Timecode — pre-built starting timecode (alternative to @c TimecodeStart).
                static inline const ID TimecodeValue = declareID("TimecodeValue",
                        VariantSpec().setType(Variant::TypeTimecode)
                                .setDefault(Timecode())
                                .setDescription("Pre-built starting timecode."));

                /// @brief bool — drop-frame flag for 29.97 / 59.94 timecode.
                static inline const ID TimecodeDropFrame = declareID("TimecodeDropFrame",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Drop-frame flag for 29.97 / 59.94 timecode."));

                // ============================================================
                // Converter (MediaIOTask_Converter)
                // ============================================================

                /// @brief PixelDesc — target pixel description for the converter
                /// stage (@c Invalid = video pass-through).
                static inline const ID OutputPixelDesc = declareID("OutputPixelDesc",
                        VariantSpec().setType(Variant::TypePixelDesc)
                                .setDefault(PixelDesc())
                                .setDescription("Target pixel description (Invalid = pass-through)."));

                /// @brief Enum @ref AudioDataType — target audio sample format
                /// (@c Invalid = audio pass-through).
                static inline const ID OutputAudioDataType = declareID("OutputAudioDataType",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(AudioDataType::Type)
                                .setDescription("Target audio sample format (Invalid = pass-through)."));

                /// @brief int — internal FIFO capacity in frames.
                static inline const ID Capacity = declareID("Capacity",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Internal FIFO capacity in frames."));

                // ============================================================
                // JPEG codec
                // ============================================================

                /// @brief int — JPEG quality 1-100 (codec default: 85).
                static inline const ID JpegQuality = declareID("JpegQuality",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(85))
                                .setRange(int32_t(1), int32_t(100))
                                .setDescription("JPEG quality 1-100."));

                /// @brief Enum @ref ChromaSubsampling — JPEG chroma subsampling
                /// (codec default: 4:2:2, RFC 2435 compatible).
                static inline const ID JpegSubsampling = declareID("JpegSubsampling",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(ChromaSubsampling::YUV422)
                                .setEnumType(ChromaSubsampling::Type)
                                .setDescription("JPEG chroma subsampling."));

                // ============================================================
                // JPEG XS codec
                // ============================================================

                /// @brief int (or float) — JPEG XS target bits per pixel.  JPEG XS is
                /// constant-bitrate; typical broadcast values are 2-6 bpp for
                /// visually-lossless contribution.  Codec default: 3.
                static inline const ID JpegXsBpp = declareID("JpegXsBpp",
                        VariantSpec().setTypes({Variant::TypeS32, Variant::TypeFloat, Variant::TypeDouble})
                                .setDefault(int32_t(3))
                                .setMin(1)
                                .setDescription("JPEG XS target bits per pixel."));

                /// @brief int — JPEG XS horizontal decomposition depth (0-5,
                /// codec default: 5).  Higher values trade encode cost for
                /// quality.  See @c ndecomp_h in SvtJpegxsEnc.h.
                static inline const ID JpegXsDecomposition = declareID("JpegXsDecomposition",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(5))
                                .setRange(int32_t(0), int32_t(5))
                                .setDescription("JPEG XS horizontal decomposition depth 0-5."));

                // ============================================================
                // CSC pipeline
                // ============================================================

                /// @brief Enum @ref CscPath — CSC processing path selection
                /// (@c Optimized default, @c Scalar for debug / reference).
                static inline const ID CscPath = declareID("CscPath",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::CscPath::Optimized)
                                .setEnumType(promeki::CscPath::Type)
                                .setDescription("CSC processing path (Optimized or Scalar)."));

                // ============================================================
                // Image file sequence (MediaIOTask_ImageFile)
                // ============================================================

                /// @brief int — explicit @ref ImageFile::ID, bypasses extension probe.
                static inline const ID ImageFileID = declareID("ImageFileID",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Explicit ImageFile ID (0 = infer from extension)."));

                /// @brief int — first frame index for a sequence writer.
                static inline const ID SequenceHead = declareID("SequenceHead",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("First frame index for a sequence writer."));

                /// @brief String — if non-empty, the backend writes an @c .imgseq
                /// JSON sidecar to this path when the sequence writer closes.
                static inline const ID SaveImgSeqPath = declareID("SaveImgSeqPath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Path to write .imgseq JSON sidecar."));

                /// @brief Enum @ref ImgSeqPathMode — whether the sidecar's
                /// directory reference is relative (to the sidecar) or absolute.
                static inline const ID SaveImgSeqPathMode = declareID("SaveImgSeqPathMode",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(ImgSeqPathMode::Relative)
                                .setEnumType(ImgSeqPathMode::Type)
                                .setDescription("Sidecar directory reference mode (Relative or Absolute)."));

                // ============================================================
                // QuickTime / ISO-BMFF (MediaIOTask_QuickTime)
                // ============================================================

                /// @brief Enum QuickTimeLayout — writer on-disk layout.
                static inline const ID QuickTimeLayout = declareID("QuickTimeLayout",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::QuickTimeLayout::Fragmented)
                                .setEnumType(promeki::QuickTimeLayout::Type)
                                .setDescription("QuickTime writer on-disk layout."));

                /// @brief int — video frames per fragment (fragmented writer).
                static inline const ID QuickTimeFragmentFrames = declareID("QuickTimeFragmentFrames",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video frames per fragment (fragmented writer)."));

                /// @brief bool — call @c fdatasync after each flush.
                static inline const ID QuickTimeFlushSync = declareID("QuickTimeFlushSync",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Call fdatasync after each flush."));

                // ============================================================
                // SDL display sink (mediaplay)
                // ============================================================

                /// @brief bool — pace video to real time (false = run as fast as possible).
                static inline const ID SdlPaced = declareID("SdlPaced",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Pace video to real time."));

                /// @brief bool — open audio output alongside the video window.
                static inline const ID SdlAudioEnabled = declareID("SdlAudioEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Open audio output alongside the video window."));

                /// @brief Size2Du32 — initial SDL window size.
                static inline const ID SdlWindowSize = declareID("SdlWindowSize",
                        VariantSpec().setType(Variant::TypeSize2D)
                                .setDefault(Size2Du32())
                                .setDescription("Initial SDL window size."));

                /// @brief String — SDL window title bar text.
                static inline const ID SdlWindowTitle = declareID("SdlWindowTitle",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDL window title bar text."));

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
                static inline const ID RtpLocalAddress = declareID("RtpLocalAddress",
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Local bind address for all RTP streams."));

                /// @brief String — SDP @c s= line (session name).
                static inline const ID RtpSessionName = declareID("RtpSessionName",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDP session name (s= line)."));

                /// @brief String — SDP @c o= originator username.
                static inline const ID RtpSessionOrigin = declareID("RtpSessionOrigin",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDP originator username (o= line)."));

                /// @brief Enum @ref RtpPacingMode — pacing mechanism used for all streams.
                static inline const ID RtpPacingMode = declareID("RtpPacingMode",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::RtpPacingMode::Auto)
                                .setEnumType(promeki::RtpPacingMode::Type)
                                .setDescription("RTP pacing mechanism."));

                /// @brief int — multicast TTL applied to the transport.
                static inline const ID RtpMulticastTTL = declareID("RtpMulticastTTL",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(16))
                                .setRange(int32_t(1), int32_t(255))
                                .setDescription("Multicast TTL."));

                /// @brief String — multicast outgoing interface name (empty = default).
                static inline const ID RtpMulticastInterface = declareID("RtpMulticastInterface",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Multicast outgoing interface name."));

                /// @brief String — if non-empty, the MediaIO opens this file and
                /// writes the generated SDP session description to it at open time.
                static inline const ID RtpSaveSdpPath = declareID("RtpSaveSdpPath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("File path to write generated SDP to."));

                /// @brief Polymorphic reader-side SDP input.  Accepts either:
                /// - @c String: interpreted as a filesystem path.
                /// - @ref SdpSession: consumed directly, no filesystem access.
                static inline const ID RtpSdp = declareID("RtpSdp",
                        VariantSpec().setTypes({Variant::TypeString, Variant::TypeSdpSession})
                                .setDescription("SDP input: file path (String) or session object (SdpSession)."));

                /// @brief int — reader-side jitter buffer depth in milliseconds.
                static inline const ID RtpJitterMs = declareID("RtpJitterMs",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(50))
                                .setMin(int32_t(0))
                                .setDescription("Reader jitter buffer depth in ms."));

                /// @brief int — reader-side output frame queue capacity.
                static inline const ID RtpMaxReadQueueDepth = declareID("RtpMaxReadQueueDepth",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(4))
                                .setMin(int32_t(1))
                                .setDescription("Reader output frame queue capacity."));

                // --- Video stream ---

                /// @brief SocketAddress — destination for the video stream. Empty = disabled.
                static inline const ID VideoRtpDestination = declareID("VideoRtpDestination",
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the video RTP stream."));

                /// @brief int — RTP payload type (0-127).
                static inline const ID VideoRtpPayloadType = declareID("VideoRtpPayloadType",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(96))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Video RTP payload type."));

                /// @brief int — RTP timestamp clock rate in Hz (default 90000).
                static inline const ID VideoRtpClockRate = declareID("VideoRtpClockRate",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(90000))
                                .setMin(int32_t(1))
                                .setDescription("Video RTP timestamp clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID VideoRtpSsrc = declareID("VideoRtpSsrc",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the video stream (default 46 / EF).
                static inline const ID VideoRtpDscp = declareID("VideoRtpDscp",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(46))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Video RTP DSCP marking."));

                /// @brief int — target bitrate in bits/sec (0 = compute from descriptor).
                static inline const ID VideoRtpTargetBitrate = declareID("VideoRtpTargetBitrate",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video RTP target bitrate in bps (0 = auto)."));

                /// @brief String — raw @c a=fmtp value from the SDP for the video stream.
                static inline const ID VideoRtpFmtp = declareID("VideoRtpFmtp",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Raw SDP a=fmtp value for the video stream."));

                // --- Audio stream ---

                /// @brief SocketAddress — destination for the audio stream. Empty = disabled.
                static inline const ID AudioRtpDestination = declareID("AudioRtpDestination",
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the audio RTP stream."));

                /// @brief int — RTP payload type (0-127).
                static inline const ID AudioRtpPayloadType = declareID("AudioRtpPayloadType",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(97))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Audio RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default matches @c AudioRate).
                static inline const ID AudioRtpClockRate = declareID("AudioRtpClockRate",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio RTP clock rate in Hz (0 = match AudioRate)."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID AudioRtpSsrc = declareID("AudioRtpSsrc",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the audio stream (default 34 / AF41).
                static inline const ID AudioRtpDscp = declareID("AudioRtpDscp",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(34))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Audio RTP DSCP marking."));

                /// @brief int — packet time in microseconds (AES67 default 1000).
                static inline const ID AudioRtpPacketTimeUs = declareID("AudioRtpPacketTimeUs",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(1000))
                                .setMin(int32_t(1))
                                .setDescription("Audio RTP packet time in microseconds."));

                // --- Data / metadata stream ---

                /// @brief bool — enable transmission of per-frame Metadata.
                static inline const ID DataEnabled = declareID("DataEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable per-frame metadata transmission."));

                /// @brief SocketAddress — destination for the metadata stream. Empty = disabled.
                static inline const ID DataRtpDestination = declareID("DataRtpDestination",
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the metadata RTP stream."));

                /// @brief int — RTP payload type (0-127).
                static inline const ID DataRtpPayloadType = declareID("DataRtpPayloadType",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(100))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Metadata RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default 90000).
                static inline const ID DataRtpClockRate = declareID("DataRtpClockRate",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(90000))
                                .setMin(int32_t(1))
                                .setDescription("Metadata RTP clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                static inline const ID DataRtpSsrc = declareID("DataRtpSsrc",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Metadata RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the metadata stream.
                static inline const ID DataRtpDscp = declareID("DataRtpDscp",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(34))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Metadata RTP DSCP marking."));

                /// @brief Enum @ref MetadataRtpFormat — wire format for the metadata stream.
                static inline const ID DataRtpFormat = declareID("DataRtpFormat",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(MetadataRtpFormat::JsonMetadata)
                                .setEnumType(MetadataRtpFormat::Type)
                                .setDescription("Wire format for the metadata RTP stream."));
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
