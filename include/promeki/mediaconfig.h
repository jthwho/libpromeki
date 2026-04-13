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
#include <promeki/enumlist.h>
#include <promeki/uuid.h>

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

                /// @brief String — human-readable instance name (used in logs, spawned thread names,
                /// benchmark stamp IDs). Defaults to `"media<localId>"` when left empty.
                static inline const ID Name = declareID("Name",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Human-readable instance name; defaults to "
                                        "\"media<localId>\" when empty."));

                /// @brief UUID — globally-unique instance identifier used for cross-process
                /// pipeline correlation. Defaults to a freshly generated UUID when left invalid.
                static inline const ID Uuid = declareID("Uuid",
                        VariantSpec().setType(Variant::TypeUUID)
                                .setDefault(UUID())
                                .setDescription("Globally-unique instance identifier; defaults "
                                        "to a fresh UUID when invalid."));

                /// @brief bool — opt into per-frame Benchmark stamping in the MediaIO base class.
                /// When true, every frame flowing through this MediaIO receives stamps at
                /// enqueue / dequeue / taskBegin / taskEnd, aggregated by an attached
                /// BenchmarkReporter when the stage is a sink.
                static inline const ID EnableBenchmark = declareID("EnableBenchmark",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable per-frame Benchmark stamping in the "
                                        "MediaIO base class."));

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

                /// @brief EnumList @ref AudioPattern — per-channel test
                ///        patterns.  Channels beyond the end of the
                ///        list are silenced.
                static inline const ID AudioChannelModes = declareID("AudioChannelModes",
                        VariantSpec().setType(Variant::TypeEnumList)
                                .setDefault(EnumList::forType<AudioPattern>())
                                .setEnumType(AudioPattern::Type)
                                .setDescription("Comma-separated list of per-channel audio test "
                                        "patterns (extra channels silenced)."));

                /// @brief double — tone frequency in Hz (used by Tone / AvSync).
                static inline const ID AudioToneFrequency = declareID("AudioToneFrequency",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1000.0)
                                .setMin(0.0)
                                .setDescription("Tone frequency in Hz (Tone / AvSync channels)."));

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

                /// @brief double — ChannelId base frequency in Hz.
                ///        Channel @em N carries a sine at
                ///        `base + N * step`.
                static inline const ID AudioChannelIdBaseFreq = declareID("AudioChannelIdBaseFreq",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1000.0)
                                .setMin(0.0)
                                .setDescription("ChannelId base tone frequency in Hz."));

                /// @brief double — ChannelId per-channel step in Hz.
                static inline const ID AudioChannelIdStepFreq = declareID("AudioChannelIdStepFreq",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(100.0)
                                .setMin(0.0)
                                .setDescription("ChannelId per-channel tone step in Hz."));

                /// @brief double — Chirp sweep start frequency in Hz.
                static inline const ID AudioChirpStartFreq = declareID("AudioChirpStartFreq",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(20.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep start frequency in Hz."));

                /// @brief double — Chirp sweep end frequency in Hz.
                static inline const ID AudioChirpEndFreq = declareID("AudioChirpEndFreq",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(20000.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep end frequency in Hz."));

                /// @brief double — Chirp sweep period in seconds.
                static inline const ID AudioChirpDurationSec = declareID("AudioChirpDurationSec",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep period in seconds."));

                /// @brief double — DualTone low-side frequency in Hz.
                static inline const ID AudioDualToneFreq1 = declareID("AudioDualToneFreq1",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(60.0)
                                .setMin(0.0)
                                .setDescription("DualTone low-side frequency in Hz (SMPTE IMD default 60 Hz)."));

                /// @brief double — DualTone high-side frequency in Hz.
                static inline const ID AudioDualToneFreq2 = declareID("AudioDualToneFreq2",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(7000.0)
                                .setMin(0.0)
                                .setDescription("DualTone high-side frequency in Hz (SMPTE IMD default 7 kHz)."));

                /// @brief double — DualTone amplitude ratio freq2 / freq1.
                static inline const ID AudioDualToneRatio = declareID("AudioDualToneRatio",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.25)
                                .setMin(0.0)
                                .setDescription("DualTone amplitude ratio of freq2 to freq1 "
                                        "(SMPTE IMD default 0.25 = 4:1)."));

                /// @brief double — WhiteNoise / PinkNoise buffer length in seconds.
                static inline const ID AudioNoiseBufferSec = declareID("AudioNoiseBufferSec",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(10.0)
                                .setMin(0.0)
                                .setDescription("WhiteNoise / PinkNoise cached buffer length in seconds."));

                /// @brief uint32 — PRNG seed used to build the noise buffers.
                static inline const ID AudioNoiseSeed = declareID("AudioNoiseSeed",
                        VariantSpec().setType(Variant::TypeU32)
                                .setDefault(uint32_t(0x505244A4u))
                                .setDescription("WhiteNoise / PinkNoise PRNG seed."));

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
                // Image data encoder (VITC-style binary stamp on top of video)
                // ============================================================

                /// @brief uint32 — opaque per-stream identifier.  Combined with the
                /// rolling frame number into the frame-ID payload of the
                /// @ref ImageDataEncoder when @ref TpgDataEncoderEnabled is true.
                /// Defaults to 0; use any value the application finds convenient
                /// for cross-stream correlation.
                static inline const ID StreamID = declareID("StreamID",
                        VariantSpec().setType(Variant::TypeU32)
                                .setDefault(uint32_t(0))
                                .setDescription("Opaque per-stream identifier (uint32)."));

                /// @brief bool — enable the @ref ImageDataEncoder pass on TPG video
                /// frames.  When true (default), the TPG stamps two 64-bit
                /// payloads into the top of every generated video frame:
                /// (1) @c (StreamID << 32) | frameNumber, and
                /// (2) the @ref Timecode::toBcd64 BCD timecode word.
                static inline const ID TpgDataEncoderEnabled = declareID("TpgDataEncoderEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable VITC-style binary data encoder pass on TPG video."));

                /// @brief int — number of scan lines each @ref ImageDataEncoder item
                /// occupies in the encoded band.  The TPG emits two items, so the
                /// total stamped band height is @c 2 * TpgDataEncoderRepeatLines
                /// scan lines starting from the top of the image.  Default 16,
                /// which gives a comfortable read margin for a noisy decoder
                /// without consuming too much picture area.
                static inline const ID TpgDataEncoderRepeatLines = declareID("TpgDataEncoderRepeatLines",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(16))
                                .setMin(int32_t(1))
                                .setDescription("Scan lines per ImageDataEncoder item in TPG."));

                // ============================================================
                // Inspector sink (MediaIOTask_Inspector)
                // ============================================================

                /// @brief bool — drop incoming frames after running checks.  When
                /// true, the inspector behaves as a pure null sink for upstream
                /// pacing purposes; the per-frame events and accumulator stats
                /// are still produced as long as the corresponding decoders are
                /// enabled.
                static inline const ID InspectorDropFrames = declareID("InspectorDropFrames",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Inspector drops frames after checks (sink behaviour)."));

                /// @brief bool — decode the @ref ImageDataEncoder bands from the
                /// picture.  Required for the picture-side timecode and frame ID
                /// checks; auto-enabled when @ref InspectorCheckTcSync or
                /// @ref InspectorCheckContinuity is set.
                static inline const ID InspectorDecodeImageData = declareID("InspectorDecodeImageData",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Inspector decodes image-data bands from each frame."));

                /// @brief bool — decode LTC from the audio track.  Required for
                /// the LTC vs picture-TC drift check; auto-enabled when
                /// @ref InspectorCheckTcSync is set.
                static inline const ID InspectorDecodeLtc = declareID("InspectorDecodeLtc",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Inspector decodes LTC from each frame's audio."));

                /// @brief bool — compare the picture timecode against the audio
                /// LTC and report the per-frame offset, in audio samples, on
                /// every frame.  Implies @ref InspectorDecodeImageData and
                /// @ref InspectorDecodeLtc.
                ///
                /// In professional video workflows audio and video are locked
                /// to the same reference, so the offset is expected to be a
                /// fixed phase relationship and any *change* from one frame
                /// to the next is a real fault.  Combined with
                /// @ref InspectorSyncOffsetToleranceSamples, the inspector
                /// fires a discontinuity warning whenever the offset moves
                /// by more than the configured tolerance.
                static inline const ID InspectorCheckTcSync = declareID("InspectorCheckTcSync",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Inspector reports picture-TC vs LTC offset in samples."));

                /// @brief int — maximum allowed sample-to-sample change in the
                /// picture-vs-LTC sync offset before the inspector flags a
                /// discontinuity.  Default 0: any change is reported.  Set
                /// higher (e.g. 1, 2, ...) when the upstream encode/decode
                /// pair has a known small jitter that should not generate
                /// noise.  Only meaningful when @ref InspectorCheckTcSync
                /// is enabled.
                static inline const ID InspectorSyncOffsetToleranceSamples = declareID("InspectorSyncOffsetToleranceSamples",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription(
                                        "Max allowed sample-to-sample change in "
                                        "picture-vs-LTC sync offset before flagging "
                                        "a discontinuity (0 = any change)."));

                /// @brief bool — track frame number, timecode, and stream ID
                /// continuity from one frame to the next.  Any unexpected jump
                /// (skipped frame, repeated frame, stream-ID change, TC
                /// discontinuity) becomes a discontinuity record on the per-frame
                /// event with both the previous and current values.  Implies
                /// @ref InspectorDecodeImageData.
                static inline const ID InspectorCheckContinuity = declareID("InspectorCheckContinuity",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Inspector tracks TC / frame# / streamID continuity."));

                /// @brief int — scan lines per @ref ImageDataEncoder band.  Must
                /// match the encoder's @ref TpgDataEncoderRepeatLines so the
                /// inspector reads at the right band offsets.  Default 16.
                static inline const ID InspectorImageDataRepeatLines = declareID("InspectorImageDataRepeatLines",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(16))
                                .setMin(int32_t(1))
                                .setDescription("Scan lines per ImageDataDecoder band in Inspector."));

                /// @brief int — audio channel index that carries LTC.  Mirrors
                /// @ref AudioLtcChannel; default 0.
                static inline const ID InspectorLtcChannel = declareID("InspectorLtcChannel",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio channel index carrying LTC for the inspector."));

                /// @brief double — periodic-summary log interval in seconds (wall
                /// time, not media time).  Default 1.0.  Set to 0 to disable
                /// periodic logging entirely; per-frame events are still produced.
                static inline const ID InspectorLogIntervalSec = declareID("InspectorLogIntervalSec",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1.0)
                                .setMin(0.0)
                                .setDescription("Inspector periodic-summary log interval, seconds."));

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

                /// @brief bool — enable automatic @c .imgseq sidecar for image
                /// sequences.  When true (the default), the ImageFile backend
                /// writes a @c .imgseq sidecar alongside the image files when
                /// a sequence writer closes and auto-detects an existing
                /// sidecar on read.  The sidecar filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.imgseq").  Set to @c false to inhibit both
                /// behaviours.
                static inline const ID SaveImgSeqEnabled = declareID("SaveImgSeqEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable automatic .imgseq sidecar for image sequences."));

                /// @brief String — override path for the @c .imgseq sidecar.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.imgseq" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                static inline const ID SaveImgSeqPath = declareID("SaveImgSeqPath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Override path for the .imgseq sidecar."));

                /// @brief Enum @ref ImgSeqPathMode — whether the sidecar's
                /// directory reference is relative (to the sidecar) or absolute.
                static inline const ID SaveImgSeqPathMode = declareID("SaveImgSeqPathMode",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(ImgSeqPathMode::Relative)
                                .setEnumType(ImgSeqPathMode::Type)
                                .setDescription("Sidecar directory reference mode (Relative or Absolute)."));

                /// @brief bool — enable sidecar audio file alongside an image
                /// sequence.  When true (the default), the ImageFile backend
                /// writes a Broadcast WAV sidecar when audio data is present
                /// and auto-detects an existing sidecar on read.  Set to
                /// @c false to inhibit both behaviours.
                static inline const ID SidecarAudioEnabled = declareID("SidecarAudioEnabled",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable sidecar audio file for image sequences."));

                /// @brief String — override path for the sidecar audio file.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.wav" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                static inline const ID SidecarAudioPath = declareID("SidecarAudioPath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Override path for the sidecar audio file."));

                /// @brief Enum @ref AudioSourceHint — preferred audio source
                /// when reading an image sequence.  Selects between the
                /// sidecar audio file and embedded per-frame audio (e.g. DPX
                /// user-data blocks).  Acts as a hint: if the preferred
                /// source is unavailable, the backend falls back to the
                /// other.  Default is @c Sidecar.
                static inline const ID AudioSource = declareID("AudioSource",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(AudioSourceHint::Sidecar)
                                .setEnumType(AudioSourceHint::Type)
                                .setDescription("Preferred audio source for image sequence readers."));

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

                /// @brief String — timing source for the SDL player.
                ///
                /// - @c "audio" (default) — pace to the audio device's
                ///   consumption rate.  Falls back to wall clock if the
                ///   stream has no audio or no audio output is available.
                /// - @c "wall" — pace to the system's monotonic wall
                ///   clock.  Audio is still played but is not used as
                ///   the timing reference.
                static inline const ID SdlTimingSource = declareID("SdlTimingSource",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String("audio"))
                                .setDescription(
                                        "Timing source: \"audio\" (default) or \"wall\"."));

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

                // ============================================================
                // V4L2 capture (Linux)
                // ============================================================

                /// @brief String — V4L2 device node path (e.g. "/dev/video0").
                static inline const ID V4l2DevicePath = declareID("V4l2DevicePath",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("V4L2 device node path."));

                /// @brief int — number of MMAP capture buffers (2-32).
                static inline const ID V4l2BufferCount = declareID("V4l2BufferCount",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(4))
                                .setRange(int32_t(2), int32_t(32))
                                .setDescription("Number of V4L2 MMAP capture buffers."));

                /// @brief String — ALSA capture device name for paired audio.
                /// "auto" (default) auto-detects a paired USB audio device.
                /// "none" or empty disables audio capture.
                /// Any other value is used as-is (e.g. "hw:1,0", "default").
                static inline const ID V4l2AudioDevice = declareID("V4l2AudioDevice",
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String("auto"))
                                .setDescription("ALSA capture device for paired audio. "
                                        "\"auto\" = auto-detect, \"none\" or empty = disabled."));

                /// @brief bool — enable audio clock-drift correction.
                /// When true, the audio ring buffer dynamically adjusts its
                /// resampling ratio to keep the fill level stable, compensating
                /// for clock drift between the ALSA capture clock and the V4L2
                /// video frame clock.  Default: true.
                static inline const ID V4l2AudioDriftCorrection = declareID("V4l2AudioDriftCorrection",
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable ALSA/V4L2 audio clock-drift correction."));

                /// @brief double — proportional gain for drift correction.
                /// Controls how aggressively the resampler compensates for
                /// clock drift.  Small values (0.001) give smooth, inaudible
                /// corrections; larger values track drift faster but risk
                /// audible pitch shifts.  Default: 0.001.
                static inline const ID V4l2AudioDriftGain = declareID("V4l2AudioDriftGain",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.001)
                                .setRange(0.0001, 0.1)
                                .setDescription("Drift correction proportional gain."));

                /// @brief double — drift correction target fill level in seconds.
                /// The ring buffer aims to keep this much audio buffered.
                /// Lower values reduce capture latency; higher values absorb
                /// more jitter.  Default: 0.1 (100 ms).
                static inline const ID V4l2AudioDriftTarget = declareID("V4l2AudioDriftTarget",
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.1)
                                .setRange(0.01, 2.0)
                                .setDescription("Drift correction target fill level in seconds."));

                // ---- V4L2 camera controls ----
                //
                // These map directly to V4L2 CID controls.  A value of
                // -1 (the default) means "don't touch, use device default."
                // Actual ranges are device-dependent; see --probe output.

                /// @brief int — Brightness (V4L2_CID_BRIGHTNESS).  -1 = device default.
                static inline const ID V4l2Brightness = declareID("V4l2Brightness",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Brightness (-1 = device default)."));

                /// @brief int — Contrast (V4L2_CID_CONTRAST).  -1 = device default.
                static inline const ID V4l2Contrast = declareID("V4l2Contrast",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Contrast (-1 = device default)."));

                /// @brief int — Saturation (V4L2_CID_SATURATION).  -1 = device default.
                static inline const ID V4l2Saturation = declareID("V4l2Saturation",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Saturation (-1 = device default)."));

                /// @brief int — Hue (V4L2_CID_HUE).  -1 = device default.
                static inline const ID V4l2Hue = declareID("V4l2Hue",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Hue (-1 = device default)."));

                /// @brief int — Gamma (V4L2_CID_GAMMA).  -1 = device default.
                static inline const ID V4l2Gamma = declareID("V4l2Gamma",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Gamma (-1 = device default)."));

                /// @brief int — Sharpness (V4L2_CID_SHARPNESS).  -1 = device default.
                static inline const ID V4l2Sharpness = declareID("V4l2Sharpness",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Sharpness (-1 = device default)."));

                /// @brief int — Backlight compensation (V4L2_CID_BACKLIGHT_COMPENSATION).
                /// -1 = device default.
                static inline const ID V4l2BacklightComp = declareID("V4l2BacklightComp",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Backlight compensation (-1 = device default)."));

                /// @brief int — White balance temperature in Kelvin
                /// (V4L2_CID_WHITE_BALANCE_TEMPERATURE).  -1 = device default.
                static inline const ID V4l2WhiteBalanceTemp = declareID("V4l2WhiteBalanceTemp",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("White balance temperature in K (-1 = device default)."));

                /// @brief bool — Auto white balance (V4L2_CID_AUTO_WHITE_BALANCE).
                /// -1 = device default, 0 = off, 1 = on.
                static inline const ID V4l2AutoWhiteBalance = declareID("V4l2AutoWhiteBalance",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setRange(int32_t(-1), int32_t(1))
                                .setDescription("Auto white balance (-1 = device default, 0 = off, 1 = on)."));

                /// @brief int — Exposure time, absolute, in 100µs units
                /// (V4L2_CID_EXPOSURE_ABSOLUTE).  -1 = device default.
                static inline const ID V4l2ExposureAbsolute = declareID("V4l2ExposureAbsolute",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Exposure time in 100us units (-1 = device default)."));

                /// @brief Enum @ref V4l2ExposureMode — auto exposure mode (V4L2_CID_EXPOSURE_AUTO).
                static inline const ID V4l2AutoExposure = declareID("V4l2AutoExposure",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(V4l2ExposureMode::Type)
                                .setDescription("Auto exposure mode (empty = device default)."));

                /// @brief int — Gain (V4L2_CID_GAIN).  -1 = device default.
                static inline const ID V4l2Gain = declareID("V4l2Gain",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Gain (-1 = device default)."));

                /// @brief Enum @ref V4l2PowerLineMode — power line frequency filter (V4L2_CID_POWER_LINE_FREQUENCY).
                static inline const ID V4l2PowerLineFreq = declareID("V4l2PowerLineFreq",
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(V4l2PowerLineMode::Type)
                                .setDescription("Power line frequency (empty = device default)."));

                /// @brief int — JPEG compression quality 1-100
                /// (V4L2_CID_JPEG_COMPRESSION_QUALITY).  -1 = device default.
                /// Not all devices support this control.
                static inline const ID V4l2JpegQuality = declareID("V4l2JpegQuality",
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("JPEG compression quality 1-100 (-1 = device default)."));
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
