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
 * @brief Generic name/value configuration container for the media subsystem.
 * @ingroup media
 *
 * Thin subclass of @ref VariantDatabase "VariantDatabase<"MediaConfig">"
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
class MediaConfig : public VariantDatabase<"MediaConfig"> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<"MediaConfig">;

                using Base::Base;

                // ============================================================
                // Common / core
                // ============================================================

                /// @brief String — path or URL to the media resource.
                PROMEKI_DECLARE_ID(Filename,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Path or URL to the media resource."));

                /// @brief String — registered backend type name (e.g. "TPG", "ImageFile").
                PROMEKI_DECLARE_ID(Type,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Registered backend type name."));

                /// @brief String — human-readable instance name (used in logs, spawned thread names,
                /// benchmark stamp IDs). Defaults to `"media<localId>"` when left empty.
                PROMEKI_DECLARE_ID(Name,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Human-readable instance name; defaults to "
                                        "\"media<localId>\" when empty."));

                /// @brief UUID — globally-unique instance identifier used for cross-process
                /// pipeline correlation. Defaults to a freshly generated UUID when left invalid.
                PROMEKI_DECLARE_ID(Uuid,
                        VariantSpec().setType(Variant::TypeUUID)
                                .setDefault(UUID())
                                .setDescription("Globally-unique instance identifier; defaults "
                                        "to a fresh UUID when invalid."));

                /// @brief bool — opt into per-frame Benchmark stamping in the MediaIO base class.
                /// When true, every frame flowing through this MediaIO receives stamps at
                /// enqueue / dequeue / taskBegin / taskEnd, aggregated by an attached
                /// BenchmarkReporter when the stage is a sink.
                PROMEKI_DECLARE_ID(EnableBenchmark,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable per-frame Benchmark stamping in the "
                                        "MediaIO base class."));

                /// @brief FrameRate — stream or target frame rate.
                PROMEKI_DECLARE_ID(FrameRate,
                        VariantSpec().setType(Variant::TypeFrameRate)
                                .setDefault(promeki::FrameRate())
                                .setDescription("Stream or target frame rate."));

                // ============================================================
                // Video — shared across backends
                // ============================================================

                /// @brief VideoFormat — combined video raster, frame rate, and scan mode.
                PROMEKI_DECLARE_ID(VideoFormat,
                        VariantSpec().setType(Variant::TypeVideoFormat)
                                .setDefault(promeki::VideoFormat())
                                .setDescription("Combined video raster, frame rate, and scan mode."));

                /// @brief bool — enable video generation / decode.
                PROMEKI_DECLARE_ID(VideoEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable video generation or decode."));

                /// @brief Size2Du32 — image dimensions.
                PROMEKI_DECLARE_ID(VideoSize,
                        VariantSpec().setType(Variant::TypeSize2D)
                                .setDefault(Size2Du32())
                                .setDescription("Image dimensions."));

                /// @brief PixelDesc — stage video pixel description (target format for
                /// generators, hint for headerless readers).
                PROMEKI_DECLARE_ID(VideoPixelFormat,
                        VariantSpec().setType(Variant::TypePixelDesc)
                                .setDefault(PixelDesc())
                                .setDescription("Video pixel description."));

                /// @brief int — 0-based video track index to use (-1 = auto).
                PROMEKI_DECLARE_ID(VideoTrack,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setMin(int32_t(-1))
                                .setDescription("0-based video track index (-1 = auto)."));

                // ============================================================
                // Video test pattern generator
                // ============================================================

                /// @brief Enum @ref VideoPattern — selected test pattern.
                PROMEKI_DECLARE_ID(VideoPattern,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoPattern::ColorBars)
                                .setEnumType(promeki::VideoPattern::Type)
                                .setDescription("Selected video test pattern."));

                /// @brief Color — fill color for @c SolidColor pattern.
                PROMEKI_DECLARE_ID(VideoSolidColor,
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Fill color for the SolidColor pattern."));

                /// @brief double — horizontal motion pixels/frame.
                PROMEKI_DECLARE_ID(VideoMotion,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.0)
                                .setDescription("Horizontal motion in pixels per frame."));

                // ============================================================
                // Video burn-in overlay
                // ============================================================

                /// @brief bool — enable text burn-in.
                PROMEKI_DECLARE_ID(VideoBurnEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable text burn-in overlay."));

                /// @brief String — TrueType / OpenType font path for burn-in.
                PROMEKI_DECLARE_ID(VideoBurnFontPath,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("TrueType or OpenType font path for burn-in."));

                /// @brief int — burn-in font size in pixels.
                PROMEKI_DECLARE_ID(VideoBurnFontSize,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Burn-in font size in pixels."));

                /// @brief String — burn-in text template (Frame::makeString).
                PROMEKI_DECLARE_ID(VideoBurnText,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Burn-in text as a Frame::makeString "
                                        "template, resolved per-frame against the "
                                        "frame's metadata.  Use \\n for multi-line."));

                /// @brief Enum @ref BurnPosition — on-screen position.
                PROMEKI_DECLARE_ID(VideoBurnPosition,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(BurnPosition::BottomCenter)
                                .setEnumType(BurnPosition::Type)
                                .setDescription("On-screen burn-in text position."));

                /// @brief Color — burn-in text color.
                PROMEKI_DECLARE_ID(VideoBurnTextColor,
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Burn-in text color."));

                /// @brief Color — burn-in background color.
                PROMEKI_DECLARE_ID(VideoBurnBgColor,
                        VariantSpec().setType(Variant::TypeColor)
                                .setDefault(Color())
                                .setDescription("Burn-in background color."));

                /// @brief bool — draw background rectangle behind burn-in text.
                PROMEKI_DECLARE_ID(VideoBurnDrawBg,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Draw background rectangle behind burn-in text."));

                // ============================================================
                // Audio — shared across backends
                // ============================================================

                /// @brief bool — enable audio generation / decode.
                PROMEKI_DECLARE_ID(AudioEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable audio generation or decode."));

                /// @brief float — audio sample rate in Hz.
                PROMEKI_DECLARE_ID(AudioRate,
                        VariantSpec().setType(Variant::TypeFloat)
                                .setDefault(0.0f)
                                .setMin(0.0f)
                                .setDescription("Audio sample rate in Hz."));

                /// @brief int — audio channel count.
                PROMEKI_DECLARE_ID(AudioChannels,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio channel count."));

                /// @brief int — 0-based audio track index to use (-1 = auto).
                PROMEKI_DECLARE_ID(AudioTrack,
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
                PROMEKI_DECLARE_ID(AudioChannelModes,
                        VariantSpec().setType(Variant::TypeEnumList)
                                .setDefault(EnumList::forType<AudioPattern>())
                                .setEnumType(AudioPattern::Type)
                                .setDescription("Comma-separated list of per-channel audio test "
                                        "patterns (extra channels silenced)."));

                /// @brief double — tone frequency in Hz (used by Tone / AvSync).
                PROMEKI_DECLARE_ID(AudioToneFrequency,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1000.0)
                                .setMin(0.0)
                                .setDescription("Tone frequency in Hz (Tone / AvSync channels)."));

                /// @brief double — tone level in dBFS.
                PROMEKI_DECLARE_ID(AudioToneLevel,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(-20.0)
                                .setMax(0.0)
                                .setDescription("Tone level in dBFS."));

                /// @brief double — LTC burn-in level in dBFS.
                PROMEKI_DECLARE_ID(AudioLtcLevel,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(-20.0)
                                .setMax(0.0)
                                .setDescription("LTC burn-in level in dBFS."));

                /// @brief double — ChannelId base frequency in Hz.
                ///        Channel @em N carries a sine at
                ///        `base + N * step`.
                PROMEKI_DECLARE_ID(AudioChannelIdBaseFreq,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1000.0)
                                .setMin(0.0)
                                .setDescription("ChannelId base tone frequency in Hz."));

                /// @brief double — ChannelId per-channel step in Hz.
                PROMEKI_DECLARE_ID(AudioChannelIdStepFreq,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(100.0)
                                .setMin(0.0)
                                .setDescription("ChannelId per-channel tone step in Hz."));

                /// @brief double — Chirp sweep start frequency in Hz.
                PROMEKI_DECLARE_ID(AudioChirpStartFreq,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(20.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep start frequency in Hz."));

                /// @brief double — Chirp sweep end frequency in Hz.
                PROMEKI_DECLARE_ID(AudioChirpEndFreq,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(20000.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep end frequency in Hz."));

                /// @brief double — Chirp sweep period in seconds.
                PROMEKI_DECLARE_ID(AudioChirpDurationSec,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1.0)
                                .setMin(0.0)
                                .setDescription("Chirp log-sweep period in seconds."));

                /// @brief double — DualTone low-side frequency in Hz.
                PROMEKI_DECLARE_ID(AudioDualToneFreq1,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(60.0)
                                .setMin(0.0)
                                .setDescription("DualTone low-side frequency in Hz (SMPTE IMD default 60 Hz)."));

                /// @brief double — DualTone high-side frequency in Hz.
                PROMEKI_DECLARE_ID(AudioDualToneFreq2,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(7000.0)
                                .setMin(0.0)
                                .setDescription("DualTone high-side frequency in Hz (SMPTE IMD default 7 kHz)."));

                /// @brief double — DualTone amplitude ratio freq2 / freq1.
                PROMEKI_DECLARE_ID(AudioDualToneRatio,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.25)
                                .setMin(0.0)
                                .setDescription("DualTone amplitude ratio of freq2 to freq1 "
                                        "(SMPTE IMD default 0.25 = 4:1)."));

                /// @brief double — WhiteNoise / PinkNoise buffer length in seconds.
                PROMEKI_DECLARE_ID(AudioNoiseBufferSec,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(10.0)
                                .setMin(0.0)
                                .setDescription("WhiteNoise / PinkNoise cached buffer length in seconds."));

                /// @brief uint32 — PRNG seed used to build the noise buffers.
                PROMEKI_DECLARE_ID(AudioNoiseSeed,
                        VariantSpec().setType(Variant::TypeU32)
                                .setDefault(uint32_t(0x505244A4u))
                                .setDescription("WhiteNoise / PinkNoise PRNG seed."));

                // ============================================================
                // Timecode
                // ============================================================

                /// @brief bool — enable timecode generation.
                PROMEKI_DECLARE_ID(TimecodeEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable timecode generation."));

                /// @brief String — starting timecode (SMPTE "HH:MM:SS:FF" form).
                PROMEKI_DECLARE_ID(TimecodeStart,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Starting timecode in SMPTE HH:MM:SS:FF form."));

                /// @brief Timecode — pre-built starting timecode (alternative to @c TimecodeStart).
                PROMEKI_DECLARE_ID(TimecodeValue,
                        VariantSpec().setType(Variant::TypeTimecode)
                                .setDefault(Timecode())
                                .setDescription("Pre-built starting timecode."));

                /// @brief bool — drop-frame flag for 29.97 / 59.94 timecode.
                PROMEKI_DECLARE_ID(TimecodeDropFrame,
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
                PROMEKI_DECLARE_ID(StreamID,
                        VariantSpec().setType(Variant::TypeU32)
                                .setDefault(uint32_t(0))
                                .setDescription("Opaque per-stream identifier (uint32)."));

                /// @brief bool — enable the @ref ImageDataEncoder pass on TPG video
                /// frames.  When true (default), the TPG stamps two 64-bit
                /// payloads into the top of every generated video frame:
                /// (1) @c (StreamID << 32) | frameNumber, and
                /// (2) the @ref Timecode::toBcd64 BCD timecode word.
                PROMEKI_DECLARE_ID(TpgDataEncoderEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable VITC-style binary data encoder pass on TPG video."));

                /// @brief int — number of scan lines each @ref ImageDataEncoder item
                /// occupies in the encoded band.  The TPG emits two items, so the
                /// total stamped band height is @c 2 * TpgDataEncoderRepeatLines
                /// scan lines starting from the top of the image.  Default 16,
                /// which gives a comfortable read margin for a noisy decoder
                /// without consuming too much picture area.
                PROMEKI_DECLARE_ID(TpgDataEncoderRepeatLines,
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
                PROMEKI_DECLARE_ID(InspectorDropFrames,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Inspector drops frames after checks (sink behaviour)."));

                /// @brief EnumList @ref InspectorTest — list of inspector
                /// tests to run.
                ///
                /// The default lists every known test
                /// (@c ImageData, @c Ltc, @c TcSync, @c Continuity,
                /// @c Timestamp, @c AudioSamples) so a default-
                /// configured inspector runs the full suite.  Set to
                /// a shorter list to disable tests; an empty list
                /// disables every test.
                /// Dependencies are still auto-resolved (e.g. asking
                /// for @c TcSync implicitly also enables @c ImageData
                /// and @c Ltc).
                ///
                /// @par Example
                /// @code
                /// // Only run the timestamp and A/V sync checks:
                /// EnumList tests = EnumList::forType<InspectorTest>();
                /// tests.append(InspectorTest::Timestamp);
                /// tests.append(InspectorTest::TcSync);
                /// cfg.set(MediaConfig::InspectorTests, tests);
                /// // Equivalent string form on the command line:
                /// //   InspectorTests=Timestamp,TcSync
                /// @endcode
                PROMEKI_DECLARE_ID(InspectorTests,
                        VariantSpec().setType(Variant::TypeEnumList)
                                .setDefault([]{
                                        EnumList l = EnumList::forType<InspectorTest>();
                                        l.append(InspectorTest::ImageData);
                                        l.append(InspectorTest::Ltc);
                                        l.append(InspectorTest::TcSync);
                                        l.append(InspectorTest::Continuity);
                                        l.append(InspectorTest::Timestamp);
                                        l.append(InspectorTest::AudioSamples);
                                        return l;
                                }())
                                .setEnumType(InspectorTest::Type)
                                .setDescription(
                                        "List of inspector tests to run."));

                /// @brief int — maximum allowed sample-to-sample change in the
                /// picture-vs-LTC sync offset before the inspector flags a
                /// discontinuity.  Default 0: any change is reported.  Set
                /// higher (e.g. 1, 2, ...) when the upstream encode/decode
                /// pair has a known small jitter that should not generate
                /// noise.  Only meaningful when the @c TcSync test is
                /// enabled.
                PROMEKI_DECLARE_ID(InspectorSyncOffsetToleranceSamples,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription(
                                        "Max allowed sample-to-sample change in "
                                        "picture-vs-LTC sync offset before flagging "
                                        "a discontinuity (0 = any change)."));

                /// @brief int — scan lines per @ref ImageDataEncoder band.  Must
                /// match the encoder's @ref TpgDataEncoderRepeatLines so the
                /// inspector reads at the right band offsets.  Default 16.
                PROMEKI_DECLARE_ID(InspectorImageDataRepeatLines,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(16))
                                .setMin(int32_t(1))
                                .setDescription("Scan lines per ImageDataDecoder band in Inspector."));

                /// @brief int — audio channel index that carries LTC.  Mirrors
                /// @ref AudioLtcChannel; default 0.
                PROMEKI_DECLARE_ID(InspectorLtcChannel,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio channel index carrying LTC for the inspector."));

                /// @brief double — periodic-summary log interval in seconds (wall
                /// time, not media time).  Default 1.0.  Set to 0 to disable
                /// periodic logging entirely; per-frame events are still produced.
                PROMEKI_DECLARE_ID(InspectorLogIntervalSec,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(1.0)
                                .setMin(0.0)
                                .setDescription("Inspector periodic-summary log interval, seconds."));

                /// @brief String — output file for the @c CaptureStats
                /// inspector test.
                ///
                /// One tab-separated row per frame is appended to the
                /// named file while the @c CaptureStats test is enabled
                /// via @ref InspectorTests.  If the value is empty a
                /// unique filename is generated inside
                /// @c Dir::temp() (form:
                /// @c promeki_inspector_stats_<pid>_<epoch_ns>.tsv).
                /// The resolved path is logged at @c Info level when
                /// the file is opened.
                PROMEKI_DECLARE_ID(InspectorStatsFile,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription(
                                        "Output file for Inspector CaptureStats test "
                                        "(TSV, one row per frame).  Empty = auto-name "
                                        "in Dir::temp()."));

                // ============================================================
                // CSC (MediaIOTask_CSC)
                // ============================================================

                /// @brief PixelDesc — target pixel description for the converter
                /// stage (@c Invalid = video pass-through).
                PROMEKI_DECLARE_ID(OutputPixelDesc,
                        VariantSpec().setType(Variant::TypePixelDesc)
                                .setDefault(PixelDesc())
                                .setDescription("Target pixel description (Invalid = pass-through)."));

                /// @brief Enum @ref AudioDataType — target audio sample format
                /// (@c Invalid = audio pass-through).
                PROMEKI_DECLARE_ID(OutputAudioDataType,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(AudioDataType::Type)
                                .setDescription("Target audio sample format (Invalid = pass-through)."));

                /// @brief int — internal FIFO capacity in frames.
                PROMEKI_DECLARE_ID(Capacity,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Internal FIFO capacity in frames."));

                // ============================================================
                // FrameSync (MediaIOTask_FrameSync)
                // ============================================================

                /// @brief FrameRate — output frame rate for the FrameSync task.
                /// When invalid (default), the output rate is inherited from
                /// @c pendingMediaDesc — i.e. the source rate passes through.
                /// Set to a valid FrameRate to resync to a different cadence.
                PROMEKI_DECLARE_ID(OutputFrameRate,
                        VariantSpec().setType(Variant::TypeFrameRate)
                                .setDefault(promeki::FrameRate())
                                .setDescription("FrameSync output frame rate "
                                        "(invalid = inherit from source)."));

                /// @brief float — output audio sample rate for the FrameSync task.
                /// When zero (default), the sample rate is inherited from
                /// @c pendingMediaDesc.  Set to a positive value to resample
                /// to a different rate.
                PROMEKI_DECLARE_ID(OutputAudioRate,
                        VariantSpec().setType(Variant::TypeFloat)
                                .setDefault(0.0f)
                                .setMin(0.0f)
                                .setDescription("FrameSync output audio sample rate "
                                        "(0 = inherit from source)."));

                /// @brief int — output audio channel count for the FrameSync task.
                /// When zero (default), the channel count is inherited from
                /// @c pendingMediaDesc.
                PROMEKI_DECLARE_ID(OutputAudioChannels,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("FrameSync output audio channel count "
                                        "(0 = inherit from source)."));

                /// @brief int — input queue depth for the FrameSync task.
                PROMEKI_DECLARE_ID(InputQueueCapacity,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(8))
                                .setMin(int32_t(1))
                                .setDescription("FrameSync input queue depth."));

                // ============================================================
                // FrameBridge (cross-process shared-memory frame transport)
                // ============================================================

                /// @brief String — logical bridge name.  Required.  Identifies
                /// the FrameBridge output that inputs connect to.
                PROMEKI_DECLARE_ID(FrameBridgeName,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("FrameBridge logical name (required)."));

                /// @brief int — number of ring-buffer slots.  Default 2 (ping-pong).
                PROMEKI_DECLARE_ID(FrameBridgeRingDepth,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(2))
                                .setMin(int32_t(2))
                                .setDescription("FrameBridge ring-buffer depth."));

                /// @brief int — per-slot metadata reserve bytes (default 64 KiB).
                PROMEKI_DECLARE_ID(FrameBridgeMetadataReserveBytes,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(64 * 1024))
                                .setMin(int32_t(512))
                                .setDescription("FrameBridge metadata reserve per slot, bytes."));

                /// @brief double — extra audio capacity fraction above worst-case
                /// samples-per-frame (default 0.20).
                PROMEKI_DECLARE_ID(FrameBridgeAudioHeadroomFraction,
                        VariantSpec().setType(Variant::TypeDouble)
                                .setDefault(0.20)
                                .setMin(0.0)
                                .setDescription("FrameBridge audio headroom fraction."));

                /// @brief int — POSIX file mode for the shm and socket (default 0600).
                PROMEKI_DECLARE_ID(FrameBridgeAccessMode,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0600))
                                .setDescription("FrameBridge POSIX access mode."));

                /// @brief String — group name for cross-user access (empty = skip).
                PROMEKI_DECLARE_ID(FrameBridgeGroupName,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("FrameBridge chown group (empty = skip)."));

                /// @brief bool — input-side sync mode (default true).
                ///
                /// When @c true (the default) the consumer acknowledges
                /// every frame before the publisher advances, providing
                /// natural back-pressure.  When @c false the consumer
                /// runs free and the publisher never waits — faster,
                /// but the consumer must keep up or it will miss
                /// frames.  Only consulted when the MediaIO task is
                /// opened on the consumer side.
                PROMEKI_DECLARE_ID(FrameBridgeSyncMode,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("FrameBridge input sync mode."));

                /// @brief bool — publisher blocks until a consumer attaches
                /// (default true).
                ///
                /// When @c true (the default) the output-side
                /// @c writeFrame blocks instead of silently dropping
                /// frames while no consumer is connected.  Set
                /// @c false to let the publisher run free even when
                /// nobody is listening (e.g. live capture that must
                /// not stall).  Only consulted when the MediaIO task
                /// is opened on the producer side.
                PROMEKI_DECLARE_ID(FrameBridgeWaitForConsumer,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("FrameBridge output: block writeFrame until consumer connects."));

                // ============================================================
                // JPEG codec
                // ============================================================

                /// @brief int — JPEG quality 1-100 (codec default: 85).
                PROMEKI_DECLARE_ID(JpegQuality,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(85))
                                .setRange(int32_t(1), int32_t(100))
                                .setDescription("JPEG quality 1-100."));

                /// @brief Enum @ref ChromaSubsampling — JPEG chroma subsampling
                /// (codec default: 4:2:2, RFC 2435 compatible).
                PROMEKI_DECLARE_ID(JpegSubsampling,
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
                PROMEKI_DECLARE_ID(JpegXsBpp,
                        VariantSpec().setTypes({Variant::TypeS32, Variant::TypeFloat, Variant::TypeDouble})
                                .setDefault(int32_t(3))
                                .setMin(1)
                                .setDescription("JPEG XS target bits per pixel."));

                /// @brief int — JPEG XS horizontal decomposition depth (0-5,
                /// codec default: 5).  Higher values trade encode cost for
                /// quality.  See @c ndecomp_h in SvtJpegxsEnc.h.
                PROMEKI_DECLARE_ID(JpegXsDecomposition,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(5))
                                .setRange(int32_t(0), int32_t(5))
                                .setDescription("JPEG XS horizontal decomposition depth 0-5."));

                // ============================================================
                // Video codec rate control (H.264 / HEVC / shared)
                // ============================================================
                //
                // These keys are generic across any @ref VideoEncoder
                // backend (NVENC, x264, QSV, VA-API, AMF, …).  Each backend
                // honours the subset it natively supports; keys it does
                // not understand are ignored by @c configure().  The
                // bitrate is expressed in kilobits-per-second so callers
                // can say @c cfg.set(MediaConfig::BitrateKbps, 10000)
                // without thinking about byte-vs-bit conversions.

                /// @brief int — target / average bitrate in kbit/s.
                /// Honoured by @c VideoRateControl::CBR and
                /// @c VideoRateControl::VBR.  Ignored by
                /// @c VideoRateControl::CQP.  Codec default: 5000.
                PROMEKI_DECLARE_ID(BitrateKbps,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(5000))
                                .setMin(int32_t(1))
                                .setDescription("Target / average bitrate in kbit/s."));

                /// @brief int — maximum (peak) bitrate in kbit/s.  Only
                /// meaningful for @c VideoRateControl::VBR; CBR ignores
                /// it, CQP ignores it.  Codec default: 0 (no cap).
                PROMEKI_DECLARE_ID(MaxBitrateKbps,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Peak bitrate in kbit/s (VBR only; 0 = uncapped)."));

                /// @brief Enum @ref VideoRateControl — rate-control mode.
                /// Codec default: VBR.
                PROMEKI_DECLARE_ID(VideoRcMode,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoRateControl::VBR)
                                .setEnumType(promeki::VideoRateControl::Type)
                                .setDescription("Video rate-control mode (CBR / VBR / CQP)."));

                /// @brief int — GOP length in frames (distance between
                /// keyframes).  0 = codec default.  Negative values are
                /// rejected.  Codec default: 60.
                PROMEKI_DECLARE_ID(GopLength,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(60))
                                .setMin(int32_t(0))
                                .setDescription("GOP length in frames (0 = codec default)."));

                /// @brief int — maximum frames between IDR keyframes.  For
                /// many codecs this is the same as @ref GopLength, but a
                /// closed-GOP encoder may allow open GOPs where I-frames
                /// are more frequent than IDRs.  0 = same as @c GopLength.
                PROMEKI_DECLARE_ID(IdrInterval,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Maximum frames between IDR keyframes "
                                                "(0 = same as GopLength)."));

                /// @brief int — number of B-frames between reference frames.
                /// 0 = disable B-frames (lowest latency).  Codec default: 0.
                PROMEKI_DECLARE_ID(BFrames,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Number of B-frames between references "
                                                "(0 = no B-frames)."));

                /// @brief int — number of look-ahead frames for rate
                /// control.  0 = disable look-ahead (lowest latency).
                /// Codec default: 0.
                PROMEKI_DECLARE_ID(LookaheadFrames,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Rate-control look-ahead depth in frames "
                                                "(0 = disabled)."));

                /// @brief Enum @ref VideoEncoderPreset — speed/quality preset.
                /// Each concrete backend maps this onto its own native
                /// preset.  Codec default: Balanced.
                PROMEKI_DECLARE_ID(VideoPreset,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoEncoderPreset::Balanced)
                                .setEnumType(promeki::VideoEncoderPreset::Type)
                                .setDescription("Video encoder speed/quality preset."));

                /// @brief String — codec-specific profile name
                /// (e.g. @c "baseline", @c "main", @c "high" for H.264;
                /// @c "main", @c "main10" for HEVC).  Empty string = codec
                /// default.  Profiles are string-typed because the valid
                /// set is codec-dependent.
                PROMEKI_DECLARE_ID(VideoProfile,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Codec-specific profile name "
                                                "(empty = codec default)."));

                /// @brief String — codec-specific level name
                /// (e.g. @c "4.0", @c "4.1", @c "5.1").  Empty string =
                /// codec default (auto-selected from resolution /
                /// bitrate).
                PROMEKI_DECLARE_ID(VideoLevel,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Codec-specific level name "
                                                "(empty = codec default / auto)."));

                /// @brief int — constant quantization parameter used when
                /// @ref VideoRcMode is @c CQP.  Lower values = higher
                /// quality and higher bitrate.  Typical range 18..40 for
                /// H.264 / HEVC.  Codec default: 23.
                PROMEKI_DECLARE_ID(VideoQp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(23))
                                .setRange(int32_t(0), int32_t(51))
                                .setDescription("Constant QP for CQP rate-control mode."));

                /// @brief bool — enable spatial adaptive quantization.
                PROMEKI_DECLARE_ID(VideoSpatialAQ,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable spatial adaptive quantization."));

                /// @brief int — spatial AQ strength (1-15; 0 = auto).
                PROMEKI_DECLARE_ID(VideoSpatialAQStrength,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setRange(int32_t(0), int32_t(15))
                                .setDescription("Spatial AQ strength (1-15; 0 = auto)."));

                /// @brief bool — enable temporal adaptive quantization.
                PROMEKI_DECLARE_ID(VideoTemporalAQ,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable temporal adaptive quantization."));

                /// @brief int — multi-pass encoding mode
                /// (0 = disabled, 1 = quarter-resolution, 2 = full-resolution).
                PROMEKI_DECLARE_ID(VideoMultiPass,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setRange(int32_t(0), int32_t(2))
                                .setDescription("Multi-pass mode "
                                                "(0=disabled, 1=quarter-res, 2=full-res)."));

                /// @brief bool — emit SPS/PPS (H.264), VPS/SPS/PPS (HEVC),
                /// or Sequence Header (AV1) with every IDR/key frame.
                PROMEKI_DECLARE_ID(VideoRepeatHeaders,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Emit parameter sets / sequence headers "
                                                "with every IDR."));

                /// @brief bool — emit SMPTE timecode via codec SEI.  Carried
                /// in Picture Timing SEI (H.264) / Time Code SEI (HEVC); the
                /// per-frame value comes from @ref Metadata::Timecode on the
                /// source Image.  Frames without a valid Timecode skip
                /// insertion for that picture.  Ignored for AV1 (NVENC does
                /// not expose a timecode OBU path).
                PROMEKI_DECLARE_ID(VideoTimecodeSEI,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Emit SMPTE timecode SEI "
                                                "(H.264 picture timing / HEVC time code)."));

                /// @brief @ref ColorPrimaries — color primaries signalled
                /// in the VUI (H.264/HEVC) or color description (AV1).
                /// Numeric values per ISO/IEC 23091-4 / ITU-T H.273.
                ///
                /// Default @c Auto lets the encoder derive the value from
                /// the first input frame's PixelDesc / ColorModel (Rec.709
                /// → @c BT709, Rec.2020 → @c BT2020, sRGB → @c BT709, …).
                /// Set @c Unspecified to suppress the color-description
                /// block entirely, or pick a specific value to override.
                PROMEKI_DECLARE_ID(VideoColorPrimaries,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::ColorPrimaries::Auto)
                                .setEnumType(promeki::ColorPrimaries::Type)
                                .setDescription("VUI color primaries "
                                                "(Auto = derive from input)."));

                /// @brief @ref TransferCharacteristics — opto-electronic
                /// transfer function signalled in the VUI.  Numeric values
                /// per ISO/IEC 23091-4 / ITU-T H.273.
                ///
                /// Default @c Auto derives the SDR curve matching the
                /// input primaries.  Auto-derivation cannot pick HDR
                /// curves today — the library's @ref ColorModel doesn't
                /// distinguish PQ / HLG yet — so HDR callers must set
                /// @c SMPTE2084 (HDR10) or @c ARIB_STD_B67 (HLG) explicitly.
                PROMEKI_DECLARE_ID(VideoTransferCharacteristics,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::TransferCharacteristics::Auto)
                                .setEnumType(promeki::TransferCharacteristics::Type)
                                .setDescription("VUI transfer characteristics "
                                                "(Auto = derive from input)."));

                /// @brief @ref MatrixCoefficients — Y'CbCr derivation matrix
                /// signalled in the VUI.  Numeric values per ISO/IEC 23091-4
                /// / ITU-T H.273.
                ///
                /// Default @c Auto derives from the input PixelDesc's
                /// ColorModel (RGB models → @c RGB, YCbCr_Rec709 →
                /// @c BT709, YCbCr_Rec2020 → @c BT2020_NCL, …).
                PROMEKI_DECLARE_ID(VideoMatrixCoefficients,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::MatrixCoefficients::Auto)
                                .setEnumType(promeki::MatrixCoefficients::Type)
                                .setDescription("VUI matrix coefficients "
                                                "(Auto = derive from input)."));

                /// @brief @ref VideoRange — studio/limited vs. full-range
                /// flag signalled in the VUI @c videoFullRangeFlag (H.264,
                /// HEVC) or AV1 @c colorRange field.
                ///
                /// Default @c Unknown means "derive from the first input
                /// frame's @ref PixelDesc::videoRange".  Callers can force
                /// @c Limited or @c Full to override the PixelDesc-derived
                /// signalling (rarely useful, but covers formats whose
                /// on-wire representation disagrees with their source
                /// convention).
                PROMEKI_DECLARE_ID(VideoRange,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoRange::Unknown)
                                .setEnumType(promeki::VideoRange::Type)
                                .setDescription("VUI video range "
                                                "(Unknown = derive from input)."));

                /// @brief Enum @ref ChromaSubsampling — preferred chroma
                /// sampling for a video encoder's accepted input.
                ///
                /// Governs which of the backend's @c supportedInputs the
                /// pipeline planner advertises as the encoder's preferred
                /// input format.  The CSC then converts the upstream
                /// format down / across to match.  Defaults to @c YUV420
                /// — the lingua franca of H.264 / HEVC / AV1 decoders —
                /// so an RGB or 4:4:4 source routed into an encoder
                /// lands on a 4:2:0 bitstream (H.264 Main / High profile,
                /// HEVC Main) by default.  Callers that need a higher-
                /// quality chroma path pin @c YUV422 or @c YUV444
                /// explicitly.
                PROMEKI_DECLARE_ID(VideoChromaSubsampling,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(ChromaSubsampling::YUV420)
                                .setEnumType(ChromaSubsampling::Type)
                                .setDescription("Preferred encoder input "
                                                "chroma subsampling (default "
                                                "4:2:0)."));

                /// @brief @ref VideoScanMode — raster scan mode signalled
                /// through codec SEI (H.264 Picture Timing / HEVC Picture
                /// Timing) and/or native interlaced coding.
                ///
                /// Default @c Unknown means "derive from the first input
                /// frame's @ref ImageDesc::videoScanMode; fall back to
                /// @c Progressive when that is also @c Unknown".  When
                /// the resolved mode is interlaced the NVENC backend
                /// flips on H.264 @c outputPictureTimingSEI / HEVC @c
                /// outputPictureTimingSEI at session init so every
                /// decoded picture carries its @c pic_struct, and then
                /// maps @c InterlacedEvenFirst / @c InterlacedOddFirst
                /// to @c NV_ENC_PIC_STRUCT_DISPLAY_FIELD_TOP_BOTTOM /
                /// @c _BOTTOM_TOP in the per-picture @c NV_ENC_TIME_CODE.
                ///
                /// Per-frame overrides go through
                /// @ref Metadata::VideoScanMode on the source Image,
                /// which lets a stream carry mixed scan modes if the
                /// source format allows it.  AV1 does not expose
                /// interlaced signalling through NVENC, so interlaced
                /// requests to the AV1 codec warn-once and fall through
                /// as progressive.
                PROMEKI_DECLARE_ID(VideoScanMode,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::VideoScanMode::Unknown)
                                .setEnumType(promeki::VideoScanMode::Type)
                                .setDescription("Raster scan mode "
                                                "(Unknown = derive from input)."));

                /// @brief @ref MasteringDisplay — stream-level mastering
                /// display color volume (SMPTE ST 2086).  When set, the
                /// encoder embeds this in every IDR (HEVC/AV1 SEI/OBU).
                /// Per-frame overrides via @ref Metadata::MasteringDisplay
                /// on the source Image take precedence.
                PROMEKI_DECLARE_ID(HdrMasteringDisplay,
                        VariantSpec().setType(Variant::TypeMasteringDisplay)
                                .setDescription("Stream-level mastering display metadata "
                                                "(SMPTE ST 2086)."));

                /// @brief @ref ContentLightLevel — stream-level content
                /// light level information (CTA-861.3).  When set, the
                /// encoder embeds this in every IDR (HEVC/AV1 SEI/OBU).
                PROMEKI_DECLARE_ID(HdrContentLightLevel,
                        VariantSpec().setType(Variant::TypeContentLightLevel)
                                .setDescription("Stream-level content light level "
                                                "(MaxCLL / MaxFALL)."));

                /// @brief @ref VideoCodec — typed codec identity used by
                /// the generic video encoder / decoder @ref MediaIOTask
                /// backends to look up the concrete @ref VideoEncoder /
                /// @ref VideoDecoder factory.  Authored on the CLI as
                /// the codec's registered name (e.g. @c "H264",
                /// @c "HEVC", @c "JPEG") and resolved through
                /// @ref promeki::VideoCodec::lookup.  Distinct from
                /// @ref Type, which selects the @ref MediaIO backend
                /// itself (e.g. @c "VideoEncoder" vs @c "CSC").
                PROMEKI_DECLARE_ID(VideoCodec,
                        VariantSpec().setType(Variant::TypeVideoCodec)
                                .setDefault(promeki::VideoCodec())
                                .setDescription("Video codec for the VideoEncoder / "
                                                "VideoDecoder backends "
                                                "(e.g. \"H264\", \"HEVC\", \"JPEG\")."));

                /// @brief @ref AudioCodec — typed codec identity used by
                /// audio encoder / decoder backends to look up the
                /// concrete encoder / decoder factory (currently
                /// metadata-only — registered audio backends will land
                /// alongside this key).
                PROMEKI_DECLARE_ID(AudioCodec,
                        VariantSpec().setType(Variant::TypeAudioCodec)
                                .setDefault(promeki::AudioCodec())
                                .setDescription("Audio codec for the audio encoder / "
                                                "decoder backends "
                                                "(e.g. \"AAC\", \"Opus\", \"FLAC\")."));

                // ============================================================
                // CSC pipeline
                // ============================================================

                /// @brief Enum @ref CscPath — CSC processing path selection
                /// (@c Optimized default, @c Scalar for debug / reference).
                PROMEKI_DECLARE_ID(CscPath,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::CscPath::Optimized)
                                .setEnumType(promeki::CscPath::Type)
                                .setDescription("CSC processing path (Optimized or Scalar)."));

                // ============================================================
                // Image file sequence (MediaIOTask_ImageFile)
                // ============================================================

                /// @brief int — explicit @ref ImageFile::ID, bypasses extension probe.
                PROMEKI_DECLARE_ID(ImageFileID,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Explicit ImageFile ID (0 = infer from extension)."));

                /// @brief int — first frame index for a sequence writer.
                PROMEKI_DECLARE_ID(SequenceHead,
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
                PROMEKI_DECLARE_ID(SaveImgSeqEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable automatic .imgseq sidecar for image sequences."));

                /// @brief String — override path for the @c .imgseq sidecar.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.imgseq" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                PROMEKI_DECLARE_ID(SaveImgSeqPath,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Override path for the .imgseq sidecar."));

                /// @brief Enum @ref ImgSeqPathMode — whether the sidecar's
                /// directory reference is relative (to the sidecar) or absolute.
                PROMEKI_DECLARE_ID(SaveImgSeqPathMode,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(ImgSeqPathMode::Relative)
                                .setEnumType(ImgSeqPathMode::Type)
                                .setDescription("Sidecar directory reference mode (Relative or Absolute)."));

                /// @brief bool — enable sidecar audio file alongside an image
                /// sequence.  When true (the default), the ImageFile backend
                /// writes a Broadcast WAV sidecar when audio data is present
                /// and auto-detects an existing sidecar on read.  Set to
                /// @c false to inhibit both behaviours.
                PROMEKI_DECLARE_ID(SidecarAudioEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(true)
                                .setDescription("Enable sidecar audio file for image sequences."));

                /// @brief String — override path for the sidecar audio file.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.wav" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                PROMEKI_DECLARE_ID(SidecarAudioPath,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Override path for the sidecar audio file."));

                /// @brief Enum @ref AudioSourceHint — preferred audio source
                /// when reading an image sequence.  Selects between the
                /// sidecar audio file and embedded per-frame audio (e.g. DPX
                /// user-data blocks).  Acts as a hint: if the preferred
                /// source is unavailable, the backend falls back to the
                /// other.  Default is @c Sidecar.
                PROMEKI_DECLARE_ID(AudioSource,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(AudioSourceHint::Sidecar)
                                .setEnumType(AudioSourceHint::Type)
                                .setDescription("Preferred audio source for image sequence readers."));

                // ============================================================
                // QuickTime / ISO-BMFF (MediaIOTask_QuickTime)
                // ============================================================

                /// @brief Enum QuickTimeLayout — writer on-disk layout.
                PROMEKI_DECLARE_ID(QuickTimeLayout,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::QuickTimeLayout::Fragmented)
                                .setEnumType(promeki::QuickTimeLayout::Type)
                                .setDescription("QuickTime writer on-disk layout."));

                /// @brief int — video frames per fragment (fragmented writer).
                PROMEKI_DECLARE_ID(QuickTimeFragmentFrames,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video frames per fragment (fragmented writer)."));

                /// @brief bool — call @c fdatasync after each flush.
                PROMEKI_DECLARE_ID(QuickTimeFlushSync,
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
                PROMEKI_DECLARE_ID(SdlTimingSource,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String("audio"))
                                .setDescription(
                                        "Timing source: \"audio\" (default) or \"wall\"."));

                /// @brief Size2Du32 — initial SDL window size.
                PROMEKI_DECLARE_ID(SdlWindowSize,
                        VariantSpec().setType(Variant::TypeSize2D)
                                .setDefault(Size2Du32())
                                .setDescription("Initial SDL window size."));

                /// @brief String — SDL window title bar text.
                PROMEKI_DECLARE_ID(SdlWindowTitle,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDL window title bar text."));

                /// @brief String — which SDL player implementation to use.
                ///
                /// - @c "framesync" (default) — @c FrameSync-based
                ///   @c SDLPlayerTask.  Runs video repeat/drop +
                ///   audio drift-corrected resampling through a
                ///   shared sync object; the sink's pull thread
                ///   drives pacing instead of the strand worker.
                /// - @c "pacer" — legacy FramePacer-based
                ///   @c SDLPlayerOldTask (deprecated; retained for
                ///   side-by-side comparison).
                PROMEKI_DECLARE_ID(SdlPlayerImpl,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String("framesync"))
                                .setDescription(
                                        "SDL player implementation: "
                                        "\"framesync\" (default) or "
                                        "\"pacer\" (deprecated)."));

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
                PROMEKI_DECLARE_ID(RtpLocalAddress,
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Local bind address for all RTP streams."));

                /// @brief String — SDP @c s= line (session name).
                PROMEKI_DECLARE_ID(RtpSessionName,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDP session name (s= line)."));

                /// @brief String — SDP @c o= originator username.
                PROMEKI_DECLARE_ID(RtpSessionOrigin,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("SDP originator username (o= line)."));

                /// @brief Enum @ref RtpPacingMode — pacing mechanism used for all streams.
                PROMEKI_DECLARE_ID(RtpPacingMode,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(promeki::RtpPacingMode::Auto)
                                .setEnumType(promeki::RtpPacingMode::Type)
                                .setDescription("RTP pacing mechanism."));

                /// @brief int — multicast TTL applied to the transport.
                PROMEKI_DECLARE_ID(RtpMulticastTTL,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(16))
                                .setRange(int32_t(1), int32_t(255))
                                .setDescription("Multicast TTL."));

                /// @brief String — multicast outgoing interface name (empty = default).
                PROMEKI_DECLARE_ID(RtpMulticastInterface,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Multicast outgoing interface name."));

                /// @brief String — if non-empty, the MediaIO opens this file and
                /// writes the generated SDP session description to it at open time.
                PROMEKI_DECLARE_ID(RtpSaveSdpPath,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("File path to write generated SDP to."));

                /// @brief Polymorphic reader-side SDP input.  Accepts either:
                /// - @c String: interpreted as a filesystem path.
                /// - @ref SdpSession: consumed directly, no filesystem access.
                PROMEKI_DECLARE_ID(RtpSdp,
                        VariantSpec().setTypes({Variant::TypeString, Variant::TypeSdpSession})
                                .setDescription("SDP input: file path (String) or session object (SdpSession)."));

                /// @brief int — reader-side jitter buffer depth in milliseconds.
                PROMEKI_DECLARE_ID(RtpJitterMs,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(50))
                                .setMin(int32_t(0))
                                .setDescription("Reader jitter buffer depth in ms."));

                /// @brief int — reader-side output frame queue capacity.
                PROMEKI_DECLARE_ID(RtpMaxReadQueueDepth,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(4))
                                .setMin(int32_t(1))
                                .setDescription("Reader output frame queue capacity."));

                // --- Video stream ---

                /// @brief SocketAddress — destination for the video stream. Empty = disabled.
                PROMEKI_DECLARE_ID(VideoRtpDestination,
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the video RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(VideoRtpPayloadType,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(96))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Video RTP payload type."));

                /// @brief int — RTP timestamp clock rate in Hz (default 90000).
                PROMEKI_DECLARE_ID(VideoRtpClockRate,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(90000))
                                .setMin(int32_t(1))
                                .setDescription("Video RTP timestamp clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(VideoRtpSsrc,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the video stream (default 46 / EF).
                PROMEKI_DECLARE_ID(VideoRtpDscp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(46))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Video RTP DSCP marking."));

                /// @brief int — target bitrate in bits/sec (0 = compute from descriptor).
                PROMEKI_DECLARE_ID(VideoRtpTargetBitrate,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Video RTP target bitrate in bps (0 = auto)."));

                /// @brief String — raw @c a=fmtp value from the SDP for the video stream.
                PROMEKI_DECLARE_ID(VideoRtpFmtp,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("Raw SDP a=fmtp value for the video stream."));

                // --- Audio stream ---

                /// @brief SocketAddress — destination for the audio stream. Empty = disabled.
                PROMEKI_DECLARE_ID(AudioRtpDestination,
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the audio RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(AudioRtpPayloadType,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(97))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Audio RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default matches @c AudioRate).
                PROMEKI_DECLARE_ID(AudioRtpClockRate,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio RTP clock rate in Hz (0 = match AudioRate)."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(AudioRtpSsrc,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Audio RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the audio stream (default 34 / AF41).
                PROMEKI_DECLARE_ID(AudioRtpDscp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(34))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Audio RTP DSCP marking."));

                /// @brief int — packet time in microseconds (AES67 default 1000).
                PROMEKI_DECLARE_ID(AudioRtpPacketTimeUs,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(1000))
                                .setMin(int32_t(1))
                                .setDescription("Audio RTP packet time in microseconds."));

                // --- Data / metadata stream ---

                /// @brief bool — enable transmission of per-frame Metadata.
                PROMEKI_DECLARE_ID(DataEnabled,
                        VariantSpec().setType(Variant::TypeBool)
                                .setDefault(false)
                                .setDescription("Enable per-frame metadata transmission."));

                /// @brief SocketAddress — destination for the metadata stream. Empty = disabled.
                PROMEKI_DECLARE_ID(DataRtpDestination,
                        VariantSpec().setType(Variant::TypeSocketAddress)
                                .setDescription("Destination for the metadata RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(DataRtpPayloadType,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(100))
                                .setRange(int32_t(0), int32_t(127))
                                .setDescription("Metadata RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default 90000).
                PROMEKI_DECLARE_ID(DataRtpClockRate,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(90000))
                                .setMin(int32_t(1))
                                .setDescription("Metadata RTP clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(DataRtpSsrc,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Metadata RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the metadata stream.
                PROMEKI_DECLARE_ID(DataRtpDscp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(34))
                                .setRange(int32_t(0), int32_t(63))
                                .setDescription("Metadata RTP DSCP marking."));

                /// @brief Enum @ref MetadataRtpFormat — wire format for the metadata stream.
                PROMEKI_DECLARE_ID(DataRtpFormat,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(MetadataRtpFormat::JsonMetadata)
                                .setEnumType(MetadataRtpFormat::Type)
                                .setDescription("Wire format for the metadata RTP stream."));

                // ============================================================
                // V4L2 capture (Linux)
                // ============================================================

                /// @brief String — V4L2 device node path (e.g. "/dev/video0").
                PROMEKI_DECLARE_ID(V4l2DevicePath,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String())
                                .setDescription("V4L2 device node path."));

                /// @brief int — number of MMAP capture buffers (2-32).
                PROMEKI_DECLARE_ID(V4l2BufferCount,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(4))
                                .setRange(int32_t(2), int32_t(32))
                                .setDescription("Number of V4L2 MMAP capture buffers."));

                /// @brief String — ALSA capture device name for paired audio.
                /// "auto" (default) auto-detects a paired USB audio device.
                /// "none" or empty disables audio capture.
                /// Any other value is used as-is (e.g. "hw:1,0", "default").
                PROMEKI_DECLARE_ID(V4l2AudioDevice,
                        VariantSpec().setType(Variant::TypeString)
                                .setDefault(String("auto"))
                                .setDescription("ALSA capture device for paired audio. "
                                        "\"auto\" = auto-detect, \"none\" or empty = disabled."));

                // ---- V4L2 camera controls ----
                //
                // These map directly to V4L2 CID controls.  A value of
                // -1 (the default) means "don't touch, use device default."
                // Actual ranges are device-dependent; see --probe output.

                /// @brief int — Brightness (V4L2_CID_BRIGHTNESS).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Brightness,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Brightness (-1 = device default)."));

                /// @brief int — Contrast (V4L2_CID_CONTRAST).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Contrast,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Contrast (-1 = device default)."));

                /// @brief int — Saturation (V4L2_CID_SATURATION).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Saturation,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Saturation (-1 = device default)."));

                /// @brief int — Hue (V4L2_CID_HUE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Hue,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Hue (-1 = device default)."));

                /// @brief int — Gamma (V4L2_CID_GAMMA).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Gamma,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Gamma (-1 = device default)."));

                /// @brief int — Sharpness (V4L2_CID_SHARPNESS).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Sharpness,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Sharpness (-1 = device default)."));

                /// @brief int — Backlight compensation (V4L2_CID_BACKLIGHT_COMPENSATION).
                /// -1 = device default.
                PROMEKI_DECLARE_ID(V4l2BacklightComp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Backlight compensation (-1 = device default)."));

                /// @brief int — White balance temperature in Kelvin
                /// (V4L2_CID_WHITE_BALANCE_TEMPERATURE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2WhiteBalanceTemp,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("White balance temperature in K (-1 = device default)."));

                /// @brief bool — Auto white balance (V4L2_CID_AUTO_WHITE_BALANCE).
                /// -1 = device default, 0 = off, 1 = on.
                PROMEKI_DECLARE_ID(V4l2AutoWhiteBalance,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setRange(int32_t(-1), int32_t(1))
                                .setDescription("Auto white balance (-1 = device default, 0 = off, 1 = on)."));

                /// @brief int — Exposure time, absolute, in 100µs units
                /// (V4L2_CID_EXPOSURE_ABSOLUTE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2ExposureAbsolute,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Exposure time in 100us units (-1 = device default)."));

                /// @brief Enum @ref V4l2ExposureMode — auto exposure mode (V4L2_CID_EXPOSURE_AUTO).
                PROMEKI_DECLARE_ID(V4l2AutoExposure,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(V4l2ExposureMode::Type)
                                .setDescription("Auto exposure mode (empty = device default)."));

                /// @brief int — Gain (V4L2_CID_GAIN).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Gain,
                        VariantSpec().setType(Variant::TypeS32)
                                .setDefault(int32_t(-1))
                                .setDescription("Gain (-1 = device default)."));

                /// @brief Enum @ref V4l2PowerLineMode — power line frequency filter (V4L2_CID_POWER_LINE_FREQUENCY).
                PROMEKI_DECLARE_ID(V4l2PowerLineFreq,
                        VariantSpec().setType(Variant::TypeEnum)
                                .setDefault(Enum())
                                .setEnumType(V4l2PowerLineMode::Type)
                                .setDescription("Power line frequency (empty = device default)."));

                /// @brief int — JPEG compression quality 1-100
                /// (V4L2_CID_JPEG_COMPRESSION_QUALITY).  -1 = device default.
                /// Not all devices support this control.
                PROMEKI_DECLARE_ID(V4l2JpegQuality,
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
