/**
 * @file      mediaconfig.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <promeki/namespace.h>
#include <promeki/variantdatabase.h>
#include <promeki/color.h>
#include <promeki/enums_anc.h>
#include <promeki/enums_audio.h>
#include <promeki/enums_clock.h>
#include <promeki/enums_codec.h>
#include <promeki/enums_color.h>
#include <promeki/enums_jxs.h>
#include <promeki/enums_mediaio.h>
#include <promeki/enums_ndi.h>
#include <promeki/enums_network.h>
#include <promeki/enums_rtmp.h>
#include <promeki/enums_rtp.h>
#include <promeki/enums_st2110.h>
#include <promeki/enums_subtitle.h>
#include <promeki/enums_tpg.h>
#include <promeki/enums_transcription.h>
#include <promeki/enums_video.h>
#include <promeki/enumlist.h>
#include <promeki/url.h>
#include <promeki/duration.h>
#include <promeki/eui64.h>
#include <promeki/macaddress.h>
#include <promeki/mediaduration.h>
#include <promeki/timecode.h>
#include <promeki/videoformat.h>
#include <promeki/audiochannelmap.h>
#include <promeki/audiocodec.h>
#include <promeki/hdmisignalconfig.h>
#include <promeki/sdioutputfanoutconfig.h>
#include <promeki/sdisignalconfig.h>
#include <promeki/videocodec.h>
#include <promeki/videoreferenceconfig.h>
#include <promeki/pixelaspect.h>
#include <promeki/pixelformat.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Generic name/value configuration container for the media subsystem.
 * @ingroup media
 *
 * Thin subclass of @ref VariantDatabase "VariantDatabase<"MediaConfig">"
 * that adds a canonical catalog of well-known @ref ID constants for every
 * configurable knob in libpromeki.  All media components — @ref MediaIO
 * backends, @ref UncompressedVideoPayload::convert, @ref CSCPipeline —
 * share this single registry so:
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

                /// @brief String — filesystem path to the media resource.
                ///
                /// Set by @ref MediaIO::createForFileRead and
                /// @ref MediaIO::createForFileWrite when the caller
                /// passes a plain path.  When the caller passes a URL
                /// it is handled via @ref MediaIO::createFromUrl
                /// instead, which populates @ref Url rather than
                /// @c Filename — so a MediaIO opened via URL has an
                /// empty @c Filename and a populated @c Url, and
                /// vice versa.
                PROMEKI_DECLARE_ID(Filename, VariantSpec()
                                                     .setType(DataTypeString)
                                                     .setDefault(String())
                                                     .setDescription("Filesystem path to the media resource."));

                /// @brief Url — URL the MediaIO was opened from.
                ///
                /// Populated by @ref MediaIO::createFromUrl (and by
                /// the transparent URL handling in
                /// @ref MediaIO::createForFileRead /
                /// @ref MediaIO::createForFileWrite when the argument
                /// parses as a URL with a registered scheme).  Left
                /// invalid for MediaIOs opened via a plain path or
                /// via @ref MediaIO::create with a Type-keyed
                /// Config.  Useful for logging and for tools that
                /// want to round-trip "show me the URL the user
                /// passed" through @ref MediaIO::config.
                PROMEKI_DECLARE_ID(Url, VariantSpec()
                                                .setType(DataTypeUrl)
                                                .setDefault(promeki::Url())
                                                .setDescription("URL the MediaIO was opened from."));

                /// @brief String — registered backend type name (e.g. "TPG", "ImageFile").
                PROMEKI_DECLARE_ID(Type, VariantSpec()
                                                 .setType(DataTypeString)
                                                 .setDefault(String())
                                                 .setDescription("Registered backend type name."));

                /// @brief Enum @ref MediaIOOpenMode — Read (open a source) or Write (open a sink).
                ///
                /// File-based backends (ImageFile, AudioFile, DebugMedia,
                /// Quicktime, etc.) consult this key during open to decide
                /// whether to register a source or a sink.  Backends that
                /// only support one direction ignore it.  Defaults to
                /// @c Read so the common case of opening an existing
                /// file for playback needs no extra config.
                PROMEKI_DECLARE_ID(OpenMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(MediaIOOpenMode::Type)
                                           .setDefault(MediaIOOpenMode(MediaIOOpenMode::Read))
                                           .setDescription("Read or Write open direction for file-style backends."));

                /// @brief String — human-readable instance name (used in logs and spawned
                /// thread names). The pipeline always seeds this to the stage name; standalone
                /// callers may set it explicitly or leave it empty.
                PROMEKI_DECLARE_ID(Name, VariantSpec()
                                                 .setType(DataTypeString)
                                                 .setDefault(String())
                                                 .setDescription("Human-readable instance name; empty by default."));

                /// @brief FrameRate — stream or target frame rate.
                PROMEKI_DECLARE_ID(FrameRate, VariantSpec()
                                                      .setType(DataTypeFrameRate)
                                                      .setDefault(promeki::FrameRate())
                                                      .setDescription("Stream or target frame rate."));

                // ============================================================
                // Video — shared across backends
                // ============================================================

                /// @brief VideoFormat — combined video raster, frame rate, and scan mode.
                PROMEKI_DECLARE_ID(VideoFormat,
                                   VariantSpec()
                                           .setType(DataTypeVideoFormat)
                                           .setDefault(promeki::VideoFormat())
                                           .setDescription("Combined video raster, frame rate, and scan mode."));

                /// @brief bool — enable video generation / decode.
                PROMEKI_DECLARE_ID(VideoEnabled, VariantSpec()
                                                         .setType(DataTypeBool)
                                                         .setDefault(false)
                                                         .setDescription("Enable video generation or decode."));

                /// @brief Size2Du32 — image dimensions.
                PROMEKI_DECLARE_ID(VideoSize, VariantSpec()
                                                      .setType(DataTypeSize2D)
                                                      .setDefault(Size2Du32())
                                                      .setDescription("Image dimensions."));

                /// @brief PixelFormat — stage video pixel description (target format for
                /// generators, hint for headerless readers).
                PROMEKI_DECLARE_ID(VideoPixelFormat, VariantSpec()
                                                             .setType(DataTypePixelFormat)
                                                             .setDefault(PixelFormat())
                                                             .setDescription("Video pixel description."));

                /// @brief int — 0-based video track index to use (-1 = auto).
                PROMEKI_DECLARE_ID(VideoTrack, VariantSpec()
                                                       .setType(DataTypeInt32)
                                                       .setDefault(int32_t(-1))
                                                       .setMin(int32_t(-1))
                                                       .setDescription("0-based video track index (-1 = auto)."));

                // ============================================================
                // Generic video carrier
                // ============================================================

                /// @brief SdiSignalConfig — SDI input port(s) + SMPTE link standard.
                ///
                /// Hardware MediaIO backends (NTV2, DeckLink, ST 2022-6,
                /// ST 2110-20, …) consult this key during open to bind
                /// the input side to a specific physical SDI port (or
                /// dual/quad-link set) and to lock in the wire format.
                /// File-based and synthetic backends ignore it.
                PROMEKI_DECLARE_ID(SdiInputSignal,
                                   VariantSpec()
                                           .setType(DataTypeSdiSignalConfig)
                                           .setDefault(SdiSignalConfig())
                                           .setDescription(
                                                   "SDI input port(s) and SMPTE link standard "
                                                   "for hardware MediaIO backends."));

                /// @brief SdiSignalConfig — SDI output port(s) + SMPTE link standard.
                PROMEKI_DECLARE_ID(SdiOutputSignal,
                                   VariantSpec()
                                           .setType(DataTypeSdiSignalConfig)
                                           .setDefault(SdiSignalConfig())
                                           .setDescription(
                                                   "SDI output port(s) and SMPTE link standard "
                                                   "for hardware MediaIO backends."));

                /// @brief HdmiSignalConfig — HDMI input port + spec-version hint.
                ///
                /// Hardware MediaIO backends that expose HDMI connectors
                /// (NTV2 Kona HDMI cards, capture cards with HDMI input)
                /// consult this key during open to bind the input side
                /// to a specific physical HDMI connector and choose the
                /// version subset to advertise on negotiation.  Backends
                /// without HDMI ignore it.
                PROMEKI_DECLARE_ID(HdmiInputSignal,
                                   VariantSpec()
                                           .setType(DataTypeHdmiSignalConfig)
                                           .setDefault(HdmiSignalConfig())
                                           .setDescription(
                                                   "HDMI input port and version hint "
                                                   "for hardware MediaIO backends."));

                /// @brief HdmiSignalConfig — HDMI output port + spec-version hint.
                PROMEKI_DECLARE_ID(HdmiOutputSignal,
                                   VariantSpec()
                                           .setType(DataTypeHdmiSignalConfig)
                                           .setDefault(HdmiSignalConfig())
                                           .setDescription(
                                                   "HDMI output port and version hint "
                                                   "for hardware MediaIO backends."));

                /// @brief VideoReferenceConfig — device-wide reference clock config.
                ///
                /// Hardware MediaIO backends consult this key during
                /// open to choose which reference (free-run, GENLOCK,
                /// external, or a specific input signal) the device's
                /// outputs lock to.  File-based and synthetic backends
                /// ignore it.
                PROMEKI_DECLARE_ID(VideoReference,
                                   VariantSpec()
                                           .setType(DataTypeVideoReferenceConfig)
                                           .setDefault(VideoReferenceConfig())
                                           .setDescription(
                                                   "Device-wide reference clock configuration "
                                                   "for hardware MediaIO backends."));

                /// @brief SdiOutputFanoutConfig — fan one outbound
                /// signal across multiple SDI destination groups via
                /// the device's crosspoint fabric (when the backend
                /// supports it).  Each comma-separated group must
                /// carry the same SMPTE link standard, with
                /// @c sdiCableCount(standard) ports each.  When set,
                /// supersedes @c SdiOutputSignal (the fanout's first
                /// group plays the role of the primary; subsequent
                /// groups are mirrors).  String form:
                /// <code>standard:p1+p2,p3+p4</code> — see the type's
                /// doc.  Default empty = no fanout (fall back to
                /// @c SdiOutputSignal alone).  Backends that lack a
                /// crosspoint fabric ignore it.
                PROMEKI_DECLARE_ID(SdiOutputFanout,
                                   VariantSpec()
                                           .setType(DataTypeSdiOutputFanoutConfig)
                                           .setDefault(SdiOutputFanoutConfig())
                                           .setDescription(
                                                   "Multi-destination SDI fanout: one signal driven "
                                                   "out N matching port groups via the device's "
                                                   "crosspoint fabric.  Standard form: "
                                                   "'dl_3g:p1+p2,p3+p4'."));

                // ============================================================
                // Video test pattern generator
                // ============================================================

                /// @brief Enum @ref VideoPattern — selected test pattern.
                PROMEKI_DECLARE_ID(VideoPattern, VariantSpec()
                                                         .setType(DataTypeEnum)
                                                         .setDefault(promeki::VideoPattern::ColorBars)
                                                         .setEnumType(promeki::VideoPattern::Type)
                                                         .setDescription("Selected video test pattern."));

                /// @brief Color — fill color for @c SolidColor pattern.
                PROMEKI_DECLARE_ID(VideoSolidColor, VariantSpec()
                                                            .setType(DataTypeColor)
                                                            .setDefault(Color())
                                                            .setDescription("Fill color for the SolidColor pattern."));

                /// @brief double — horizontal motion pixels/frame.
                PROMEKI_DECLARE_ID(VideoMotion, VariantSpec()
                                                        .setType(DataTypeDouble)
                                                        .setDefault(0.0)
                                                        .setDescription("Horizontal motion in pixels per frame."));

                // ============================================================
                // Video burn-in overlay
                // ============================================================

                /// @brief bool — enable text burn-in.
                PROMEKI_DECLARE_ID(VideoBurnEnabled, VariantSpec()
                                                             .setType(DataTypeBool)
                                                             .setDefault(false)
                                                             .setDescription("Enable text burn-in overlay."));

                /// @brief String — TrueType / OpenType font path for burn-in.
                PROMEKI_DECLARE_ID(VideoBurnFontPath,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("TrueType or OpenType font path for burn-in."));

                /// @brief int — burn-in font size in pixels.
                PROMEKI_DECLARE_ID(VideoBurnFontSize, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(0))
                                                              .setMin(int32_t(0))
                                                              .setDescription("Burn-in font size in pixels."));

                /// @brief String — burn-in text template (VariantLookup<Frame>::format).
                PROMEKI_DECLARE_ID(VideoBurnText,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Burn-in text as a VariantLookup<Frame>::format "
                                                           "template, resolved per-frame against the "
                                                           "frame's metadata.  Use \\n for multi-line."));

                /// @brief Enum @ref BurnPosition — on-screen position.
                PROMEKI_DECLARE_ID(VideoBurnPosition, VariantSpec()
                                                              .setType(DataTypeEnum)
                                                              .setDefault(BurnPosition::BottomCenter)
                                                              .setEnumType(BurnPosition::Type)
                                                              .setDescription("On-screen burn-in text position."));

                /// @brief Color — burn-in text color.
                PROMEKI_DECLARE_ID(VideoBurnTextColor, VariantSpec()
                                                               .setType(DataTypeColor)
                                                               .setDefault(Color())
                                                               .setDescription("Burn-in text color."));

                /// @brief Color — burn-in background color.
                PROMEKI_DECLARE_ID(VideoBurnBgColor, VariantSpec()
                                                             .setType(DataTypeColor)
                                                             .setDefault(Color())
                                                             .setDescription("Burn-in background color."));

                /// @brief bool — draw background rectangle behind burn-in text.
                PROMEKI_DECLARE_ID(VideoBurnDrawBg,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Draw background rectangle behind burn-in text."));

                // ============================================================
                // Subtitle burn-in — Metadata::Subtitle / CEA-608 ANC overlay
                // ============================================================

                /// @brief bool — enable subtitle burn-in (renders the
                ///        active @ref Subtitle cue onto the video).
                PROMEKI_DECLARE_ID(VideoSubtitleBurnEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Enable subtitle burn-in overlay rendering the active "
                                                           "Metadata::Subtitle cue (or a CEA-608 decoded cue when "
                                                           "VideoSubtitleBurnDecodeAnc is set) onto the video."));

                /// @brief String — TrueType / OpenType font path for subtitle burn-in.
                PROMEKI_DECLARE_ID(VideoSubtitleBurnFontPath,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("TrueType or OpenType font path for subtitle burn-in. "
                                                           "Empty = the library's bundled default font."));

                /// @brief int — subtitle burn-in font size in pixels.
                ///        @c 0 (default) auto-scales from frame height.
                PROMEKI_DECLARE_ID(VideoSubtitleBurnFontSize,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Subtitle burn-in font size in pixels (0 = auto from "
                                                           "frame height)."));

                /// @brief Color — subtitle text colour (fallback when a
                ///        span carries no explicit colour).
                PROMEKI_DECLARE_ID(VideoSubtitleBurnTextColor,
                                   VariantSpec()
                                           .setType(DataTypeColor)
                                           .setDefault(Color::White)
                                           .setDescription("Default subtitle text colour; spans with an explicit "
                                                           "SubtitleSpan::color override this."));

                /// @brief Color — subtitle background colour.
                PROMEKI_DECLARE_ID(VideoSubtitleBurnBgColor,
                                   VariantSpec()
                                           .setType(DataTypeColor)
                                           .setDefault(Color::Black)
                                           .setDescription("Subtitle background colour (used behind the cue "
                                                           "when VideoSubtitleBurnDrawBg is set)."));

                /// @brief bool — draw a background rectangle behind the subtitle.
                PROMEKI_DECLARE_ID(VideoSubtitleBurnDrawBg,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Draw a solid background rectangle behind the subtitle "
                                                           "cue for legibility."));

                /// @brief Enum @ref SubtitleAnchor — override the cue's
                ///        anchor.  @c Default honours the cue's own
                ///        @ref Subtitle::anchor (which itself falls
                ///        back to @c BottomCenter when @c Default).
                PROMEKI_DECLARE_ID(VideoSubtitleBurnAnchor,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(SubtitleAnchor::Default)
                                           .setEnumType(SubtitleAnchor::Type)
                                           .setDescription("Anchor override for subtitle burn-in.  Default = "
                                                           "honour the cue's Subtitle::anchor (which falls back "
                                                           "to BottomCenter when itself Default)."));

                /// @brief EnumList @ref SubtitleSource — ordered
                ///        preference list of cue sources for subtitle
                ///        burn-in.
                ///
                /// The renderer queries each source in turn and paints
                /// the first cue it finds.  An empty list disables
                /// rendering entirely (effectively the same as
                /// @ref VideoSubtitleBurnEnabled @c = @c false).
                ///
                /// Default is @c [Metadata] — frame-stamped cues only,
                /// which is what producers like @c TpgMediaIO emit
                /// today.  Add @c Cea608Anc to fall back to (or prefer)
                /// CEA-608 decoded from the frame's @c AncPayloads.
                ///
                /// @par Example
                /// @code
                /// // Prefer the in-band ANC decode, fall back to
                /// // any metadata-stamped cue.
                /// EnumList sources = EnumList::forType<SubtitleSource>();
                /// sources.append(SubtitleSource::Cea608Anc);
                /// sources.append(SubtitleSource::Metadata);
                /// cfg.set(MediaConfig::VideoSubtitleBurnSources, sources);
                /// // Equivalent string form on the command line:
                /// //   VideoSubtitleBurnSources=Cea608Anc,Metadata
                /// @endcode
                PROMEKI_DECLARE_ID(VideoSubtitleBurnSources,
                                   VariantSpec()
                                           .setType(DataTypeEnumList)
                                           .setDefault([] {
                                                   EnumList l = EnumList::forType<SubtitleSource>();
                                                   l.append(SubtitleSource::Metadata);
                                                   return l;
                                           }())
                                           .setEnumType(SubtitleSource::Type)
                                           .setDescription("Ordered preference list of subtitle cue sources for "
                                                           "burn-in.  Queries each source in turn and paints the "
                                                           "first cue it finds.  Empty list disables rendering."));

                // ============================================================
                // Video motion band — scrolling marker for stutter detection
                // ============================================================

                /// @brief bool — enable the @ref MotionBand overlay.
                PROMEKI_DECLARE_ID(VideoMotionBandEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Enable the scrolling motion band overlay used to "
                                                           "make frame stutter / drop / repeat visually obvious."));

                /// @brief int — motion band height in scan lines (0 = default).
                PROMEKI_DECLARE_ID(VideoMotionBandHeight,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Motion band height in scan lines (0 = default)."));

                // ============================================================
                // Audio — shared across backends
                // ============================================================

                /// @brief bool — enable audio generation / decode.
                PROMEKI_DECLARE_ID(AudioEnabled, VariantSpec()
                                                         .setType(DataTypeBool)
                                                         .setDefault(false)
                                                         .setDescription("Enable audio generation or decode."));

                /// @brief float — audio sample rate in Hz.
                PROMEKI_DECLARE_ID(AudioRate, VariantSpec()
                                                      .setType(DataTypeFloat)
                                                      .setDefault(0.0f)
                                                      .setMin(0.0f)
                                                      .setDescription("Audio sample rate in Hz."));

                /// @brief int — audio channel count.
                PROMEKI_DECLARE_ID(AudioChannels, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(0))
                                                          .setMin(int32_t(0))
                                                          .setDescription("Audio channel count."));

                /// @brief int — 0-based audio track index to use (-1 = auto).
                PROMEKI_DECLARE_ID(AudioTrack, VariantSpec()
                                                       .setType(DataTypeInt32)
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
                                   VariantSpec()
                                           .setType(DataTypeEnumList)
                                           .setDefault(EnumList::forType<AudioPattern>())
                                           .setEnumType(AudioPattern::Type)
                                           .setDescription("Comma-separated list of per-channel audio test "
                                                           "patterns (extra channels silenced)."));

                /// @brief double — tone frequency in Hz (used by Tone / AvSync).
                PROMEKI_DECLARE_ID(AudioToneFrequency,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(1000.0)
                                           .setMin(0.0)
                                           .setDescription("Tone frequency in Hz (Tone / AvSync channels)."));

                /// @brief double — tone level in dBFS.
                PROMEKI_DECLARE_ID(AudioToneLevel, VariantSpec()
                                                           .setType(DataTypeDouble)
                                                           .setDefault(-20.0)
                                                           .setMax(0.0)
                                                           .setDescription("Tone level in dBFS."));

                /// @brief double — LTC burn-in level in dBFS.
                PROMEKI_DECLARE_ID(AudioLtcLevel, VariantSpec()
                                                          .setType(DataTypeDouble)
                                                          .setDefault(-20.0)
                                                          .setMax(0.0)
                                                          .setDescription("LTC burn-in level in dBFS."));

                /// @brief double — ChannelId base frequency in Hz.
                ///        Channel @em N carries a sine at
                ///        `base + N * step`.
                PROMEKI_DECLARE_ID(AudioChannelIdBaseFreq,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(1000.0)
                                           .setMin(0.0)
                                           .setDescription("ChannelId base tone frequency in Hz."));

                /// @brief double — ChannelId per-channel step in Hz.
                PROMEKI_DECLARE_ID(AudioChannelIdStepFreq,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(100.0)
                                           .setMin(0.0)
                                           .setDescription("ChannelId per-channel tone step in Hz."));

                /// @brief double — Chirp sweep start frequency in Hz.
                PROMEKI_DECLARE_ID(AudioChirpStartFreq,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(20.0)
                                           .setMin(0.0)
                                           .setDescription("Chirp log-sweep start frequency in Hz."));

                /// @brief double — Chirp sweep end frequency in Hz.
                PROMEKI_DECLARE_ID(AudioChirpEndFreq, VariantSpec()
                                                              .setType(DataTypeDouble)
                                                              .setDefault(20000.0)
                                                              .setMin(0.0)
                                                              .setDescription("Chirp log-sweep end frequency in Hz."));

                /// @brief double — Chirp sweep period in seconds.
                PROMEKI_DECLARE_ID(AudioChirpDurationSec,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(1.0)
                                           .setMin(0.0)
                                           .setDescription("Chirp log-sweep period in seconds."));

                /// @brief double — DualTone low-side frequency in Hz.
                PROMEKI_DECLARE_ID(
                        AudioDualToneFreq1,
                        VariantSpec()
                                .setType(DataTypeDouble)
                                .setDefault(60.0)
                                .setMin(0.0)
                                .setDescription("DualTone low-side frequency in Hz (SMPTE IMD default 60 Hz)."));

                /// @brief double — DualTone high-side frequency in Hz.
                PROMEKI_DECLARE_ID(
                        AudioDualToneFreq2,
                        VariantSpec()
                                .setType(DataTypeDouble)
                                .setDefault(7000.0)
                                .setMin(0.0)
                                .setDescription("DualTone high-side frequency in Hz (SMPTE IMD default 7 kHz)."));

                /// @brief double — DualTone amplitude ratio freq2 / freq1.
                PROMEKI_DECLARE_ID(AudioDualToneRatio,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(0.25)
                                           .setMin(0.0)
                                           .setDescription("DualTone amplitude ratio of freq2 to freq1 "
                                                           "(SMPTE IMD default 0.25 = 4:1)."));

                /// @brief double — WhiteNoise / PinkNoise buffer length in seconds.
                PROMEKI_DECLARE_ID(AudioNoiseBufferSec,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(10.0)
                                           .setMin(0.0)
                                           .setDescription("WhiteNoise / PinkNoise cached buffer length in seconds."));

                /// @brief uint32 — PRNG seed used to build the noise buffers.
                PROMEKI_DECLARE_ID(AudioNoiseSeed, VariantSpec()
                                                           .setType(DataTypeUInt32)
                                                           .setDefault(uint32_t(0x505244A4u))
                                                           .setDescription("WhiteNoise / PinkNoise PRNG seed."));

                // ============================================================
                // Timecode
                // ============================================================

                /// @brief bool — enable timecode generation.
                PROMEKI_DECLARE_ID(TimecodeEnabled, VariantSpec()
                                                            .setType(DataTypeBool)
                                                            .setDefault(false)
                                                            .setDescription("Enable timecode generation."));

                /// @brief String — starting timecode (SMPTE "HH:MM:SS:FF" form).
                PROMEKI_DECLARE_ID(TimecodeStart,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Starting timecode in SMPTE HH:MM:SS:FF form."));

                /// @brief Timecode — pre-built starting timecode (alternative to @c TimecodeStart).
                PROMEKI_DECLARE_ID(TimecodeValue, VariantSpec()
                                                          .setType(DataTypeTimecode)
                                                          .setDefault(Timecode())
                                                          .setDescription("Pre-built starting timecode."));

                /// @brief bool — drop-frame flag for 29.97 / 59.94 timecode.
                PROMEKI_DECLARE_ID(TimecodeDropFrame,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Drop-frame flag for 29.97 / 59.94 timecode."));

                /// @brief MediaDuration — playback in/out range (start frame plus length).
                ///
                /// When set, a backend that supports it should expose only
                /// the requested range, returning EOF after the last frame
                /// in the range.  An @c Unknown or default value disables
                /// the limiter (full asset is read).  Range form
                /// @c "<start>-<end>" inclusive is also accepted via the
                /// MediaDuration string parser.
                PROMEKI_DECLARE_ID(PlaybackRange,
                                   VariantSpec()
                                           .setType(DataTypeMediaDuration)
                                           .setDefault(MediaDuration())
                                           .setDescription("Playback in/out range (start frame plus length)."));

                // ============================================================
                // Image data encoder (VITC-style binary stamp on top of video)
                // ============================================================

                /// @brief uint32 — opaque per-stream identifier.  Combined with the
                /// rolling frame number into the frame-ID payload of the
                /// @ref ImageDataEncoder when @ref TpgDataEncoderEnabled is true.
                /// Defaults to 0; use any value the application finds convenient
                /// for cross-stream correlation.
                PROMEKI_DECLARE_ID(StreamID, VariantSpec()
                                                     .setType(DataTypeUInt32)
                                                     .setDefault(uint32_t(0))
                                                     .setDescription("Opaque per-stream identifier (uint32)."));

                /// @brief bool — enable the @ref ImageDataEncoder pass on TPG video
                /// frames.  When true (default), the TPG stamps two 64-bit
                /// payloads into the top of every generated video frame:
                /// (1) @c (StreamID << 32) | frameNumber, and
                /// (2) the @ref Timecode::toBcd64 BCD timecode word.
                PROMEKI_DECLARE_ID(TpgDataEncoderEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Enable VITC-style binary data encoder pass on TPG video."));

                /// @brief int — number of scan lines each @ref ImageDataEncoder item
                /// occupies in the encoded band.  The TPG emits two items, so the
                /// total stamped band height is @c 2 * TpgDataEncoderRepeatLines
                /// scan lines starting from the top of the image.  Default 16,
                /// which gives a comfortable read margin for a noisy decoder
                /// without consuming too much picture area.
                PROMEKI_DECLARE_ID(TpgDataEncoderRepeatLines,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(16))
                                           .setMin(int32_t(1))
                                           .setDescription("Scan lines per ImageDataEncoder item in TPG."));

                /// @brief bool — enable CEA-708 caption injection on TPG.
                /// When true, the TPG parses @ref TpgAncCaptionsFile,
                /// drives a @ref Cea608Encoder against the cue timeline at
                /// the configured frame rate, wraps the per-frame
                /// @c CcDataList into a @ref Cea708Cdp, and attaches the
                /// resulting @ref AncPacket to a fresh @ref AncPayload on
                /// the produced Frame.  When no file is configured (or the
                /// file is empty) the TPG still emits per-frame CDPs
                /// carrying null caption pairs so the receiver sees a
                /// steady stream.
                PROMEKI_DECLARE_ID(TpgAncCaptionsEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Enable CEA-708 caption ANC injection on TPG frames."));

                /// @brief String — path to a SubRip (`.srt`) file whose
                /// cues drive the per-frame caption byte stream.  Path
                /// resolution goes through @ref File so `:/...` resource
                /// paths and @ref Dir::temp-relative paths both work.
                /// Empty (default) means "no file" — the TPG still emits
                /// per-frame null CDPs when @ref TpgAncCaptionsEnabled is
                /// true.
                PROMEKI_DECLARE_ID(TpgAncCaptionsFile,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Path to SubRip file driving CEA-608 captions on TPG."));

                /// @brief Duration — offset added to every cue's
                /// @c start / @c end before scheduling.  Positive values
                /// delay captions relative to the TPG's frame timeline
                /// (which always starts at t=0 at TPG open); negative
                /// values advance them.  Use this when the SubRip file
                /// was authored against a different reference point
                /// (e.g. a broadcast hour-of-day TC) and needs to be
                /// re-anchored to the TPG's t=0.  Default zero —
                /// SubRip cue times are interpreted directly as media-
                /// relative offsets from TPG frame 0.
                PROMEKI_DECLARE_ID(TpgAncCaptionsOffset,
                                   VariantSpec()
                                           .setType(DataTypeDuration)
                                           .setDefault(Duration::zero())
                                           .setDescription("Offset applied to SubRip cue times before scheduling."));

                /// @brief int — VANC line number the TPG stamps on emitted
                /// CEA-708 ANC packets (via @c AncPacket::st291Line()).
                /// Default 11, which is the canonical VANC line for HD
                /// CEA-708 carriage.
                PROMEKI_DECLARE_ID(TpgAncCaptionsLine,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(11))
                                           .setMin(int32_t(0))
                                           .setDescription("VANC line number stamped on TPG CEA-708 ANC packets."));

                /// @brief Enum @ref CaptionCodec — selects which caption
                /// codec(s) the TPG drives into the per-frame @c CcDataList.
                /// Default @c Cea608 (line-21 byte pairs).  @c Cea708 drives
                /// @ref Cea708Encoder for DTVCC pop-on transactions.
                /// @c Both runs the 608 and 708 encoders side-by-side so the
                /// emitted CDP carries both wire forms (mirrors real SDI
                /// broadcast practice).  Ignored when @ref TpgAncCaptionsScc
                /// is set (SCC bypass is 608-only).
                PROMEKI_DECLARE_ID(TpgAncCaptionsCodec,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::CaptionCodec::Cea608)
                                           .setEnumType(promeki::CaptionCodec::Type)
                                           .setDescription("CEA caption codec(s) the TPG emits into the CDP cc_data."));

                /// @brief int — DTVCC service number (1..63) the TPG's
                /// @ref Cea708Encoder targets when @ref TpgAncCaptionsCodec
                /// is @c Cea708 or @c Both.  Default 1 (the primary English
                /// caption service).
                PROMEKI_DECLARE_ID(TpgAncCaptions708Service,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(1))
                                           .setMin(int32_t(1))
                                           .setMax(int32_t(63))
                                           .setDescription("DTVCC service number for TPG CEA-708 caption emission."));

                /// @brief String — path to a Scenarist SCC (`.scc`) file
                /// whose byte-pair rows drive the TPG's per-frame caption
                /// payload directly, bypassing the @ref Cea608Encoder.
                /// When set, the SCC rows are looked up by frame number
                /// (the first row anchors to TPG frame 0); the matching
                /// byte pairs ride into the @c Cea708Cdp's cc_data list
                /// verbatim.  Mutually exclusive with
                /// @ref TpgAncCaptionsFile (SubRip path) — set one or
                /// the other.  This is the "real broadcast captioner
                /// output" test path: it proves the CDP wire layer
                /// independently of the encoder's scheduling decisions.
                /// Empty (default) means "no SCC override" — the
                /// SubRip + encoder path takes over when
                /// @ref TpgAncCaptionsFile is set.
                PROMEKI_DECLARE_ID(TpgAncCaptionsScc,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Path to Scenarist SCC file feeding TPG CEA-708 ANC bytes directly."));

                // ============================================================
                // Inspector sink (InspectorMediaIO)
                // ============================================================

                /// @brief bool — drop incoming frames after running checks.  When
                /// true, the inspector behaves as a pure null sink for upstream
                /// pacing purposes; the per-frame events and accumulator stats
                /// are still produced as long as the corresponding decoders are
                /// enabled.
                PROMEKI_DECLARE_ID(InspectorDropFrames,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Inspector drops frames after checks (sink behaviour)."));

                /// @brief EnumList @ref InspectorTest — list of inspector
                /// tests to run.
                ///
                /// The default lists every known test
                /// (@c ImageData, @c AudioData, @c AvSync,
                /// @c Continuity, @c Timestamp, @c AudioSamples) so
                /// a default-configured inspector runs the full
                /// suite.  Set to a shorter list to disable tests;
                /// an empty list disables every test.
                /// Dependencies are still auto-resolved (e.g.
                /// asking for @c AvSync implicitly also enables
                /// @c ImageData and @c AudioData).
                ///
                /// @par Example
                /// @code
                /// // Only run the timestamp and A/V sync checks:
                /// EnumList tests = EnumList::forType<InspectorTest>();
                /// tests.append(InspectorTest::Timestamp);
                /// tests.append(InspectorTest::AvSync);
                /// cfg.set(MediaConfig::InspectorTests, tests);
                /// // Equivalent string form on the command line:
                /// //   InspectorTests=Timestamp,AvSync
                /// @endcode
                PROMEKI_DECLARE_ID(InspectorTests, VariantSpec()
                                                           .setType(DataTypeEnumList)
                                                           .setDefault([] {
                                                                   EnumList l = EnumList::forType<InspectorTest>();
                                                                   l.append(InspectorTest::ImageData);
                                                                   l.append(InspectorTest::AudioData);
                                                                   l.append(InspectorTest::AvSync);
                                                                   l.append(InspectorTest::Continuity);
                                                                   l.append(InspectorTest::Timestamp);
                                                                   l.append(InspectorTest::AudioSamples);
                                                                   return l;
                                                           }())
                                                           .setEnumType(InspectorTest::Type)
                                                           .setDescription("List of inspector tests to run."));

                /// @brief int — maximum allowed sample-to-sample change in the
                /// marker-based A/V sync offset before the inspector flags a
                /// discontinuity.  Only meaningful when the @c AvSync test
                /// is enabled.
                ///
                /// Default 0: the inspector reports the offset as the
                /// audio codeword's deviation from the rational-rate
                /// cadence (computed via
                /// @ref FrameRate::cumulativeTicks), so a clean stream
                /// sits at exactly 0 regardless of NTSC cadence
                /// (1601/1602/1601/1602/1602 at 48k @ 29.97 stops
                /// wobbling the offset because both sides of the
                /// formula are in the same audio-sample domain).
                /// Strict (0-sample) tolerance is therefore the right
                /// default and any frame-to-frame change reflects a
                /// real audio-side shift (codeword moved within a
                /// chunk, audio sample dropped/inserted).  Pipelines
                /// with known sub-sample jitter (e.g. an SRC
                /// re-clocking the audio) can raise this.
                PROMEKI_DECLARE_ID(InspectorSyncOffsetToleranceSamples,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Max allowed sample-to-sample change in "
                                                           "the marker-based A/V sync offset before "
                                                           "flagging a discontinuity (default 0 — "
                                                           "the offset is cadence-free, so any "
                                                           "movement is a real shift)."));

                /// @brief int — scan lines per @ref ImageDataEncoder band.  Must
                /// match the encoder's @ref TpgDataEncoderRepeatLines so the
                /// inspector reads at the right band offsets.  Default 16.
                PROMEKI_DECLARE_ID(InspectorImageDataRepeatLines,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(16))
                                           .setMin(int32_t(1))
                                           .setDescription("Scan lines per ImageDataDecoder band in Inspector."));

                /// @brief int — audio channel index that carries LTC; default 0.
                PROMEKI_DECLARE_ID(InspectorLtcChannel,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio channel index carrying LTC for the inspector."));

                /// @brief double — periodic-summary log interval in seconds (wall
                /// time, not media time).  Default 1.0.  Set to 0 to disable
                /// periodic logging entirely; per-frame events are still produced.
                PROMEKI_DECLARE_ID(InspectorLogIntervalSec,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(1.0)
                                           .setMin(0.0)
                                           .setDescription("Inspector periodic-summary log interval, seconds."));

                /// @brief int64 — maximum tolerated divergence between an
                /// incoming audio chunk's @ref MediaTimeStamp and the
                /// inspector's prediction (audio stream anchor + cumulative
                /// samples × sample period), in nanoseconds.  Within this
                /// window the divergence is silently absorbed as a jitter
                /// statistic; beyond it the inspector emits an
                /// @c AudioTimestampReanchor discontinuity and re-anchors
                /// the audio stream timeline on the new PTS.  Default
                /// 5,000,000 (5 ms — about 240 samples at 48 kHz, well
                /// below typical sender-side timestamp noise).
                PROMEKI_DECLARE_ID(InspectorAudioPtsToleranceNs,
                                   VariantSpec()
                                           .setType(DataTypeInt64)
                                           .setDefault(int64_t(5'000'000))
                                           .setMin(int64_t(0))
                                           .setDescription("Max tolerated audio PTS deviation from prediction "
                                                           "before re-anchoring (nanoseconds)."));

                /// @brief int64 — maximum tolerated divergence between this
                /// frame's video @ref MediaTimeStamp and the inspector's
                /// prediction (video anchor + frame index × frame duration),
                /// in nanoseconds.  Within this window the divergence is a
                /// jitter statistic; beyond it the inspector emits a
                /// @c VideoTimestampReanchor discontinuity and re-anchors
                /// the video timeline on the new PTS.  Default 5,000,000.
                PROMEKI_DECLARE_ID(InspectorVideoPtsToleranceNs,
                                   VariantSpec()
                                           .setType(DataTypeInt64)
                                           .setDefault(int64_t(5'000'000))
                                           .setMin(int64_t(0))
                                           .setDescription("Max tolerated video PTS deviation from prediction "
                                                           "before re-anchoring (nanoseconds)."));

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
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Output file for Inspector CaptureStats test "
                                                           "(TSV, one row per frame).  Empty = auto-name "
                                                           "in Dir::temp()."));

                /// @brief String — output JSON-Lines file for the
                /// Inspector @c AncData test.  One JSON object per
                /// frame, each carrying the frame index plus the array
                /// of decoded ANC packets ({format, transport, line,
                /// meta, parsed: <typed JSON>}).  Empty = auto-name in
                /// @c Dir::temp() (form:
                /// @c promeki_inspector_anc_<pid>_<epoch_ns>.jsonl).
                /// The resolved path is logged at @c Info level when
                /// the file is opened.
                PROMEKI_DECLARE_ID(InspectorAncDataFile,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Output JSONL file for Inspector AncData test "
                                                           "(one JSON object per frame).  Empty = auto-name "
                                                           "in Dir::temp()."));

                // ============================================================
                // NullPacing sink (NullPacingMediaIO)
                // ============================================================

                /// @brief Enum @ref NullPacingMode — pacing strategy for the
                /// @ref NullPacingMediaIO sink.
                ///
                /// @c Wallclock (default) consumes one frame per
                /// @c 1/NullPacingTargetFps wall-clock interval and
                /// drops anything arriving inside the same interval.
                /// @c Free drains incoming frames at whatever rate the
                /// upstream feeds.  See @ref NullPacingMode for the
                /// detailed semantics.
                PROMEKI_DECLARE_ID(NullPacingMode, VariantSpec()
                                                           .setType(DataTypeEnum)
                                                           .setDefault(promeki::NullPacingMode::Wallclock)
                                                           .setEnumType(promeki::NullPacingMode::Type)
                                                           .setDescription("Pacing strategy for the NullPacing sink "
                                                                           "(Wallclock = drop-between-ticks, Free = "
                                                                           "drain at upstream rate)."));

                /// @brief Rational — target sink rate for the
                /// @ref NullPacingMediaIO sink, in frames per
                /// second.  Default @c 0/1 means "follow the source
                /// descriptor": the sink reads the frame rate from the
                /// upstream @ref MediaDesc cached at @c open() time.
                /// Ignored in @c promeki::NullPacingMode::Free mode.
                PROMEKI_DECLARE_ID(NullPacingTargetFps, VariantSpec()
                                                                .setType(DataTypeRational)
                                                                .setDefault(Rational<int>(0, 1))
                                                                .setDescription("Target frame-consumption rate for the "
                                                                                "NullPacing sink (frames per second).  "
                                                                                "0/1 = follow the source descriptor."));

                /// @brief bool — when true, the @ref NullPacingMediaIO
                /// sink emits a per-frame debug log entry showing the
                /// measured period and jitter (interval since the
                /// previous consumption minus the configured period).
                /// Default false.  Useful for demos and pacing
                /// diagnostics; intentionally noisy when enabled.
                PROMEKI_DECLARE_ID(NullPacingBurnTimings,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("NullPacing sink logs per-frame jitter / "
                                                           "period at debug level when true."));

                // ============================================================
                // MJPEG stream sink (MjpegStreamMediaIO)
                // ============================================================

                /// @brief Rational — maximum encode rate for the
                /// @ref MjpegStreamMediaIO sink, in frames per
                /// second.  Frames arriving inside @c 1/MjpegMaxFps of
                /// the previously-encoded frame are dropped before
                /// JPEG encoding so the sink can throttle a fast
                /// upstream to a preview-friendly rate.  Default
                /// @c 15/1.  The sentinel @c 0/1 disables the rate
                /// gate entirely (every frame is encoded).
                PROMEKI_DECLARE_ID(MjpegMaxFps, VariantSpec()
                                                        .setType(DataTypeRational)
                                                        .setDefault(Rational<int>(15, 1))
                                                        .setDescription("Maximum encode rate for the MjpegStream "
                                                                        "sink (frames per second).  0/1 = no rate "
                                                                        "limit."));

                /// @brief int — JPEG quality (1-100) used by the
                /// @ref MjpegStreamMediaIO sink when encoding
                /// frames.  Default @c 80.  Forwarded verbatim to
                /// @ref JpegVideoEncoder via @ref MediaConfig::JpegQuality
                /// at session creation.
                PROMEKI_DECLARE_ID(MjpegQuality, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(80))
                                                         .setMin(int32_t(1))
                                                         .setMax(int32_t(100))
                                                         .setDescription("JPEG quality used by the MjpegStream "
                                                                         "sink (1-100)."));

                /// @brief int — depth of the latest-N ring of
                /// encoded frames retained by the
                /// @ref MjpegStreamMediaIO sink.  Subscribers
                /// always receive the newest frame; the ring keeps
                /// recent history so freshly-attached subscribers can
                /// be primed with the latest frame without waiting for
                /// the next encode.  Default @c 1, range 1-16.
                PROMEKI_DECLARE_ID(MjpegMaxQueueFrames,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(1))
                                           .setMin(int32_t(1))
                                           .setMax(int32_t(16))
                                           .setDescription("Latest-N ring depth for the MjpegStream "
                                                           "sink (1-16)."));

                // ============================================================
                // CSC (CscMediaIO)
                // ============================================================

                /// @brief PixelFormat — target pixel description for the converter
                /// stage (@c Invalid = video pass-through).
                PROMEKI_DECLARE_ID(OutputPixelFormat,
                                   VariantSpec()
                                           .setType(DataTypePixelFormat)
                                           .setDefault(PixelFormat())
                                           .setDescription("Target pixel description (Invalid = pass-through)."));

                /// @brief Enum @ref AudioDataType — target audio sample format
                /// (@c Invalid = audio pass-through).
                PROMEKI_DECLARE_ID(OutputAudioDataType,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(Enum())
                                           .setEnumType(AudioDataType::Type)
                                           .setDescription("Target audio sample format (Invalid = pass-through)."));

                /// @brief int — internal FIFO capacity in frames.
                PROMEKI_DECLARE_ID(Capacity, VariantSpec()
                                                     .setType(DataTypeInt32)
                                                     .setDefault(int32_t(0))
                                                     .setMin(int32_t(0))
                                                     .setDescription("Internal FIFO capacity in frames."));

                // ============================================================
                // FrameSync (FrameSyncMediaIO)
                // ============================================================

                /// @brief FrameRate — output frame rate for the FrameSync task.
                /// When invalid (default), the output rate is inherited from
                /// @c pendingMediaDesc — i.e. the source rate passes through.
                /// Set to a valid FrameRate to resync to a different cadence.
                PROMEKI_DECLARE_ID(OutputFrameRate, VariantSpec()
                                                            .setType(DataTypeFrameRate)
                                                            .setDefault(promeki::FrameRate())
                                                            .setDescription("FrameSync output frame rate "
                                                                            "(invalid = inherit from source)."));

                /// @brief float — output audio sample rate for the FrameSync task.
                /// When zero (default), the sample rate is inherited from
                /// @c pendingMediaDesc.  Set to a positive value to resample
                /// to a different rate.
                PROMEKI_DECLARE_ID(OutputAudioRate, VariantSpec()
                                                            .setType(DataTypeFloat)
                                                            .setDefault(0.0f)
                                                            .setMin(0.0f)
                                                            .setDescription("FrameSync output audio sample rate "
                                                                            "(0 = inherit from source)."));

                /// @brief int — output audio channel count for the FrameSync task.
                /// When zero (default), the channel count is inherited from
                /// @c pendingMediaDesc.
                PROMEKI_DECLARE_ID(OutputAudioChannels, VariantSpec()
                                                                .setType(DataTypeInt32)
                                                                .setDefault(int32_t(0))
                                                                .setMin(int32_t(0))
                                                                .setDescription("FrameSync output audio channel count "
                                                                                "(0 = inherit from source)."));

                /// @brief int — input queue depth for the FrameSync task.
                PROMEKI_DECLARE_ID(InputQueueCapacity, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(8))
                                                               .setMin(int32_t(1))
                                                               .setDescription("FrameSync input queue depth."));

                // ============================================================
                // FrameBridge (cross-process shared-memory frame transport)
                // ============================================================

                /// @brief String — logical bridge name.  Required.  Identifies
                /// the FrameBridge output that inputs connect to.
                PROMEKI_DECLARE_ID(FrameBridgeName, VariantSpec()
                                                            .setType(DataTypeString)
                                                            .setDefault(String())
                                                            .setDescription("FrameBridge logical name (required)."));

                /// @brief int — number of ring-buffer slots.  Default 2 (ping-pong).
                PROMEKI_DECLARE_ID(FrameBridgeRingDepth, VariantSpec()
                                                                 .setType(DataTypeInt32)
                                                                 .setDefault(int32_t(2))
                                                                 .setMin(int32_t(2))
                                                                 .setDescription("FrameBridge ring-buffer depth."));

                /// @brief int — per-slot metadata reserve bytes (default 64 KiB).
                PROMEKI_DECLARE_ID(FrameBridgeMetadataReserveBytes,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(64 * 1024))
                                           .setMin(int32_t(512))
                                           .setDescription("FrameBridge metadata reserve per slot, bytes."));

                /// @brief double — extra audio capacity fraction above worst-case
                /// samples-per-frame (default 0.20).
                PROMEKI_DECLARE_ID(FrameBridgeAudioHeadroomFraction,
                                   VariantSpec()
                                           .setType(DataTypeDouble)
                                           .setDefault(0.20)
                                           .setMin(0.0)
                                           .setDescription("FrameBridge audio headroom fraction."));

                /// @brief int — POSIX file mode for the shm and socket (default 0600).
                PROMEKI_DECLARE_ID(FrameBridgeAccessMode, VariantSpec()
                                                                  .setType(DataTypeInt32)
                                                                  .setDefault(int32_t(0600))
                                                                  .setDescription("FrameBridge POSIX access mode."));

                /// @brief String — group name for cross-user access (empty = skip).
                PROMEKI_DECLARE_ID(FrameBridgeGroupName,
                                   VariantSpec()
                                           .setType(DataTypeString)
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
                PROMEKI_DECLARE_ID(FrameBridgeSyncMode, VariantSpec()
                                                                .setType(DataTypeBool)
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
                PROMEKI_DECLARE_ID(
                        FrameBridgeWaitForConsumer,
                        VariantSpec()
                                .setType(DataTypeBool)
                                .setDefault(true)
                                .setDescription("FrameBridge output: block writeFrame until consumer connects."));

                // ============================================================
                // JPEG codec
                // ============================================================

                /// @brief int — JPEG quality 1-100 (codec default: 85).
                PROMEKI_DECLARE_ID(JpegQuality, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(85))
                                                        .setRange(int32_t(1), int32_t(100))
                                                        .setDescription("JPEG quality 1-100."));

                /// @brief Enum @ref ChromaSubsampling — JPEG chroma subsampling
                /// (codec default: 4:2:2, RFC 2435 compatible).
                PROMEKI_DECLARE_ID(JpegSubsampling, VariantSpec()
                                                            .setType(DataTypeEnum)
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
                                   VariantSpec()
                                           .setTypes({DataTypeInt32, DataTypeFloat, DataTypeDouble})
                                           .setDefault(int32_t(3))
                                           .setMin(1)
                                           .setDescription("JPEG XS target bits per pixel."));

                /// @brief int — JPEG XS horizontal decomposition depth (0-5,
                /// codec default: 5).  Higher values trade encode cost for
                /// quality.  See @c ndecomp_h in SvtJpegxsEnc.h.
                PROMEKI_DECLARE_ID(JpegXsDecomposition,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
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
                /// Honoured by @c RateControlMode::CBR and
                /// @c RateControlMode::VBR.  Ignored by
                /// @c RateControlMode::CQP.  Codec default: 5000.
                PROMEKI_DECLARE_ID(BitrateKbps, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(5000))
                                                        .setMin(int32_t(1))
                                                        .setDescription("Target / average bitrate in kbit/s."));

                /// @brief int — maximum (peak) bitrate in kbit/s.  Only
                /// meaningful for @c RateControlMode::VBR; CBR ignores
                /// it, CQP ignores it.  Codec default: 0 (no cap).
                PROMEKI_DECLARE_ID(MaxBitrateKbps,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Peak bitrate in kbit/s (VBR only; 0 = uncapped)."));

                /// @brief Enum @ref RateControlMode — rate-control mode.
                /// Codec default: VBR.
                PROMEKI_DECLARE_ID(VideoRcMode, VariantSpec()
                                                        .setType(DataTypeEnum)
                                                        .setDefault(promeki::RateControlMode::VBR)
                                                        .setEnumType(promeki::RateControlMode::Type)
                                                        .setDescription("Video rate-control mode (CBR / VBR / CQP)."));

                /// @brief int — GOP length in frames (distance between
                /// keyframes).  0 = codec default.  Negative values are
                /// rejected.  Codec default: 60.
                PROMEKI_DECLARE_ID(GopLength, VariantSpec()
                                                      .setType(DataTypeInt32)
                                                      .setDefault(int32_t(60))
                                                      .setMin(int32_t(0))
                                                      .setDescription("GOP length in frames (0 = codec default)."));

                /// @brief int — maximum frames between IDR keyframes.  For
                /// many codecs this is the same as @ref GopLength, but a
                /// closed-GOP encoder may allow open GOPs where I-frames
                /// are more frequent than IDRs.  0 = same as @c GopLength.
                PROMEKI_DECLARE_ID(IdrInterval, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(0))
                                                        .setMin(int32_t(0))
                                                        .setDescription("Maximum frames between IDR keyframes "
                                                                        "(0 = same as GopLength)."));

                /// @brief int — number of B-frames between reference frames.
                /// 0 = disable B-frames (lowest latency).  Codec default: 0.
                PROMEKI_DECLARE_ID(BFrames, VariantSpec()
                                                    .setType(DataTypeInt32)
                                                    .setDefault(int32_t(0))
                                                    .setMin(int32_t(0))
                                                    .setDescription("Number of B-frames between references "
                                                                    "(0 = no B-frames)."));

                /// @brief int — number of look-ahead frames for rate
                /// control.  0 = disable look-ahead (lowest latency).
                /// Codec default: 0.
                PROMEKI_DECLARE_ID(LookaheadFrames, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(0))
                                                            .setMin(int32_t(0))
                                                            .setDescription("Rate-control look-ahead depth in frames "
                                                                            "(0 = disabled)."));

                /// @brief Enum @ref VideoEncoderPreset — speed/quality preset.
                /// Each concrete backend maps this onto its own native
                /// preset.  Codec default: Balanced.
                PROMEKI_DECLARE_ID(VideoPreset, VariantSpec()
                                                        .setType(DataTypeEnum)
                                                        .setDefault(promeki::VideoEncoderPreset::Balanced)
                                                        .setEnumType(promeki::VideoEncoderPreset::Type)
                                                        .setDescription("Video encoder speed/quality preset."));

                /// @brief String — codec-specific profile name
                /// (e.g. @c "baseline", @c "main", @c "high" for H.264;
                /// @c "main", @c "main10" for HEVC).  Empty string = codec
                /// default.  Profiles are string-typed because the valid
                /// set is codec-dependent.
                PROMEKI_DECLARE_ID(VideoProfile, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("Codec-specific profile name "
                                                                         "(empty = codec default)."));

                /// @brief String — codec-specific level name
                /// (e.g. @c "4.0", @c "4.1", @c "5.1").  Empty string =
                /// codec default (auto-selected from resolution /
                /// bitrate).
                PROMEKI_DECLARE_ID(VideoLevel, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("Codec-specific level name "
                                                                       "(empty = codec default / auto)."));

                /// @brief int — constant quantization parameter used when
                /// @ref VideoRcMode is @c CQP.  Lower values = higher
                /// quality and higher bitrate.  Typical range 18..40 for
                /// H.264 / HEVC.  Codec default: 23.
                PROMEKI_DECLARE_ID(VideoQp, VariantSpec()
                                                    .setType(DataTypeInt32)
                                                    .setDefault(int32_t(23))
                                                    .setRange(int32_t(0), int32_t(51))
                                                    .setDescription("Constant QP for CQP rate-control mode."));

                /// @brief bool — enable spatial adaptive quantization.
                PROMEKI_DECLARE_ID(VideoSpatialAQ, VariantSpec()
                                                           .setType(DataTypeBool)
                                                           .setDefault(false)
                                                           .setDescription("Enable spatial adaptive quantization."));

                /// @brief int — spatial AQ strength (1-15; 0 = auto).
                PROMEKI_DECLARE_ID(VideoSpatialAQStrength,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(15))
                                           .setDescription("Spatial AQ strength (1-15; 0 = auto)."));

                /// @brief bool — enable temporal adaptive quantization.
                PROMEKI_DECLARE_ID(VideoTemporalAQ, VariantSpec()
                                                            .setType(DataTypeBool)
                                                            .setDefault(false)
                                                            .setDescription("Enable temporal adaptive quantization."));

                /// @brief int — multi-pass encoding mode
                /// (0 = disabled, 1 = quarter-resolution, 2 = full-resolution).
                PROMEKI_DECLARE_ID(VideoMultiPass, VariantSpec()
                                                           .setType(DataTypeInt32)
                                                           .setDefault(int32_t(0))
                                                           .setRange(int32_t(0), int32_t(2))
                                                           .setDescription("Multi-pass mode "
                                                                           "(0=disabled, 1=quarter-res, 2=full-res)."));

                /// @brief bool — emit SPS/PPS (H.264), VPS/SPS/PPS (HEVC),
                /// or Sequence Header (AV1) with every IDR/key frame.
                ///
                /// Defaults to @c true because every streaming sink in
                /// the library — RTP, SRT, NDI, ST 2110 — expects
                /// late joiners to be able to start decoding at the
                /// next IDR without out-of-band parameter sets, and
                /// HLS / DASH segmenters require it too.  MP4 / MOV
                /// writers strip the duplicate parameter sets out of
                /// the @c mdat samples via
                /// @ref H264Bitstream::annexBToAvccFiltered while
                /// pulling them into the @c avcC / @c hvcC sample
                /// description, so the duplicate-on-IDR cost lands on
                /// the wire only.
                PROMEKI_DECLARE_ID(VideoRepeatHeaders, VariantSpec()
                                                               .setType(DataTypeBool)
                                                               .setDefault(true)
                                                               .setDescription("Emit parameter sets / sequence headers "
                                                                               "with every IDR."));

                /// @brief bool — emit SMPTE timecode via codec SEI.  Carried
                /// in Picture Timing SEI (H.264) / Time Code SEI (HEVC); the
                /// per-frame value comes from @ref Metadata::Timecode on the
                /// source Image.  Frames without a valid Timecode skip
                /// insertion for that picture.  Ignored for AV1 (NVENC does
                /// not expose a timecode OBU path).
                PROMEKI_DECLARE_ID(VideoTimecodeSEI,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Emit SMPTE timecode SEI "
                                                           "(H.264 picture timing / HEVC time code)."));

                /// @brief bool — populate expensive per-frame encoder
                /// statistics on every emitted @ref CompressedVideoPayload.
                ///
                /// Cheap stats (average QP, frame SATD, encode / display
                /// order index, temporal layer, GOP position) are always
                /// stamped and cost effectively nothing at lock time.
                /// This flag gates the more expensive family that requires
                /// the encoder to aggregate per-block information across
                /// the whole picture: @ref Metadata::CodecIntraBlockCount,
                /// @ref Metadata::CodecInterBlockCount,
                /// @ref Metadata::CodecAvgMotionVectorX, and
                /// @ref Metadata::CodecAvgMotionVectorY.  Default @c false
                /// because most pipelines do not consume the RC stats.
                PROMEKI_DECLARE_ID(VideoEncoderStats,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Populate expensive per-frame encoder "
                                                           "RC stats (intra/inter block counts, avg MV)."));

                /// @brief @ref ColorPrimaries — color primaries signalled
                /// in the VUI (H.264/HEVC) or color description (AV1).
                /// Numeric values per ISO/IEC 23091-4 / ITU-T H.273.
                ///
                /// Default @c Auto lets the encoder derive the value from
                /// the first input frame's PixelFormat / ColorModel (Rec.709
                /// → @c BT709, Rec.2020 → @c BT2020, sRGB → @c BT709, …).
                /// Set @c Unspecified to suppress the color-description
                /// block entirely, or pick a specific value to override.
                PROMEKI_DECLARE_ID(VideoColorPrimaries, VariantSpec()
                                                                .setType(DataTypeEnum)
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
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::TransferCharacteristics::Auto)
                                           .setEnumType(promeki::TransferCharacteristics::Type)
                                           .setDescription("VUI transfer characteristics "
                                                           "(Auto = derive from input)."));

                /// @brief @ref MatrixCoefficients — Y'CbCr derivation matrix
                /// signalled in the VUI.  Numeric values per ISO/IEC 23091-4
                /// / ITU-T H.273.
                ///
                /// Default @c Auto derives from the input PixelFormat's
                /// ColorModel (RGB models → @c RGB, YCbCr_Rec709 →
                /// @c BT709, YCbCr_Rec2020 → @c BT2020_NCL, …).
                PROMEKI_DECLARE_ID(VideoMatrixCoefficients, VariantSpec()
                                                                    .setType(DataTypeEnum)
                                                                    .setDefault(promeki::MatrixCoefficients::Auto)
                                                                    .setEnumType(promeki::MatrixCoefficients::Type)
                                                                    .setDescription("VUI matrix coefficients "
                                                                                    "(Auto = derive from input)."));

                /// @brief @ref VideoRange — studio/limited vs. full-range
                /// flag signalled in the VUI @c videoFullRangeFlag (H.264,
                /// HEVC) or AV1 @c colorRange field.
                ///
                /// Default @c Unknown means "derive from the first input
                /// frame's @ref PixelFormat::videoRange".  Callers can force
                /// @c Limited or @c Full to override the PixelFormat-derived
                /// signalling (rarely useful, but covers formats whose
                /// on-wire representation disagrees with their source
                /// convention).
                PROMEKI_DECLARE_ID(VideoRange, VariantSpec()
                                                       .setType(DataTypeEnum)
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
                PROMEKI_DECLARE_ID(VideoChromaSubsampling, VariantSpec()
                                                                   .setType(DataTypeEnum)
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
                PROMEKI_DECLARE_ID(VideoScanMode, VariantSpec()
                                                          .setType(DataTypeEnum)
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
                                   VariantSpec()
                                           .setType(DataTypeMasteringDisplay)
                                           .setDescription("Stream-level mastering display metadata "
                                                           "(SMPTE ST 2086)."));

                /// @brief @ref ContentLightLevel — stream-level content
                /// light level information (CTA-861.3).  When set, the
                /// encoder embeds this in every IDR (HEVC/AV1 SEI/OBU).
                PROMEKI_DECLARE_ID(HdrContentLightLevel, VariantSpec()
                                                                 .setType(DataTypeContentLightLevel)
                                                                 .setDescription("Stream-level content light level "
                                                                                 "(MaxCLL / MaxFALL)."));

                /// @brief bool — emit closed-caption SEI (ATSC A/53
                /// @c user_data_registered_itu_t_t35) on every encoded
                /// H.264 / HEVC picture that the source @ref Frame
                /// carries CEA-708 ANC for.
                ///
                /// When enabled, the encoder walks the source Frame's
                /// @ref AncPayload list via
                /// @ref VideoEncoder::selectAncForSei, translates each
                /// CEA-708 packet through @ref AncTranslator to the
                /// @c HlsSei wire transport, and injects the resulting
                /// SEI payload bytes alongside any HDR / timecode SEI
                /// the encoder is already producing.  This is the
                /// practical caption delivery path for YouTube Live,
                /// Twitch, and any modern CDN that ingests H.264 over
                /// RTMP / HLS / SRT / DASH.
                ///
                /// Default @c true — the feature is silent passthrough
                /// when no matching ANC is on the source Frame, so
                /// pipelines that never carry captions pay nothing.
                /// Set to @c false to suppress caption SEI even when
                /// the source has CEA-708 ANC available (rarely useful
                /// — typical reason is feeding a downstream that
                /// already injects its own caption track).
                ///
                /// AV1 has no direct SEI mechanism (captions ride as
                /// metadata OBUs instead) so this key is currently
                /// honoured only by H.264 and HEVC encodes; AV1
                /// requests warn-once and emit no caption metadata.
                PROMEKI_DECLARE_ID(VideoSeiCaptionsEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Emit ATSC A/53 closed-caption SEI carrying CEA-708 "
                                                           "from the source Frame's ANC payloads."));

                /// @brief @ref VideoCodec — typed codec identity used by
                /// the generic video encoder / decoder @ref MediaIO subclasses
                /// backends to look up the concrete @ref VideoEncoder /
                /// @ref VideoDecoder factory.  Authored on the CLI as
                /// the codec's registered name (e.g. @c "H264",
                /// @c "HEVC", @c "JPEG") and resolved through
                /// @ref promeki::VideoCodec::lookup.  Distinct from
                /// @ref Type, which selects the @ref MediaIO backend
                /// itself (e.g. @c "VideoEncoder" vs @c "CSC").
                PROMEKI_DECLARE_ID(VideoCodec, VariantSpec()
                                                       .setType(DataTypeVideoCodec)
                                                       .setDefault(promeki::VideoCodec())
                                                       .setDescription("Video codec for the VideoEncoder / "
                                                                       "VideoDecoder backends "
                                                                       "(e.g. \"H264\", \"HEVC\", \"JPEG\")."));

                /// @brief @ref AudioCodec — typed codec identity used by
                /// audio encoder / decoder backends to look up the
                /// concrete encoder / decoder factory (currently
                /// metadata-only — registered audio backends will land
                /// alongside this key).
                PROMEKI_DECLARE_ID(AudioCodec, VariantSpec()
                                                       .setType(DataTypeAudioCodec)
                                                       .setDefault(promeki::AudioCodec())
                                                       .setDescription("Audio codec for the audio encoder / "
                                                                       "decoder backends "
                                                                       "(e.g. \"AAC\", \"Opus\", \"FLAC\")."));

                /// @brief String — backend name override for codec
                /// resolution.  When non-empty, AudioCodec /
                /// VideoCodec @c createEncoder / @c createDecoder pin
                /// the backend registered under that name via
                /// @c AudioCodec::registerBackend /
                /// @c VideoCodec::registerBackend (e.g. @c "Native",
                /// @c "FFmpeg", @c "NVENC") instead of falling back to
                /// the highest-weight registered backend.  Equivalent
                /// to writing the codec as @c "Codec:Backend" in the
                /// string form of @c AudioCodec / @c VideoCodec.
                /// Empty (the default) means "let the registry pick".
                PROMEKI_DECLARE_ID(CodecBackend, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("Optional codec backend name pinning a "
                                                                         "specific codec backend (empty = let the "
                                                                         "registry pick)."));

                /// @brief Bool — permits the pipeline planner to pick a
                /// different backend than the user's pinned selection
                /// when the pinned backend can't satisfy the source.
                ///
                /// When @c false (the default), the planner must honor
                /// whichever backend the caller pinned on
                /// @c AudioCodec / @c VideoCodec (or via @c CodecBackend),
                /// and reports an error if that backend can't handle
                /// the incoming frames / packets.  When @c true, the
                /// planner may silently substitute a better-matched
                /// registered backend — useful when a @c MediaConfig is
                /// reused across pipelines where one of them happens
                /// to lack the originally-preferred backend.
                PROMEKI_DECLARE_ID(AllowCodecBackendOverride,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Allow the planner to pick a different "
                                                           "backend than the one pinned by the "
                                                           "caller (default: false)."));

                /// @brief Enum @ref OpusApplication — Opus encoder
                /// application mode (@c Voip / @c Audio / @c LowDelay).
                /// Backend default: @c Audio.
                PROMEKI_DECLARE_ID(OpusApplication, VariantSpec()
                                                            .setType(DataTypeEnum)
                                                            .setDefault(promeki::OpusApplication::Audio)
                                                            .setEnumType(promeki::OpusApplication::Type)
                                                            .setDescription("Opus encoder application mode "
                                                                            "(Voip / Audio / LowDelay)."));

                /// @brief float — Opus encoder frame size in
                /// milliseconds.  Valid values per libopus: 2.5, 5,
                /// 10, 20, 40, 60.  Backend default: 20 (good
                /// trade-off between latency and coding efficiency).
                PROMEKI_DECLARE_ID(OpusFrameSizeMs, VariantSpec()
                                                            .setType(DataTypeFloat)
                                                            .setDefault(20.0f)
                                                            .setMin(2.5f)
                                                            .setMax(60.0f)
                                                            .setDescription("Opus encoder frame size in milliseconds "
                                                                            "(2.5, 5, 10, 20, 40, or 60)."));

                // ============================================================
                // CSC pipeline
                // ============================================================

                /// @brief Enum @ref CscPath — CSC processing path selection
                /// (@c Optimized default, @c Scalar for debug / reference).
                PROMEKI_DECLARE_ID(CscPath, VariantSpec()
                                                    .setType(DataTypeEnum)
                                                    .setDefault(promeki::CscPath::Optimized)
                                                    .setEnumType(promeki::CscPath::Type)
                                                    .setDescription("CSC processing path (Optimized or Scalar)."));

                /// @brief Enum @ref CscToneMapping — when to apply HDR
                /// tone-mapping (@c Auto enables it automatically on
                /// HDR → SDR boundaries, @c Enabled forces it on,
                /// @c Disabled bypasses entirely).
                PROMEKI_DECLARE_ID(CscToneMapping,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::CscToneMapping::Auto)
                                           .setEnumType(promeki::CscToneMapping::Type)
                                           .setDescription("HDR tone-mapping policy (Auto / Enabled / Disabled)."));

                /// @brief Enum @ref CscToneMapOperator — which HDR tone-map
                /// operator to apply when tone-mapping is active
                /// (@c Bt2390 default; other slots reserved for future
                /// kernel work and fall back to @c Bt2390 with a warning).
                PROMEKI_DECLARE_ID(CscToneMapOperator,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::CscToneMapOperator::Bt2390)
                                           .setEnumType(promeki::CscToneMapOperator::Type)
                                           .setDescription("HDR tone-mapping operator "
                                                           "(Bt2390 / Reinhard / Hable / Aces / Bt2446a)."));

                /// @brief float — source HDR peak luminance in cd/m² (nits)
                /// for tone-mapping.  Defaults to 1000 nits, matching the
                /// HDR10 master display reference; common alternatives are
                /// 4000 nits (cinema HDR) or whatever the source's
                /// @ref MasteringDisplay metadata reports.  Consumed by
                /// operators that need a source peak (BT.2390, BT.2446).
                PROMEKI_DECLARE_ID(CscHdrPeakNits,
                                   VariantSpec()
                                           .setType(DataTypeFloat)
                                           .setDefault(float(1000.0f))
                                           .setMin(float(0.0f))
                                           .setMax(float(10000.0f))
                                           .setDescription("HDR tone-mapping source peak luminance in cd/m² (nits)."));

                /// @brief float — target SDR peak luminance in cd/m² (nits)
                /// for tone-mapping.  Defaults to 100 nits (BT.1886 / sRGB
                /// reference display).  Common alternatives are 200-300
                /// nits for HDR-capable but limited-peak displays.
                PROMEKI_DECLARE_ID(CscSdrPeakNits,
                                   VariantSpec()
                                           .setType(DataTypeFloat)
                                           .setDefault(float(100.0f))
                                           .setMin(float(0.0f))
                                           .setMax(float(10000.0f))
                                           .setDescription("HDR tone-mapping target peak luminance in cd/m² (nits)."));

                // ============================================================
                // Image file sequence (ImageFileMediaIO)
                // ============================================================

                /// @brief int — explicit @ref ImageFile::ID, bypasses extension probe.
                PROMEKI_DECLARE_ID(ImageFileID,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Explicit ImageFile ID (0 = infer from extension)."));

                /// @brief int — first frame index for a sequence writer.
                PROMEKI_DECLARE_ID(SequenceHead, VariantSpec()
                                                         .setType(DataTypeInt32)
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
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Enable automatic .imgseq sidecar for image sequences."));

                /// @brief String — override path for the @c .imgseq sidecar.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.imgseq" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                PROMEKI_DECLARE_ID(SaveImgSeqPath, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("Override path for the .imgseq sidecar."));

                /// @brief Enum @ref ImgSeqPathMode — whether the sidecar's
                /// directory reference is relative (to the sidecar) or absolute.
                PROMEKI_DECLARE_ID(SaveImgSeqPathMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(ImgSeqPathMode::Relative)
                                           .setEnumType(ImgSeqPathMode::Type)
                                           .setDescription("Sidecar directory reference mode (Relative or Absolute)."));

                /// @brief bool — enable sidecar audio file alongside an image
                /// sequence.  When true (the default), the ImageFile backend
                /// writes a Broadcast WAV sidecar when audio data is present
                /// and auto-detects an existing sidecar on read.  Set to
                /// @c false to inhibit both behaviours.
                PROMEKI_DECLARE_ID(SidecarAudioEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Enable sidecar audio file for image sequences."));

                /// @brief String — override path for the sidecar audio file.
                /// When empty (default), the filename is derived from the
                /// sequence pattern (e.g. @c "shot_####.dpx" produces
                /// @c "shot.wav" in the sequence directory).  Relative paths
                /// are resolved from the sequence directory; absolute paths
                /// are used as-is.
                PROMEKI_DECLARE_ID(SidecarAudioPath,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Override path for the sidecar audio file."));

                /// @brief Enum @ref AudioSourceHint — preferred audio source
                /// when reading an image sequence.  Selects between the
                /// sidecar audio file and embedded per-frame audio (e.g. DPX
                /// user-data blocks).  Acts as a hint: if the preferred
                /// source is unavailable, the backend falls back to the
                /// other.  Default is @c Sidecar.
                PROMEKI_DECLARE_ID(AudioSource,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(AudioSourceHint::Sidecar)
                                           .setEnumType(AudioSourceHint::Type)
                                           .setDescription("Preferred audio source for image sequence readers."));

                // ============================================================
                // QuickTime / ISO-BMFF (QuickTimeMediaIO)
                // ============================================================

                /// @brief Enum QuickTimeLayout — writer on-disk layout.
                PROMEKI_DECLARE_ID(QuickTimeLayout, VariantSpec()
                                                            .setType(DataTypeEnum)
                                                            .setDefault(promeki::QuickTimeLayout::Fragmented)
                                                            .setEnumType(promeki::QuickTimeLayout::Type)
                                                            .setDescription("QuickTime writer on-disk layout."));

                /// @brief int — video frames per fragment (fragmented writer).
                PROMEKI_DECLARE_ID(QuickTimeFragmentFrames,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Video frames per fragment (fragmented writer)."));

                /// @brief bool — call @c fdatasync after each flush.
                PROMEKI_DECLARE_ID(QuickTimeFlushSync, VariantSpec()
                                                               .setType(DataTypeBool)
                                                               .setDefault(false)
                                                               .setDescription("Call fdatasync after each flush."));

                // ============================================================
                // MPEG-TS (MpegTsFileMediaIO and any future TS-shaped sink/source)
                // ============================================================

                /// @brief int — PID used for the program's video elementary stream.
                /// Default: 0x100 (@c MpegTs::DefaultVideoPid).
                PROMEKI_DECLARE_ID(MpegTsVideoPid, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(0x0100))
                                                          .setMin(int32_t(0x0020))
                                                          .setMax(int32_t(0x1FFE))
                                                          .setDescription("MPEG-TS video elementary-stream PID."));

                /// @brief int — PID used for the program's audio elementary stream.
                /// Default: 0x101 (@c MpegTs::DefaultAudioPid).
                PROMEKI_DECLARE_ID(MpegTsAudioPid, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(0x0101))
                                                          .setMin(int32_t(0x0020))
                                                          .setMax(int32_t(0x1FFE))
                                                          .setDescription("MPEG-TS audio elementary-stream PID."));

                /// @brief int — PID carrying the PMT.  Default 0x1000.
                PROMEKI_DECLARE_ID(MpegTsPmtPid, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(0x1000))
                                                        .setMin(int32_t(0x0020))
                                                        .setMax(int32_t(0x1FFE))
                                                        .setDescription("MPEG-TS PMT PID."));

                /// @brief int — @c program_number written into the PAT / PMT.
                /// Default 1.
                PROMEKI_DECLARE_ID(MpegTsProgramNumber, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(1))
                                                               .setMin(int32_t(1))
                                                               .setMax(int32_t(0xFFFF))
                                                               .setDescription("MPEG-TS program_number."));

                /// @brief int — minimum interval, in milliseconds, between
                /// PAT and PMT re-emissions.  Default 100 ms (ETSI TR 101 290).
                PROMEKI_DECLARE_ID(MpegTsPatPmtIntervalMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(100))
                                           .setMin(int32_t(0))
                                           .setDescription("MPEG-TS PAT / PMT emission interval in ms."));

                /// @brief int — minimum interval, in milliseconds, between
                /// PCR insertions on the PCR PID.  Default 20 ms — small
                /// enough that PCR fires on every video access unit at
                /// 30 / 29.97 fps frame rates, which keeps decoders'
                /// clock estimates from drifting against the audio PTS
                /// stream.
                PROMEKI_DECLARE_ID(MpegTsPcrIntervalMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(20))
                                           .setMin(int32_t(0))
                                           .setDescription("MPEG-TS PCR insertion interval in ms."));

                /// @brief int64_t — CBR mux rate in bits per second.
                /// @c 0 (default) disables NULL-packet padding.
                PROMEKI_DECLARE_ID(MpegTsMuxRateBps, VariantSpec()
                                                            .setType(DataTypeInt64)
                                                            .setDefault(int64_t(0))
                                                            .setMin(int64_t(0))
                                                            .setDescription("MPEG-TS CBR target in bits per second."));

                /// @brief Enum @ref MpegTsAacFraming — how AAC is framed in
                /// the PMT @c stream_type.  Default @c Adts.
                PROMEKI_DECLARE_ID(MpegTsAacFraming, VariantSpec()
                                                            .setType(DataTypeEnum)
                                                            .setDefault(promeki::MpegTsAacFraming::Adts)
                                                            .setEnumType(promeki::MpegTsAacFraming::Type)
                                                            .setDescription("MPEG-TS AAC framing (ADTS or LATM)."));

                /// @brief Enum @ref VideoCodec — preferred video codec for
                /// uncompressed inputs.  Default @c H264.  Drives the
                /// stream_type the muxer picks for the video PID and lets
                /// the planner know which encoder to splice in.
                PROMEKI_DECLARE_ID(MpegTsVideoCodec, VariantSpec()
                                                            .setType(DataTypeVideoCodec)
                                                            .setDefault(promeki::VideoCodec(promeki::VideoCodec::H264))
                                                            .setDescription("MPEG-TS preferred video codec."));

                /// @brief Enum @ref AudioCodec — preferred audio codec for
                /// uncompressed inputs.  Default @c AAC.
                PROMEKI_DECLARE_ID(MpegTsAudioCodec, VariantSpec()
                                                            .setType(DataTypeAudioCodec)
                                                            .setDefault(promeki::AudioCodec(promeki::AudioCodec::AAC))
                                                            .setDescription("MPEG-TS preferred audio codec."));

                // ============================================================
                // SRT (SrtMediaIO and any future SRT-shaped sink/source)
                // ============================================================

                /// @brief Enum @ref SrtMode — Caller / Listener / Rendezvous.
                /// Default @c Caller.
                PROMEKI_DECLARE_ID(SrtMode, VariantSpec()
                                                    .setType(DataTypeEnum)
                                                    .setDefault(promeki::SrtMode::Caller)
                                                    .setEnumType(promeki::SrtMode::Type)
                                                    .setDescription("SRT connection role."));

                /// @brief String — peer host (used by Caller and Rendezvous).
                /// Accepts IPv4, IPv6, or hostname.  Empty in Listener mode.
                PROMEKI_DECLARE_ID(SrtPeerHost, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("SRT peer host (caller/rendezvous)."));

                /// @brief int — peer port (Caller / Rendezvous).  Required when
                /// in Caller or Rendezvous mode.  Range 1..65535.
                PROMEKI_DECLARE_ID(SrtPeerPort, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(0))
                                                        .setMin(int32_t(0))
                                                        .setMax(int32_t(65535))
                                                        .setDescription("SRT peer port."));

                /// @brief String — local bind host.  Defaults to @c 0.0.0.0
                /// (any).  Listener uses this as the listen address;
                /// Caller / Rendezvous as the source address.
                PROMEKI_DECLARE_ID(SrtLocalHost, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("SRT local bind host."));

                /// @brief int — local bind port.  Required in Listener and
                /// Rendezvous modes; optional (0 = ephemeral) in Caller.
                PROMEKI_DECLARE_ID(SrtLocalPort, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(0))
                                                         .setMin(int32_t(0))
                                                         .setMax(int32_t(65535))
                                                         .setDescription("SRT local bind port."));

                /// @brief int — symmetric latency in milliseconds.  Default 120.
                PROMEKI_DECLARE_ID(SrtLatencyMs, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(120))
                                                         .setMin(int32_t(0))
                                                         .setMax(int32_t(60000))
                                                         .setDescription("SRT receive / peer latency in ms."));

                /// @brief String — AES passphrase.  Empty disables encryption.
                /// Length must be 10..79 bytes when non-empty.
                PROMEKI_DECLARE_ID(SrtPassphrase, VariantSpec()
                                                          .setType(DataTypeString)
                                                          .setDefault(String())
                                                          .setDescription("SRT AES passphrase (10..79 bytes)."));

                /// @brief int — AES key length.  0 = auto, 16 / 24 / 32.
                PROMEKI_DECLARE_ID(SrtEncryptionKeyLength, VariantSpec()
                                                                  .setType(DataTypeInt32)
                                                                  .setDefault(int32_t(0))
                                                                  .setDescription("SRT AES key length (0=auto, 16/24/32)."));

                /// @brief String — SRT stream identifier.  Up to 512 bytes.
                /// Sent by Caller during handshake; available to a
                /// listen-callback / accepted listener-side socket.
                PROMEKI_DECLARE_ID(SrtStreamId, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("SRT stream ID (SRTO_STREAMID)."));

                /// @brief int64 — @c SRTO_MAXBW ceiling in bytes per second.
                /// 0 = unlimited (default), -1 = relative-to-input.
                PROMEKI_DECLARE_ID(SrtMaxBandwidthBps, VariantSpec()
                                                              .setType(DataTypeInt64)
                                                              .setDefault(int64_t(0))
                                                              .setDescription("SRT max bandwidth in bytes/sec."));

                /// @brief int — live-mode payload size in bytes.  Default 1316
                /// (7 × 188 = ideal for MPEG-TS).  Range 32..1456.  0 leaves
                /// the libsrt default in place.
                PROMEKI_DECLARE_ID(SrtPayloadSize, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(1316))
                                                          .setMin(int32_t(0))
                                                          .setMax(int32_t(1456))
                                                          .setDescription("SRT live-mode payload size."));

                /// @brief int — accept-wait timeout for Listener mode in ms.
                /// 0 = wait forever (default).
                PROMEKI_DECLARE_ID(SrtAcceptTimeoutMs, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(0))
                                                              .setMin(int32_t(0))
                                                              .setDescription("SRT accept timeout in ms (Listener mode)."));

                /// @brief Enum @ref SrtVideoPacing — Internal / External / None.
                /// Default @c Internal (matches RTMP/RTP sinks).
                PROMEKI_DECLARE_ID(SrtVideoPacing, VariantSpec()
                                                          .setType(DataTypeEnum)
                                                          .setDefault(promeki::SrtVideoPacing::Internal)
                                                          .setEnumType(promeki::SrtVideoPacing::Type)
                                                          .setDescription("SRT sink video pacing mode."));

                /// @brief int — skip-frame threshold in ms.  0 = never drop.
                PROMEKI_DECLARE_ID(SrtPaceSkipThresholdMs, VariantSpec()
                                                                  .setType(DataTypeInt32)
                                                                  .setDefault(int32_t(0))
                                                                  .setMin(int32_t(0))
                                                                  .setDescription("Drop video frames when lagging past this many ms."));

                /// @brief int — re-anchor threshold in ms.  0 = never reanchor.
                PROMEKI_DECLARE_ID(SrtPaceReanchorThresholdMs, VariantSpec()
                                                                      .setType(DataTypeInt32)
                                                                      .setDefault(int32_t(0))
                                                                      .setMin(int32_t(0))
                                                                      .setDescription("Re-anchor pacing clock after lag past this many ms."));

                /// @brief Enum @ref VideoCodec — preferred video codec for
                /// uncompressed inputs.  Default @c H264.  Drives the
                /// stream_type the muxer picks for the video PID.
                PROMEKI_DECLARE_ID(SrtVideoCodec, VariantSpec()
                                                         .setType(DataTypeVideoCodec)
                                                         .setDefault(promeki::VideoCodec(promeki::VideoCodec::H264))
                                                         .setDescription("SRT sink preferred video codec."));

                /// @brief Enum @ref AudioCodec — preferred audio codec for
                /// uncompressed inputs.  Default @c AAC.
                PROMEKI_DECLARE_ID(SrtAudioCodec, VariantSpec()
                                                         .setType(DataTypeAudioCodec)
                                                         .setDefault(promeki::AudioCodec(promeki::AudioCodec::AAC))
                                                         .setDescription("SRT sink preferred audio codec."));

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
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String("audio"))
                                           .setDescription("Timing source: \"audio\" (default) or \"wall\"."));

                /// @brief Size2Du32 — initial SDL window size.
                PROMEKI_DECLARE_ID(SdlWindowSize, VariantSpec()
                                                          .setType(DataTypeSize2D)
                                                          .setDefault(Size2Du32())
                                                          .setDescription("Initial SDL window size."));

                /// @brief String — SDL window title bar text.
                PROMEKI_DECLARE_ID(SdlWindowTitle, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("SDL window title bar text."));

                // ============================================================
                // RTP sink (RtpMediaIO)
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
                PROMEKI_DECLARE_ID(RtpLocalAddress, VariantSpec()
                                                            .setType(DataTypeSocketAddress)
                                                            .setDescription("Local bind address for all RTP streams."));

                /// @brief String — SDP @c s= line (session name).
                PROMEKI_DECLARE_ID(RtpSessionName, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("SDP session name (s= line)."));

                /// @brief String — SDP @c o= originator username.
                PROMEKI_DECLARE_ID(RtpSessionOrigin, VariantSpec()
                                                             .setType(DataTypeString)
                                                             .setDefault(String())
                                                             .setDescription("SDP originator username (o= line)."));

                /// @brief Enum @ref RtpPacingMode — pacing mechanism used for all streams.
                PROMEKI_DECLARE_ID(RtpPacingMode, VariantSpec()
                                                          .setType(DataTypeEnum)
                                                          .setDefault(promeki::RtpPacingMode::Auto)
                                                          .setEnumType(promeki::RtpPacingMode::Type)
                                                          .setDescription("RTP pacing mechanism."));

                /// @brief int — multicast TTL applied to the transport.
                PROMEKI_DECLARE_ID(RtpMulticastTTL, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(16))
                                                            .setRange(int32_t(1), int32_t(255))
                                                            .setDescription("Multicast TTL."));

                /// @brief String — multicast outgoing interface name (empty = default).
                PROMEKI_DECLARE_ID(RtpMulticastInterface,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Multicast outgoing interface name."));

                /// @brief String — if non-empty, the MediaIO opens this file and
                /// writes the generated SDP session description to it at open time.
                PROMEKI_DECLARE_ID(RtpSaveSdpPath, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("File path to write generated SDP to."));

                /// @brief bool — emit RFC 3550 RTCP Sender Reports on
                /// every active stream.  Required for cross-stream
                /// (audio/video) lip-sync at the receiver: each SR
                /// carries an @c (NTP, RTP-TS) pair the receiver uses
                /// to map both streams' RTP clocks onto a common wall
                /// clock.  Default @c true — RTCP is mandatory in
                /// RFC 3550 §6.1 for any RTP session.
                PROMEKI_DECLARE_ID(RtpRtcpEnabled, VariantSpec()
                                                           .setType(DataTypeBool)
                                                           .setDefault(true)
                                                           .setDescription("Emit RTCP Sender Reports."));

                /// @brief int — RTCP SR emission interval in
                /// milliseconds.  RFC 3550 §6.2 sets the minimum at
                /// 5 sec (computed dynamically from session
                /// bandwidth); for typical broadcast use the default
                /// is fine.  Lower values improve sync convergence
                /// at startup but add wire chatter.
                PROMEKI_DECLARE_ID(RtpRtcpIntervalMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(5000))
                                           .setMin(int32_t(100))
                                           .setDescription("RTCP Sender Report interval in ms."));

                /// @brief String — CNAME emitted in RTCP SDES.  An
                /// empty value (default) auto-generates a CNAME of
                /// the RFC 3550 §6.5.1 @c user@host form,
                /// @c "promeki-&lt;pid&gt;-&lt;objectId&gt;@&lt;egress-ip&gt;",
                /// where @c egress-ip is the source IP of the
                /// network interface used to reach the first
                /// configured destination (resolved through
                /// @ref NetworkInterface::findRoutesTo).  When no
                /// routable destination is available the host
                /// portion falls back to the first non-loopback
                /// interface IP and finally to @ref System::hostname.
                /// All streams on a single @ref RtpMediaIO share the
                /// derived CNAME so receivers can correlate an A/V
                /// pair from the same sender even when the SSRCs
                /// are unrelated; @c objectId distinguishes
                /// concurrent @ref RtpMediaIO objects within one
                /// process and @c pid distinguishes processes on
                /// the same host.
                PROMEKI_DECLARE_ID(RtpRtcpCname, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("RTCP SDES CNAME (empty = auto)."));

                /// @brief Polymorphic reader-side SDP input.  Accepts either:
                /// - @c String: interpreted as a filesystem path.
                /// - @ref SdpSession - consumed directly, no filesystem access.
                PROMEKI_DECLARE_ID(
                        RtpSdp,
                        VariantSpec()
                                .setTypes({DataTypeString, DataTypeSdpSession})
                                .setDescription("SDP input: file path (String) or session object (SdpSession)."));

                /// @brief Enum @ref RtpRefClockMode — SDP @c ts-refclk source per RFC 7273 / SMPTE ST 2110-10.
                PROMEKI_DECLARE_ID(RtpRefClock,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(RtpRefClockMode::Auto)
                                           .setEnumType(RtpRefClockMode::Type)
                                           .setDescription("SDP ts-refclk source mode."));

                /// @brief MacAddress — explicit MAC for @c ts-refclk:localmac (overrides autodetect).
                ///
                /// Used in @c "auto" / @c "localmac" modes to override
                /// the @ref NetworkInterface::firstNonLoopback default.
                /// A null MAC (the default) leaves autodetection in
                /// charge.
                PROMEKI_DECLARE_ID(RtpRefClockLocalMac,
                                   VariantSpec()
                                           .setType(DataTypeMacAddress)
                                           .setDefault(MacAddress())
                                           .setDescription("Override MAC for SDP ts-refclk:localmac."));

                /// @brief String — PTP profile identifier for SDP @c ts-refclk.
                ///
                /// Per RFC 7273: @c "IEEE1588-2008" or @c "IEEE1588-2019".
                /// SMPTE ST 2110-10 §6.3 currently mandates @c IEEE1588-2008.
                PROMEKI_DECLARE_ID(RtpPtpProfile,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String("IEEE1588-2008"))
                                           .setDescription("PTP profile for SDP ts-refclk:ptp."));

                /// @brief EUI64 — PTP grandmaster identifier.
                ///
                /// A null EUI-64 (the default) leaves the grandmaster
                /// field out of the @c ts-refclk:ptp value, which
                /// receivers tolerate per RFC 7273 §4.5.
                PROMEKI_DECLARE_ID(RtpPtpGrandmaster,
                                   VariantSpec()
                                           .setType(DataTypeEUI64)
                                           .setDefault(EUI64())
                                           .setDescription("PTP grandmaster EUI-64 for SDP ts-refclk:ptp."));

                /// @brief int — PTP domain number (0-255).
                ///
                /// Default 0 (the default IEEE 1588 domain).  SMPTE
                /// ST 2110-10 deployments commonly use domain 127.
                PROMEKI_DECLARE_ID(RtpPtpDomain,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(255))
                                           .setDescription("PTP domain number for SDP ts-refclk:ptp."));

                /// @brief int — SDP @c mediaclk:direct offset value.
                ///
                /// Default 0 (the offset SMPTE ST 2110 mandates and
                /// the only value the writer's RTP-TS counter aligns
                /// with today).  Override only when interoperating with
                /// a receiver that expects a non-zero direct offset.
                PROMEKI_DECLARE_ID(RtpMediaClkOffset,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("SDP mediaclk:direct=<offset> value."));

                /// @brief int — reader-side jitter buffer depth in milliseconds.
                PROMEKI_DECLARE_ID(RtpJitterMs, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(50))
                                                        .setMin(int32_t(0))
                                                        .setDescription("Reader jitter buffer depth in ms."));

                /// @brief int — reader-side output frame queue capacity.
                ///        Bounded with block-on-full so a stuck strand
                ///        backs the aggregator off without silently
                ///        dropping Frames; load-shedding instead surfaces
                ///        upstream as @c reorderOutputDropped on the
                ///        per-stream reorder buffer.
                PROMEKI_DECLARE_ID(RtpMaxReadQueueDepth,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(8))
                                           .setMin(int32_t(1))
                                           .setDescription("Reader output frame queue capacity."));

                /// @brief int — wire-silence timeout in milliseconds
                /// after which the reader signals EoS.  Default is
                /// 0 = derive as 10 × @ref RtpRtcpIntervalMs.  A
                /// positive value overrides the default.  Set to a
                /// large value (e.g. INT32_MAX) to effectively
                /// disable the watchdog when the source is bursty
                /// or expected to pause for long stretches.
                PROMEKI_DECLARE_ID(RtpWireSilenceTimeoutMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Reader wire-silence EoS timeout in ms (0 = 10 × RTCP)."));

                /// @brief bool — enable the video-stalled watchdog
                ///        which emits audio-only-continuation Frames
                ///        at the SDP-advertised video rate when the
                ///        sender pauses for a few frame durations.
                ///
                /// Off by default because the continuation Frames
                /// carry an audio payload but no video — downstream
                /// stages that hard-require a CompressedVideoPayload
                /// (e.g. @ref VideoDecoderMediaIO) will reject them.
                /// Enable when the consumer tolerates intermittent
                /// video-less Frames (audio-priority playout, ST
                /// 2110-30/31 monitoring) so audio playback continues
                /// smoothly across encoder pauses or brief network
                /// stalls.
                PROMEKI_DECLARE_ID(RtpVideoWatchdogEnabled,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Enable video-stall watchdog (audio-only continuation Frames)."));

                /// @brief int — desired kernel @c SO_RCVBUF size in bytes.
                PROMEKI_DECLARE_ID(
                        RtpRecvBufferBytes,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(8 * 1024 * 1024))
                                .setMin(int32_t(0))
                                .setDescription("Kernel SO_RCVBUF size for each RTP UDP socket "
                                                "(0 = leave kernel default; Linux clamps to net.core.rmem_max)."));

                /// @brief int — desired kernel @c SO_SNDBUF size in bytes.
                PROMEKI_DECLARE_ID(
                        RtpSendBufferBytes,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(8 * 1024 * 1024))
                                .setMin(int32_t(0))
                                .setDescription("Kernel SO_SNDBUF size for each RTP UDP socket "
                                                "(0 = leave kernel default; Linux clamps to net.core.wmem_max)."));

                /// @brief bool — assert IP "Don't Fragment" on egress sockets.
                ///
                /// SMPTE ST 2110-10 §6.3 requires senders never produce
                /// fragmented IP datagrams.  Default @c true for all RTP
                /// transports; disable only for legacy interop where
                /// downstream MTU is known to be smaller than the
                /// packetizer's output and on-path PMTU discovery is
                /// unwanted.
                PROMEKI_DECLARE_ID(RtpDontFragment,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Set IP DF (don't fragment) on RTP egress sockets."));

                /// @brief String — sender source IP for SDP @c source-filter
                ///        (RFC 4570).
                ///
                /// Per ST 2110-10, every multicast media section should
                /// carry @c "a=source-filter: incl IN IP4 <group> <source>"
                /// so SSM-enabled receivers join only this sender's
                /// distribution tree.  An empty value leaves the
                /// attribute off — defer to receivers that already know
                /// the source via control plane.  When set, the value
                /// must be a dotted-quad IPv4 address; the SDP layer
                /// pairs it with the per-section destination group.
                PROMEKI_DECLARE_ID(RtpSourceAddress,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Source IP for SDP source-filter (RFC 4570)."));

                // --- Video stream ---

                /// @brief SocketAddress — destination for the video stream. Empty = disabled.
                PROMEKI_DECLARE_ID(VideoRtpDestination,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("Destination for the video RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(VideoRtpPayloadType, VariantSpec()
                                                                .setType(DataTypeInt32)
                                                                .setDefault(int32_t(96))
                                                                .setRange(int32_t(0), int32_t(127))
                                                                .setDescription("Video RTP payload type."));

                /// @brief int — RTP timestamp clock rate in Hz (default 90000).
                PROMEKI_DECLARE_ID(VideoRtpClockRate, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(90000))
                                                              .setMin(int32_t(1))
                                                              .setDescription("Video RTP timestamp clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(VideoRtpSsrc, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(0))
                                                         .setMin(int32_t(0))
                                                         .setDescription("Video RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the video stream (default 34 / AF41).
                ///
                /// Per EBU TECH 3371 + AMWA BCP-006-01 + SMPTE ST 2022-6
                /// deployment notes: video RTP rides at AF41 (DSCP 34,
                /// TOS 0x88).  Reserved for emergency / control traffic
                /// at EF (DSCP 46); ST 2110 PTP rides EF on its own
                /// socket and must not collide with media DSCP marking.
                PROMEKI_DECLARE_ID(VideoRtpDscp, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(34))
                                                         .setRange(int32_t(0), int32_t(63))
                                                         .setDescription("Video RTP DSCP marking (AF41 by default)."));

                /// @brief int — target bitrate in bits/sec (0 = compute from descriptor).
                PROMEKI_DECLARE_ID(VideoRtpTargetBitrate,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Video RTP target bitrate in bps (0 = auto)."));

                /// @brief String — raw @c a=fmtp value from the SDP for the video stream.
                PROMEKI_DECLARE_ID(VideoRtpFmtp, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("Raw SDP a=fmtp value for the video stream."));

                /// @brief String — RTP @c rtpmap encoding name for the video stream
                ///        (e.g. @c "JPEG", @c "jxsv", @c "H264", @c "H265", @c "raw").
                ///        Populated by the reader from the parsed SDP and used by
                ///        @c RtpMediaIO to instantiate the right payload class when
                ///        no in-band geometry is available yet.
                PROMEKI_DECLARE_ID(VideoRtpEncoding,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("RTP rtpmap encoding name for the video stream."));

                // --- Audio stream ---

                /// @brief SocketAddress — destination for the audio stream. Empty = disabled.
                PROMEKI_DECLARE_ID(AudioRtpDestination,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("Destination for the audio RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(AudioRtpPayloadType, VariantSpec()
                                                                .setType(DataTypeInt32)
                                                                .setDefault(int32_t(97))
                                                                .setRange(int32_t(0), int32_t(127))
                                                                .setDescription("Audio RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default matches @c AudioRate).
                PROMEKI_DECLARE_ID(AudioRtpClockRate,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio RTP clock rate in Hz (0 = match AudioRate)."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(AudioRtpSsrc, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(0))
                                                         .setMin(int32_t(0))
                                                         .setDescription("Audio RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the audio stream (default 26 / AF31).
                ///
                /// Per EBU TECH 3371: AES67 / ST 2110-30 audio rides at
                /// AF31 (DSCP 26, TOS 0x68), one priority class above
                /// metadata, one below video.  Matches the per-stream
                /// DSCP map declared in @c devplan/network/2110.md
                /// Appendix A.
                PROMEKI_DECLARE_ID(AudioRtpDscp, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(26))
                                                         .setRange(int32_t(0), int32_t(63))
                                                         .setDescription("Audio RTP DSCP marking (AF31 by default)."));

                /// @brief int — packet time in microseconds (AES67 default 1000).
                PROMEKI_DECLARE_ID(AudioRtpPacketTimeUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(1000))
                                           .setMin(int32_t(1))
                                           .setDescription("Audio RTP packet time in microseconds."));

                /// @brief Enum @ref AudioWireFormat — AES67 / ST 2110-30
                ///        on-wire encoding for the audio stream.
                ///
                /// Picks @c L16 or @c L24 on the wire.  Default is
                /// @c Auto, which resolves to @c L24 when the source
                /// @c AudioDesc carries 24-bit samples, otherwise
                /// @c L16.  Per AES67 §7.1 the 96 kHz path requires
                /// @c L24; the writer falls back to @c L24 on @c Auto
                /// when running at 96 kHz regardless of the source
                /// bit depth.
                PROMEKI_DECLARE_ID(RtpAudioWireFormat,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(AudioWireFormat::Auto)
                                           .setEnumType(AudioWireFormat::Type)
                                           .setDescription("AES67 / ST 2110-30 on-wire PCM format."));

                /// @brief int — audio TX preroll in milliseconds.  The
                /// per-stream audio TX worker waits for this much
                /// source content to accumulate in its FIFO before it
                /// begins emitting RTP packets.  Two effects:
                ///
                /// - **Startup A/V matching** — when the upstream
                ///   pipeline routes video through a high-latency
                ///   stage (an H.264 / HEVC encoder that adds frames
                ///   of look-ahead, or a deep frame queue) but
                ///   audio bypasses it, raw audio arrives at this
                ///   stage seconds before video does.  Receivers
                ///   running at low buffer depth (ffplay with
                ///   @c -fflags @c nobuffer / @c -flags @c low_delay,
                ///   for example) play whatever arrives and audio
                ///   leads visible video.  Setting preroll to roughly
                ///   match the upstream video latency delays our wire
                ///   audio to coincide with the matching video frame.
                /// - **Stall absorption** — heavy IDR encodes or
                ///   per-frame work spikes upstream can stall the
                ///   strand for tens of milliseconds.  During the
                ///   stall the audio TX worker still drains at audio
                ///   cadence; without preroll the FIFO depletes and
                ///   the worker skips ticks (audible dropouts).
                ///   Preroll provides headroom equal to the configured
                ///   value, so a stall ≤ preroll never causes wire
                ///   silence.
                ///
                /// Default 0 (no preroll — audio emits immediately on
                /// first source push).  Typical values: 100-500 ms
                /// depending on upstream pipeline latency.
                PROMEKI_DECLARE_ID(AudioRtpPrerollMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio TX preroll buffer in milliseconds."));

                // --- Data / metadata stream ---

                /// @brief bool — enable transmission of per-frame Metadata.
                PROMEKI_DECLARE_ID(DataEnabled, VariantSpec()
                                                        .setType(DataTypeBool)
                                                        .setDefault(false)
                                                        .setDescription("Enable per-frame metadata transmission."));

                /// @brief SocketAddress — destination for the metadata stream. Empty = disabled.
                PROMEKI_DECLARE_ID(DataRtpDestination,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("Destination for the metadata RTP stream."));

                /// @brief int — RTP payload type (0-127).
                PROMEKI_DECLARE_ID(DataRtpPayloadType, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(100))
                                                               .setRange(int32_t(0), int32_t(127))
                                                               .setDescription("Metadata RTP payload type."));

                /// @brief int — RTP clock rate in Hz (default 90000).
                PROMEKI_DECLARE_ID(DataRtpClockRate, VariantSpec()
                                                             .setType(DataTypeInt32)
                                                             .setDefault(int32_t(90000))
                                                             .setMin(int32_t(1))
                                                             .setDescription("Metadata RTP clock rate in Hz."));

                /// @brief int — fixed SSRC, or 0 to auto-generate.
                PROMEKI_DECLARE_ID(DataRtpSsrc, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(0))
                                                        .setMin(int32_t(0))
                                                        .setDescription("Metadata RTP SSRC (0 = auto)."));

                /// @brief int — DSCP marking for the metadata stream (default 18 / AF21).
                ///
                /// Per EBU TECH 3371: ST 2110-40 ANC / metadata rides at
                /// AF21 (DSCP 18, TOS 0x48), one priority class below
                /// audio.
                PROMEKI_DECLARE_ID(DataRtpDscp, VariantSpec()
                                                        .setType(DataTypeInt32)
                                                        .setDefault(int32_t(18))
                                                        .setRange(int32_t(0), int32_t(63))
                                                        .setDescription("Metadata RTP DSCP marking (AF21 by default)."));

                /// @brief Enum @ref MetadataRtpFormat — wire format for the metadata stream.
                PROMEKI_DECLARE_ID(DataRtpFormat, VariantSpec()
                                                          .setType(DataTypeEnum)
                                                          .setDefault(MetadataRtpFormat::JsonMetadata)
                                                          .setEnumType(MetadataRtpFormat::Type)
                                                          .setDescription("Wire format for the metadata RTP stream."));

                // --- ST 2110-10 per-stream SDP fmtp ---

                /// @brief int — @c MAXUDP fmtp value for the video
                ///        stream (Standard UDP datagram size limit per
                ///        ST 2110-10 §6.3).  Default 0 = @c 1460
                ///        (Standard Size Limit).  Set up to @c 8960 for
                ///        the Extended Size Limit (jumbo frames; check
                ///        the open question in @c devplan/network/2110.md).
                PROMEKI_DECLARE_ID(RtpVideoMaxUdp,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Video SDP MAXUDP fmtp (0 = standard 1460)."));

                /// @brief int — @c MAXUDP fmtp value for the audio
                ///        stream.  ST 2110-30 §6.2.1 always uses the
                ///        Standard Size Limit; default 0 = 1460.
                PROMEKI_DECLARE_ID(RtpAudioMaxUdp,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio SDP MAXUDP fmtp (0 = standard 1460)."));

                /// @brief int — @c MAXUDP fmtp value for the data
                ///        (ANC / metadata) stream.  Default 0 = 1460.
                PROMEKI_DECLARE_ID(RtpDataMaxUdp,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Data / ANC SDP MAXUDP fmtp (0 = standard 1460)."));

                /// @brief Enum @ref RtpTsMode — @c TSMODE fmtp on the video stream.
                PROMEKI_DECLARE_ID(RtpVideoTsMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(RtpTsMode::Samp)
                                           .setEnumType(RtpTsMode::Type)
                                           .setDescription("Video SDP TSMODE fmtp."));

                /// @brief Enum @ref RtpTsMode — @c TSMODE fmtp on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioTsMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(RtpTsMode::Samp)
                                           .setEnumType(RtpTsMode::Type)
                                           .setDescription("Audio SDP TSMODE fmtp."));

                /// @brief Enum @ref RtpTsMode — @c TSMODE fmtp on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataTsMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(RtpTsMode::Samp)
                                           .setEnumType(RtpTsMode::Type)
                                           .setDescription("Data / ANC SDP TSMODE fmtp."));

                /// @brief Enum @ref RtpMediaClkMode — RFC 7273
                ///        @c mediaclk attribute mode on the video stream.
                ///
                /// Default @c Auto: emit
                /// @c mediaclk:direct=&lt;offset&gt; whenever a
                /// reference clock is advertised, omit otherwise.
                /// Override to @c Sender for sources whose media
                /// clock is asynchronous to the reference (free-running
                /// encoders, network-fed transcoders).
                PROMEKI_DECLARE_ID(RtpVideoMediaClkMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpMediaClkMode::Auto)
                                           .setEnumType(promeki::RtpMediaClkMode::Type)
                                           .setDescription("Video SDP mediaclk mode (direct/sender)."));

                /// @brief Enum @ref RtpMediaClkMode — RFC 7273
                ///        @c mediaclk attribute mode on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioMediaClkMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpMediaClkMode::Auto)
                                           .setEnumType(promeki::RtpMediaClkMode::Type)
                                           .setDescription("Audio SDP mediaclk mode (direct/sender)."));

                /// @brief Enum @ref RtpMediaClkMode — RFC 7273
                ///        @c mediaclk attribute mode on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataMediaClkMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpMediaClkMode::Auto)
                                           .setEnumType(promeki::RtpMediaClkMode::Type)
                                           .setDescription("Data / ANC SDP mediaclk mode (direct/sender)."));

                /// @brief int — @c TSDELAY fmtp microseconds on the video stream
                ///        (sender's @c D_TX = T_TX − T_RTP).  Default 0
                ///        omits the attribute.
                PROMEKI_DECLARE_ID(RtpVideoTsDelayUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Video SDP TSDELAY fmtp in microseconds (0 = omit)."));

                /// @brief int — @c TSDELAY fmtp microseconds on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioTsDelayUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio SDP TSDELAY fmtp in microseconds (0 = omit)."));

                /// @brief int — @c TSDELAY fmtp microseconds on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataTsDelayUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Data / ANC SDP TSDELAY fmtp in microseconds (0 = omit)."));

                /// @brief Enum @ref RtpSenderType — ST 2110-21 @c TP
                ///        fmtp on the video stream.
                ///
                /// Default @c Auto: derive from the bound
                /// @ref RtpPacingMode at open time (Userspace /
                /// KernelFq → TypeW, TxTime → TypeNL, None →
                /// Unknown).  Override to one of @c TypeN / @c TypeNL
                /// / @c TypeW to pin a specific narrow-timing
                /// classification.  @c Unknown suppresses the @c TP
                /// fmtp emission entirely.
                PROMEKI_DECLARE_ID(RtpVideoSenderType,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpSenderType::Auto)
                                           .setEnumType(promeki::RtpSenderType::Type)
                                           .setDescription("Video SDP TP fmtp (2110TPN/NL/W)."));

                /// @brief Enum @ref RtpSenderType — ST 2110-21 @c TP fmtp on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioSenderType,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpSenderType::Auto)
                                           .setEnumType(promeki::RtpSenderType::Type)
                                           .setDescription("Audio SDP TP fmtp (2110TPN/NL/W)."));

                /// @brief Enum @ref RtpSenderType — ST 2110-21 @c TP fmtp on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataSenderType,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtpSenderType::Auto)
                                           .setEnumType(promeki::RtpSenderType::Type)
                                           .setDescription("Data / ANC SDP TP fmtp (2110TPN/NL/W)."));

                /// @brief int — ST 2110-21 @c TROFF fmtp microseconds
                ///        on the video stream (0 = omit / use
                ///        TRO_DEFAULT).
                ///
                /// Pins the sender's TR_OFFSET; @c 0 (default) means
                /// "let the library compute TRO_DEFAULT per §7.4 from
                /// the resolved sender type, frame interval, MAXUDP,
                /// and active-line ratio".  Non-zero values are
                /// emitted directly in the SDP @c TROFF= fmtp.
                PROMEKI_DECLARE_ID(RtpVideoTrOffsetUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Video SDP TROFF fmtp in µs (0 = TRO_DEFAULT)."));

                /// @brief int — ST 2110-21 @c TROFF fmtp microseconds on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioTrOffsetUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Audio SDP TROFF fmtp in µs (0 = TRO_DEFAULT)."));

                /// @brief int — ST 2110-21 @c TROFF fmtp microseconds on the data / ANC stream.
                ///
                /// ANC streams use this knob distinct from
                /// @ref RtpAncTrOffsetUs which drives the LLTM
                /// @c T_EPO computation; this one drives the SDP
                /// @c TROFF fmtp on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataTrOffsetUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Data / ANC SDP TROFF fmtp in µs (0 = TRO_DEFAULT)."));

                /// @brief int — ST 2110-21 @c CMAX fmtp on the video
                ///        stream (0 = compute from sender type).
                ///
                /// Informational — declares the sender's observed
                /// peak burst in packets.  Default 0 = "let the
                /// library compute CMAX_narrow or CMAX_wide from the
                /// resolved sender type, packets-per-frame, and
                /// frame interval per §7.1".  Non-zero values are
                /// emitted directly in the SDP @c CMAX= fmtp.
                PROMEKI_DECLARE_ID(RtpVideoCmax,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Video SDP CMAX fmtp (0 = compute from sender type)."));

                /// @brief int — ST 2110-21 @c CMAX fmtp on the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioCmax,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Audio SDP CMAX fmtp (0 = compute from sender type)."));

                /// @brief int — ST 2110-21 @c CMAX fmtp on the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataCmax,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Data / ANC SDP CMAX fmtp (0 = compute from sender type)."));

                /// @brief Enum @ref JxsPacketMode — RFC 9134 §4.3
                ///        @c packetmode (K bit) on the video stream.
                ///
                /// Default @c Codestream (K=0, MTU fragmentation).
                /// Set to @c Slice (K=1) to engage one-or-more-
                /// complete-slices-per-packet packetization — drives
                /// the @ref RtpPayloadJpegXs::pack slice path.
                PROMEKI_DECLARE_ID(RtpVideoJxsPacketMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::JxsPacketMode::Codestream)
                                           .setEnumType(promeki::JxsPacketMode::Type)
                                           .setDescription("RFC 9134 packetmode (K bit)."));

                /// @brief Enum @ref JxsTransMode — RFC 9134 §4.3
                ///        @c transmode (T bit) on the video stream.
                ///
                /// Default @c SequentialOnly (T=1) per the RFC's
                /// "absent ⇒ T=1" rule.  Override to
                /// @c OutOfOrderAllowed when the receiver can
                /// tolerate network reorder before decode.
                PROMEKI_DECLARE_ID(RtpVideoJxsTransMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::JxsTransMode::SequentialOnly)
                                           .setEnumType(promeki::JxsTransMode::Type)
                                           .setDescription("RFC 9134 transmode (T bit)."));

                /// @brief Enum @ref JxsProfile — RFC 9134 §7.1
                ///        @c profile fmtp value.  @c Unspecified
                ///        suppresses emission.
                PROMEKI_DECLARE_ID(RtpVideoJxsProfile,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::JxsProfile::Unspecified)
                                           .setEnumType(promeki::JxsProfile::Type)
                                           .setDescription("RFC 9134 JPEG XS profile."));

                /// @brief Enum @ref JxsLevel — RFC 9134 §7.1
                ///        @c level fmtp value.  @c Unspecified
                ///        suppresses emission.
                PROMEKI_DECLARE_ID(RtpVideoJxsLevel,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::JxsLevel::Unspecified)
                                           .setEnumType(promeki::JxsLevel::Type)
                                           .setDescription("RFC 9134 JPEG XS level."));

                /// @brief Enum @ref JxsSublevel — RFC 9134 §7.1
                ///        @c sublevel fmtp value.  @c Unspecified
                ///        suppresses emission.
                PROMEKI_DECLARE_ID(RtpVideoJxsSublevel,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::JxsSublevel::Unspecified)
                                           .setEnumType(promeki::JxsSublevel::Type)
                                           .setDescription("RFC 9134 JPEG XS sublevel."));

                /// @brief bool — emit @c ts-refclk:ptp=...:traceable
                ///        (RFC 7273 §4.7) in lieu of the explicit
                ///        GMID+domain form.
                ///
                /// When @c true and @ref RtpRefClock resolves to
                /// @c Ptp, the SDP writer emits the @c traceable form
                /// regardless of @ref RtpPtpGrandmaster.  ST 2110-10
                /// §8.2 narrows this to deployments where the slave
                /// reports a traceable grandmaster
                /// (@c clockAccuracy ≤ 250 ns,
                /// @c timeTraceable asserted).
                ///
                /// **Auto-resolution (Phase D2, 2026-05-21):** when
                /// @ref RtpPtpDevicePath is set and the opened
                /// @c PhcClock reports a sub-microsecond
                /// @c sysOffsetPrecise sample (its
                /// @c PhcClock::isLocked heuristic), the writer
                /// behaves as if this flag were @c true even when the
                /// caller left it @c false.  An explicit @c true is
                /// still honoured (the deployment may know it's
                /// running against a traceable grandmaster even if the
                /// host's @c CLOCK_REALTIME hasn't been disciplined to
                /// it yet); an explicit @c false suppresses the
                /// @c traceable emit even on a locked PHC.
                PROMEKI_DECLARE_ID(RtpPtpTraceable,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Emit ts-refclk:ptp=...:traceable instead of GMID+domain."));

                /// @brief String — path to a @c /dev/ptpN PHC device
                ///        to bind as the PTP wallclock source.
                ///
                /// Empty (the default) means "no PHC source"; the SR
                /// anchor falls back to @c NtpTime::now()
                /// (@c CLOCK_REALTIME), matching the pre-Phase-D2
                /// behaviour.  When set, @ref RtpMediaIO opens a
                /// @ref PhcClock at the path, binds it to
                /// @ref ClockDomain::Ptp as the wallclock provider,
                /// and routes the writer-side SR anchor through the
                /// @c setRtpAnchor(ClockDomain, …) overload so the
                /// emitted NTP timestamps reflect the PTP timescale
                /// rather than the host's @c CLOCK_REALTIME.
                ///
                /// Typical values: @c "/dev/ptp0" (when the NIC is
                /// already running @c ptp4l with default settings),
                /// or @c "/dev/ptpN" for a specific NIC index.
                ///
                /// Errors during PHC open are non-fatal — the writer
                /// emits a @c promekiWarn and falls back to the
                /// system-clock path.  Linux-only; ignored elsewhere.
                PROMEKI_DECLARE_ID(RtpPtpDevicePath,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription(
                                                   "Linux PHC device path "
                                                   "(/dev/ptpN) for PTP-traceable "
                                                   "SR anchors; empty = use "
                                                   "system_clock."));

                // --- ST 2110-20 video fmtp (§7.2 / §7.3) ---
                //
                // When any of these are set to a non-Invalid value, the
                // corresponding fmtp parameter is emitted explicitly;
                // otherwise the value is derived from the source
                // @ref PixelFormat via @ref St2110Video::bridgeForPixelFormat.
                // The PixelFormat-derived defaults already cover every
                // PixelFormat with a clean ST 2110-20 wire-format
                // counterpart, so these keys exist primarily for callers
                // that need to override the inferred values (e.g. force
                // a specific colorimetry for ALPHA / KEY signals where
                // the PixelFormat doesn't carry the right hint).

                /// @brief Enum @ref St2110Sampling — @c sampling fmtp (§7.4.1).
                PROMEKI_DECLARE_ID(RtpVideoSampling,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110Sampling::Invalid)
                                           .setEnumType(St2110Sampling::Type)
                                           .setDescription("Video SDP sampling fmtp (Invalid = derive from PixelFormat)."));

                /// @brief Enum @ref St2110Depth — @c depth fmtp (§7.4.2).
                PROMEKI_DECLARE_ID(RtpVideoDepth,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110Depth::Invalid)
                                           .setEnumType(St2110Depth::Type)
                                           .setDescription("Video SDP depth fmtp (Invalid = derive from PixelFormat)."));

                /// @brief Enum @ref St2110Colorimetry — @c colorimetry fmtp (§7.5).
                PROMEKI_DECLARE_ID(RtpVideoColorimetry,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110Colorimetry::Invalid)
                                           .setEnumType(St2110Colorimetry::Type)
                                           .setDescription("Video SDP colorimetry fmtp (Invalid = derive from PixelFormat)."));

                /// @brief Enum @ref St2110Tcs — @c TCS fmtp (§7.6).
                PROMEKI_DECLARE_ID(RtpVideoTcs,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110Tcs::Invalid)
                                           .setEnumType(St2110Tcs::Type)
                                           .setDescription("Video SDP TCS fmtp (Invalid = derive from PixelFormat)."));

                /// @brief Enum @ref St2110Range — @c RANGE fmtp (§7.3).
                PROMEKI_DECLARE_ID(RtpVideoRange,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110Range::Invalid)
                                           .setEnumType(St2110Range::Type)
                                           .setDescription("Video SDP RANGE fmtp (Invalid = derive from PixelFormat)."));

                /// @brief Enum @ref St2110PackingMode — @c PM fmtp (§6.3).
                PROMEKI_DECLARE_ID(RtpVideoPackingMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(St2110PackingMode::Gpm)
                                           .setEnumType(St2110PackingMode::Type)
                                           .setDescription("Video SDP PM fmtp (default 2110GPM)."));

                /// @brief PixelAspect — @c PAR fmtp (§7.3).
                ///
                /// Default 1:1 (square pixels) is omitted from the SDP
                /// fmtp per §7.3 ("If PAR is not signaled, the receiver
                /// shall assume that PAR = '1:1'").  Anamorphic sources
                /// override here with the explicit width:height ratio.
                PROMEKI_DECLARE_ID(RtpVideoPar,
                                   VariantSpec()
                                           .setType(DataTypePixelAspect)
                                           .setDefault(PixelAspect())
                                           .setDescription("Video SDP PAR fmtp (default 1:1 = omit)."));

                /// @brief SocketAddress — secondary destination for the
                ///        video stream (ST 2022-7 redundancy leg).
                ///        Empty = no redundancy.
                ///
                /// When set, the SDP writer emits
                /// @c "a=group:DUP <primary-mid> <secondary-mid>" at
                /// session level plus per-section @c a=mid:<token>
                /// (RFC 5888 + RFC 7104).  Actual dual-leg transport
                /// management lands in Phase E2022.
                PROMEKI_DECLARE_ID(RtpVideoDestinationSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary destination for video."));

                /// @brief SocketAddress — secondary destination for the audio stream.
                PROMEKI_DECLARE_ID(RtpAudioDestinationSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary destination for audio."));

                /// @brief SocketAddress — secondary destination for the data / ANC stream.
                PROMEKI_DECLARE_ID(RtpDataDestinationSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary destination for data / ANC."));

                /// @brief SocketAddress — per-leg local bind address for
                ///        the *secondary* video leg.  When non-null,
                ///        overrides @ref RtpLocalAddress on the secondary
                ///        transport so the secondary leg can be pinned to
                ///        a NIC distinct from the primary.  Setting just
                ///        the IP portion (port 0) forces egress through
                ///        the NIC that owns the IP — the canonical
                ///        ST 2022-7 deployment pattern with two NICs on
                ///        two disjoint subnets.  Empty / unset = the
                ///        secondary transport reuses @ref RtpLocalAddress.
                PROMEKI_DECLARE_ID(RtpVideoLocalAddressSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary local bind address for video."));

                /// @brief String — per-leg @c SO_BINDTODEVICE interface
                ///        name for the *secondary* video leg (e.g.
                ///        @c "eth1").  Use for the rare same-subnet two-
                ///        NIC case where a source-IP bind cannot
                ///        disambiguate egress.  Requires @c CAP_NET_RAW
                ///        on Linux.  Empty / unset = no @c SO_BINDTODEVICE.
                PROMEKI_DECLARE_ID(RtpVideoInterfaceSecondary,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("ST 2022-7 secondary SO_BINDTODEVICE interface for video."));

                /// @brief SocketAddress — per-leg local bind address for the *secondary* audio leg.
                /// @see RtpVideoLocalAddressSecondary for the semantics.
                PROMEKI_DECLARE_ID(RtpAudioLocalAddressSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary local bind address for audio."));

                /// @brief String — per-leg @c SO_BINDTODEVICE interface for the *secondary* audio leg.
                /// @see RtpVideoInterfaceSecondary for the semantics.
                PROMEKI_DECLARE_ID(RtpAudioInterfaceSecondary,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("ST 2022-7 secondary SO_BINDTODEVICE interface for audio."));

                /// @brief SocketAddress — per-leg local bind address for the *secondary* data leg.
                /// @see RtpVideoLocalAddressSecondary for the semantics.
                PROMEKI_DECLARE_ID(RtpDataLocalAddressSecondary,
                                   VariantSpec()
                                           .setType(DataTypeSocketAddress)
                                           .setDescription("ST 2022-7 secondary local bind address for data / ANC."));

                /// @brief String — per-leg @c SO_BINDTODEVICE interface for the *secondary* data leg.
                /// @see RtpVideoInterfaceSecondary for the semantics.
                PROMEKI_DECLARE_ID(RtpDataInterfaceSecondary,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("ST 2022-7 secondary SO_BINDTODEVICE interface for data / ANC."));

                /// @brief Enum @ref AncTransmissionModel — ST 2110-40
                ///        §6 transmission model fmtp.
                ///
                /// Default @c Unsignalled (no @c TM= in the SDP fmtp,
                /// pairs with @c SSN=ST2110-40:2018 — the 2018-edition
                /// default).  Set to @c Lltm to advertise Low-Latency
                /// Transmission Model (§6.4) — the writer additionally
                /// stamps a per-batch @c CLOCK_TAI deadline of
                /// @c T_FST + T_EPO + T_D on every ANC frame when a
                /// PTP-anchored @ref RtpMediaClock and
                /// @ref RtpPacingMode::TxTime are both available.
                /// Set to @c Ctm to advertise Compatible Transmission
                /// Model (§6.5) — no per-batch deadline; the kernel's
                /// existing 1 ms pacing is sufficient.  Setting
                /// @c Lltm or @c Ctm also bumps the SDP @c SSN to
                /// @c ST2110-40:2023.
                PROMEKI_DECLARE_ID(RtpAncTransmissionModel,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::AncTransmissionModel::Unsignalled)
                                           .setEnumType(promeki::AncTransmissionModel::Type)
                                           .setDescription("ST 2110-40 §6 transmission model (TM fmtp)."));

                /// @brief int — ST 2110-40 §6.4 @c T_EPO (Epoch Offset)
                ///        in microseconds.
                ///
                /// Added to @c T_FST before the LLTM @c T_D slack
                /// window in the per-batch deadline computation.  Same
                /// semantics as ST 2110-10 §7.4 TR_OFFSET but
                /// namespaced to ANC's per-stream adjustment.  Default
                /// 0 — natural-anchor LLTM.  Also emitted as the
                /// @c TROFFSETANC fmtp in the SDP when non-zero.
                PROMEKI_DECLARE_ID(RtpAncTrOffsetUs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("ST 2110-40 LLTM T_EPO offset in µs."));

                /// @brief int — ST 2110-40 §6.4 TotalLines override.
                ///
                /// Used in the LLTM @c T_D computation
                /// (<tt>T_D = 8 / (FrameRate × TotalLines)</tt>).
                /// Default 0 = Auto — the library resolves TotalLines
                /// from the paired video stream's @ref VideoFormat via
                /// @ref VideoFormatDetails.  Set non-zero to override
                /// the Auto resolution when the application knows the
                /// SDI total-line count better than the
                /// @ref VideoFormat lookup (e.g. for non-SMPTE
                /// rasters, or to pin a conservative value).
                PROMEKI_DECLARE_ID(RtpAncTotalLines,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("ST 2110-40 LLTM TotalLines (0 = Auto)."));

                /// @brief int — ST 2110-40 §5.2.3 VPID_Code fmtp.
                ///
                /// Optional SDI Video Payload Identifier code (per
                /// SMPTE 352M) that lets a receiver reconstruct the
                /// exact SDI line / stream layout for downstream
                /// SDI emission.  Default 0 = "do not emit
                /// @c VPID_Code in the SDP fmtp".  Stored on the
                /// per-stream context for SDP emission; the receive
                /// side does not consume it today (gated on the
                /// future SDI write-back path).
                PROMEKI_DECLARE_ID(RtpAncVpidCode,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("ST 2110-40 VPID_Code fmtp (0 = omit)."));

                // ============================================================
                // RTMP / RTMPS (RtmpMediaIO)
                //
                // Single-connection ordered TCP transport.  One @c
                // RtmpMediaIO instance corresponds to exactly one peer
                // and one logical stream, so all keys are top-level
                // (we do not mirror the per-stream @c VideoRtp* /
                // @c AudioRtp* / @c DataRtp* style).
                // ============================================================

                /// @brief Url — `rtmp://host:1935/app/streamKey` or `rtmps://...`.  Required.
                PROMEKI_DECLARE_ID(RtmpUrl, VariantSpec()
                                                    .setType(DataTypeUrl)
                                                    .setDescription("RTMP / RTMPS endpoint URL."));

                /// @brief String — overrides the URL's last path component (some destinations
                /// want the stream key in headers rather than the URL path).
                PROMEKI_DECLARE_ID(RtmpStreamKey, VariantSpec()
                                                          .setType(DataTypeString)
                                                          .setDefault(String())
                                                          .setDescription("Stream key override (last URL path segment)."));

                /// @brief String — overrides the URL's app component (URL path's leading segments).
                PROMEKI_DECLARE_ID(RtmpAppName, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("App-name override (URL path leading segments)."));

                /// @brief String — `connect.flashVer` advertised in the AMF0 connect body.
                PROMEKI_DECLARE_ID(RtmpFlashVer,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String("FMLE/3.0 (compatible; libpromeki)"))
                                           .setDescription("AMF0 connect.flashVer."));

                /// @brief String — `connect.tcUrl`.  Empty means reconstruct from @ref RtmpUrl.
                PROMEKI_DECLARE_ID(RtmpTcUrl, VariantSpec()
                                                      .setType(DataTypeString)
                                                      .setDefault(String())
                                                      .setDescription("AMF0 connect.tcUrl override."));

                /// @brief String — `connect.pageUrl`.
                PROMEKI_DECLARE_ID(RtmpPageUrl, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("AMF0 connect.pageUrl."));

                /// @brief String — `connect.swfUrl`.
                PROMEKI_DECLARE_ID(RtmpSwfUrl, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("AMF0 connect.swfUrl."));

                /// @brief bool — emit Enhanced-RTMP video tag form when codec is HEVC / VP9 / AV1.
                PROMEKI_DECLARE_ID(RtmpEnhancedRtmp, VariantSpec()
                                                             .setType(DataTypeBool)
                                                             .setDefault(true)
                                                             .setDescription("Use Enhanced-RTMP framing for "
                                                                             "HEVC / VP9 / AV1."));

                /// @brief int — local chunk size.  RTMP §5.4.1 mandates >= 128;
                /// most peers reject anything past 65535.  Default 60000 matches OBS / FFmpeg.
                PROMEKI_DECLARE_ID(RtmpChunkSize, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(60000))
                                                          .setRange(int32_t(128), int32_t(65535))
                                                          .setDescription("Local RTMP chunk size in bytes."));

                /// @brief int — our advertised WindowAckSize.
                PROMEKI_DECLARE_ID(RtmpWindowAckSize, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(5'000'000))
                                                              .setMin(int32_t(1024))
                                                              .setDescription("Advertised WindowAckSize in bytes."));

                /// @brief int — value emitted in the SetPeerBandwidth control message
                /// (limit-type Dynamic).
                PROMEKI_DECLARE_ID(RtmpPeerBandwidth, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(5'000'000))
                                                              .setMin(int32_t(1024))
                                                              .setDescription("SetPeerBandwidth value (Dynamic)."));

                /// @brief Enum @ref RtmpHandshakeMode — Auto / Simple / Complex.
                PROMEKI_DECLARE_ID(RtmpHandshakeMode, VariantSpec()
                                                              .setType(DataTypeEnum)
                                                              .setDefault(promeki::RtmpHandshakeMode::Auto)
                                                              .setEnumType(promeki::RtmpHandshakeMode::Type)
                                                              .setDescription("RTMP handshake mode."));

                /// @brief bool — emit `FCSubscribe` before `play` (some Wowza configs require it).
                PROMEKI_DECLARE_ID(RtmpFcSubscribe, VariantSpec()
                                                            .setType(DataTypeBool)
                                                            .setDefault(false)
                                                            .setDescription("Emit FCSubscribe before play."));

                /// @brief bool — re-inject SPS/PPS (or VPS/SPS/PPS) inline ahead of every IDR.
                /// Helps subscribers recover from packet loss.
                PROMEKI_DECLARE_ID(RtmpRepeatParameterSets,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Repeat parameter sets ahead of every IDR."));

                /// @brief bool — source mode: reframe AVCC NALs to Annex-B before emitting payloads.
                PROMEKI_DECLARE_ID(RtmpEmitAnnexB, VariantSpec()
                                                          .setType(DataTypeBool)
                                                          .setDefault(false)
                                                          .setDescription("Source-mode: emit Annex-B framing."));

                /// @brief bool — sink mode: drop video access units until the first IDR after publish.
                /// Default true — most destinations refuse a stream that begins on an inter-frame.
                PROMEKI_DECLARE_ID(RtmpDropUntilKeyframe,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Sink-mode: drop access units until first IDR."));

                /// @brief bool — set @c TCP_NODELAY on the publish / play socket.  Default on.
                PROMEKI_DECLARE_ID(RtmpStartTcpNoDelay, VariantSpec()
                                                                .setType(DataTypeBool)
                                                                .setDefault(true)
                                                                .setDescription("Set TCP_NODELAY on the socket."));

                /// @brief int — TCP connect + TLS handshake budget in ms.
                PROMEKI_DECLARE_ID(RtmpConnectTimeoutMs, VariantSpec()
                                                                 .setType(DataTypeInt32)
                                                                 .setDefault(int32_t(10000))
                                                                 .setMin(int32_t(0))
                                                                 .setDescription("Connect + TLS handshake budget."));

                /// @brief int — RTMP handshake budget in ms.
                PROMEKI_DECLARE_ID(RtmpHandshakeTimeoutMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(10000))
                                           .setMin(int32_t(0))
                                           .setDescription("RTMP handshake budget."));

                /// @brief int — `_result`/`onStatus` reply wait per AMF0 transaction (ms).
                PROMEKI_DECLARE_ID(RtmpCommandTimeoutMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(5000))
                                           .setMin(int32_t(0))
                                           .setDescription("AMF0 command-reply timeout."));

                /// @brief int — source-mode dead-peer detector.
                /// Declares the connection lost after this many ms with no inbound bytes.  0 disables.
                PROMEKI_DECLARE_ID(RtmpReadIdleTimeoutMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(30000))
                                           .setMin(int32_t(0))
                                           .setDescription("Source-mode dead-peer timeout (0 disables)."));

                /// @brief int — `SO_RCVBUF`.  0 means kernel default.
                PROMEKI_DECLARE_ID(RtmpRecvBufferBytes, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(0))
                                                               .setMin(int32_t(0))
                                                               .setDescription("SO_RCVBUF (0 = kernel default)."));

                /// @brief int — `SO_SNDBUF`.
                PROMEKI_DECLARE_ID(RtmpSendBufferBytes, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(1048576))
                                                               .setMin(int32_t(0))
                                                               .setDescription("SO_SNDBUF."));

#if PROMEKI_ENABLE_TLS
                /// @brief SslContext — RTMPS context override.  An
                /// unset / null-impl handle means build one with the
                /// system CA bundle.
                PROMEKI_DECLARE_ID(RtmpTlsContext, VariantSpec()
                                                          .setType(DataTypeSslContext)
                                                          .setDescription("RTMPS SslContext override."));
#endif

                /// @brief bool — peer-verify.  Set false only for self-signed test servers.
                PROMEKI_DECLARE_ID(RtmpTlsVerify, VariantSpec()
                                                          .setType(DataTypeBool)
                                                          .setDefault(true)
                                                          .setDescription("RTMPS peer-verification."));

                /// @brief VideoCodec — pin for the video stream.  Default H.264.
                PROMEKI_DECLARE_ID(RtmpVideoCodec,
                                   VariantSpec()
                                           .setType(DataTypeVideoCodec)
                                           .setDefault(promeki::VideoCodec(promeki::VideoCodec::H264))
                                           .setDescription("Pin RTMP's video codec."));

                /// @brief AudioCodec — pin for the audio stream.  Default AAC.
                PROMEKI_DECLARE_ID(RtmpAudioCodec,
                                   VariantSpec()
                                           .setType(DataTypeAudioCodec)
                                           .setDefault(promeki::AudioCodec(promeki::AudioCodec::AAC))
                                           .setDescription("Pin RTMP's audio codec."));

                /// @brief int — target video bitrate (bits per second).  0 = derive from FrameRate × resolution.
                PROMEKI_DECLARE_ID(RtmpVideoBitrate, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(0))
                                                            .setMin(int32_t(0))
                                                            .setDescription("Target video bitrate (bps)."));

                /// @brief int — target audio bitrate (bits per second).
                PROMEKI_DECLARE_ID(RtmpAudioBitrate, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(128000))
                                                            .setMin(int32_t(0))
                                                            .setDescription("Target audio bitrate (bps)."));

                /// @brief String — preferred video encoder backend (e.g. `"Nvidia"`).
                PROMEKI_DECLARE_ID(RtmpVideoEncoderBackend,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Preferred video encoder backend (e.g. Nvidia)."));

                /// @brief String — preferred audio encoder backend (e.g. `"FdkAac"`).
                PROMEKI_DECLARE_ID(RtmpAudioEncoderBackend,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Preferred audio encoder backend."));

                /// @brief int — keyframe / GOP target in seconds; forwarded to the encoder.
                PROMEKI_DECLARE_ID(RtmpKeyframeIntervalSec,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(2))
                                           .setMin(int32_t(1))
                                           .setDescription("GOP target in seconds."));

                /// @brief bool — emit / consume FLV `SCRIPTDATA` `onMetaData`.
                PROMEKI_DECLARE_ID(RtmpDataEnabled, VariantSpec()
                                                            .setType(DataTypeBool)
                                                            .setDefault(true)
                                                            .setDescription("Emit / consume onMetaData."));

                /// @brief int — bounded packetizer→writer queue depth.  Trade-off
                /// between absorbing TCP stalls and responsive backpressure.
                PROMEKI_DECLARE_ID(RtmpSendQueueDepth, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(64))
                                                              .setRange(int32_t(2), int32_t(1024))
                                                              .setDescription("Packetizer→writer queue depth."));

                /// @brief int — bounded depacketizer→aggregator queue depth.
                PROMEKI_DECLARE_ID(RtmpReadQueueDepth, VariantSpec()
                                                              .setType(DataTypeInt32)
                                                              .setDefault(int32_t(64))
                                                              .setRange(int32_t(2), int32_t(1024))
                                                              .setDescription("Depacketizer→aggregator queue depth."));

                /// @brief Enum @ref RtmpVideoPacing — sink-mode strand video pacing source.
                ///
                /// `Internal` (default) paces the strand against a built-in
                /// `WallClock` at the resolved `FrameRate`.  `External` defers
                /// entirely to the clock bound via `MediaIOPortGroup::setClock`
                /// (no fallback when unbound — gate is a no-op until a clock
                /// arrives).  `None` disables strand-level pacing and relies on
                /// bounded-queue backpressure alone.
                PROMEKI_DECLARE_ID(RtmpVideoPacing,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::RtmpVideoPacing::Internal)
                                           .setEnumType(promeki::RtmpVideoPacing::Type)
                                           .setDescription("RTMP sink video pacing source."));

                /// @brief int — `PacingGate` skip-verdict lag threshold in ms.
                ///
                /// Lag past which the strand drops the current frame to bound
                /// pile-up.  `0` (default) resolves to one frame interval at
                /// gate-arm time.
                PROMEKI_DECLARE_ID(RtmpPaceSkipThresholdMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(5000))
                                           .setDescription("PacingGate skip-verdict threshold (ms); "
                                                           "0 = one frame interval."));

                /// @brief int — `PacingGate` reanchor-verdict lag threshold in ms.
                ///
                /// Lag past which the gate re-anchors its timeline.  `0`
                /// (default) resolves to `PacingGate::DefaultReanchorMultiple ×
                /// frame interval` at gate-arm time.
                PROMEKI_DECLARE_ID(RtmpPaceReanchorThresholdMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(30000))
                                           .setDescription("PacingGate reanchor-verdict threshold (ms); "
                                                           "0 = 8 × frame interval."));

                // ============================================================
                // NDI (Network Device Interface) — NdiMediaIO
                //
                // The NDI MediaIO backend exposes a single
                // bidirectional stream per instance — sink mode for
                // sending and source mode for receiving.  Discovery
                // runs in a process-wide background thread (see
                // @ref NdiDiscovery) and these keys configure how the
                // backend interacts with it.
                // ============================================================

                /// @brief String — canonical NDI source name (`MachineName (Source)`)
                /// for source mode.  When the MediaIO is opened from an
                /// `ndi://` URL via @ref MediaIO::createFromUrl, this
                /// key is populated automatically from the URL parse.
                PROMEKI_DECLARE_ID(NdiSourceName, VariantSpec()
                                                          .setType(DataTypeString)
                                                          .setDefault(String())
                                                          .setDescription("Canonical NDI source name "
                                                                          "(MachineName (Source)) for source mode."));

                /// @brief String — sender-advertised name in sink mode.  The
                /// canonical name on the network ends up as
                /// `<MachineName> (<NdiSendName>)`.
                PROMEKI_DECLARE_ID(NdiSendName, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String("promeki"))
                                                        .setDescription("Name advertised by the NDI sender."));

                /// @brief String — comma-separated NDI groups the sender
                /// advertises to.  Empty (default) means all groups.  Acts
                /// as NDI's coarse access-control list — receivers only
                /// discover senders in groups they're configured to see.
                PROMEKI_DECLARE_ID(NdiSendGroups, VariantSpec()
                                                          .setType(DataTypeString)
                                                          .setDefault(String())
                                                          .setDescription("Comma-separated NDI groups for the sender."));

                /// @brief Enum @ref NdiBandwidth — receive-side bandwidth tier.
                /// Translates to @c NDIlib_recv_bandwidth_e at @c recv_create_v3 time.
                PROMEKI_DECLARE_ID(NdiBandwidth, VariantSpec()
                                                         .setType(DataTypeEnum)
                                                         .setDefault(promeki::NdiBandwidth::Highest)
                                                         .setEnumType(promeki::NdiBandwidth::Type)
                                                         .setDescription("NDI receiver bandwidth tier."));

                /// @brief Enum @ref NdiColorFormat — receive-side color-format hint.
                /// Translates to @c NDIlib_recv_color_format_e.
                /// Default is @c Fastest (returns the wire format — UYVY for
                /// 8-bit, P216 for 10/12/16-bit).  @c Best is not the default
                /// because the NDI Advanced SDK delivers PA16 under that mode,
                /// which libpromeki does not yet decode.
                PROMEKI_DECLARE_ID(NdiColorFormat, VariantSpec()
                                                           .setType(DataTypeEnum)
                                                           .setDefault(promeki::NdiColorFormat::Fastest)
                                                           .setEnumType(promeki::NdiColorFormat::Type)
                                                           .setDescription("NDI receiver color-format hint "
                                                                           "(default: Fastest — returns wire "
                                                                           "format, avoids PA16 from Advanced SDK)."));

                /// @brief String — comma-separated extra IPs / hostnames for
                /// non-mDNS discovery (subnets where Bonjour can't reach).
                /// Honoured by both senders and receivers.
                PROMEKI_DECLARE_ID(NdiExtraIps, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("Comma-separated extra IPs / hostnames for "
                                                                        "non-mDNS NDI discovery."));

                /// @brief bool — sender's @c clock_video flag.  When true
                /// (default), the SDK paces video frame submission to the
                /// declared frame rate, so a producer that pushes frames
                /// as fast as it can ends up sending at the natural rate
                /// rather than bursting onto the wire.  Set false only
                /// when the caller has its own pacing source (e.g. a
                /// hardware clock driving submission cadence).
                PROMEKI_DECLARE_ID(NdiSendClockVideo, VariantSpec()
                                                             .setType(DataTypeBool)
                                                             .setDefault(true)
                                                             .setDescription("Enable sender-side video clock pacing."));

                /// @brief bool — sender's @c clock_audio flag.  Default
                /// true (analogue of @ref NdiSendClockVideo for audio).
                PROMEKI_DECLARE_ID(NdiSendClockAudio, VariantSpec()
                                                             .setType(DataTypeBool)
                                                             .setDefault(true)
                                                             .setDescription("Enable sender-side audio clock pacing."));

                /// @brief int — timeout for one @c recv_capture_v3 poll
                /// inside the receiver's capture thread.  Bounds how
                /// quickly @c cancelBlockingWork interrupts an in-flight
                /// blocking receive.
                PROMEKI_DECLARE_ID(NdiCaptureTimeoutMs, VariantSpec()
                                                               .setType(DataTypeInt32)
                                                               .setDefault(int32_t(100))
                                                               .setRange(int32_t(10), int32_t(5000))
                                                               .setDescription("recv_capture_v3 poll timeout in ms."));

                /// @brief Duration — maximum time the receiver waits for
                /// @c NdiDiscovery to register the requested source
                /// before giving up.  Capped at 30 s defensively.  Bare
                /// numbers passed via the CLI are parsed as seconds, so
                /// `--dc NdiFindWait:5` means 5 s; pass `5000ms` etc.
                /// for finer units.
                PROMEKI_DECLARE_ID(NdiFindWait, VariantSpec()
                                                        .setType(DataTypeDuration)
                                                        .setDefault(Duration::fromSeconds(3))
                                                        .setDescription("Max wait for NdiDiscovery to "
                                                                        "register the requested source."));

                /// @brief Enum @ref NdiReceiveBitDepth — promised bit depth
                /// for received P216 frames.  See the Enum's documentation
                /// for the in-band-vs-out-of-band convention.
                PROMEKI_DECLARE_ID(NdiReceiveBitDepth, VariantSpec()
                                                              .setType(DataTypeEnum)
                                                              .setDefault(promeki::NdiReceiveBitDepth::Auto)
                                                              .setEnumType(promeki::NdiReceiveBitDepth::Type)
                                                              .setDescription("Promised bit depth for received "
                                                                              "P216 frames (Auto = 16)."));

                // ============================================================
                // V4L2 capture (Linux)
                // ============================================================

                /// @brief String — V4L2 device node path (e.g. "/dev/video0").
                PROMEKI_DECLARE_ID(V4l2DevicePath, VariantSpec()
                                                           .setType(DataTypeString)
                                                           .setDefault(String())
                                                           .setDescription("V4L2 device node path."));

                /// @brief int — number of MMAP capture buffers (2-32).
                PROMEKI_DECLARE_ID(V4l2BufferCount, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(4))
                                                            .setRange(int32_t(2), int32_t(32))
                                                            .setDescription("Number of V4L2 MMAP capture buffers."));

                /// @brief String — ALSA capture device name for paired audio.
                /// "auto" (default) auto-detects a paired USB audio device.
                /// "none" or empty disables audio capture.
                /// Any other value is used as-is (e.g. "hw:1,0", "default").
                PROMEKI_DECLARE_ID(V4l2AudioDevice,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String("auto"))
                                           .setDescription("ALSA capture device for paired audio. "
                                                           "\"auto\" = auto-detect, \"none\" or empty = disabled."));

                // ---- V4L2 camera controls ----
                //
                // These map directly to V4L2 CID controls.  A value of
                // -1 (the default) means "don't touch, use device default."
                // Actual ranges are device-dependent; see --probe output.

                /// @brief int — Brightness (V4L2_CID_BRIGHTNESS).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Brightness, VariantSpec()
                                                           .setType(DataTypeInt32)
                                                           .setDefault(int32_t(-1))
                                                           .setDescription("Brightness (-1 = device default)."));

                /// @brief int — Contrast (V4L2_CID_CONTRAST).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Contrast, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(-1))
                                                         .setDescription("Contrast (-1 = device default)."));

                /// @brief int — Saturation (V4L2_CID_SATURATION).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Saturation, VariantSpec()
                                                           .setType(DataTypeInt32)
                                                           .setDefault(int32_t(-1))
                                                           .setDescription("Saturation (-1 = device default)."));

                /// @brief int — Hue (V4L2_CID_HUE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Hue, VariantSpec()
                                                    .setType(DataTypeInt32)
                                                    .setDefault(int32_t(-1))
                                                    .setDescription("Hue (-1 = device default)."));

                /// @brief int — Gamma (V4L2_CID_GAMMA).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Gamma, VariantSpec()
                                                      .setType(DataTypeInt32)
                                                      .setDefault(int32_t(-1))
                                                      .setDescription("Gamma (-1 = device default)."));

                /// @brief int — Sharpness (V4L2_CID_SHARPNESS).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Sharpness, VariantSpec()
                                                          .setType(DataTypeInt32)
                                                          .setDefault(int32_t(-1))
                                                          .setDescription("Sharpness (-1 = device default)."));

                /// @brief int — Backlight compensation (V4L2_CID_BACKLIGHT_COMPENSATION).
                /// -1 = device default.
                PROMEKI_DECLARE_ID(V4l2BacklightComp,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setDescription("Backlight compensation (-1 = device default)."));

                /// @brief int — White balance temperature in Kelvin
                /// (V4L2_CID_WHITE_BALANCE_TEMPERATURE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2WhiteBalanceTemp,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setDescription("White balance temperature in K (-1 = device default)."));

                /// @brief bool — Auto white balance (V4L2_CID_AUTO_WHITE_BALANCE).
                /// -1 = device default, 0 = off, 1 = on.
                PROMEKI_DECLARE_ID(
                        V4l2AutoWhiteBalance,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(-1))
                                .setRange(int32_t(-1), int32_t(1))
                                .setDescription("Auto white balance (-1 = device default, 0 = off, 1 = on)."));

                /// @brief int — Exposure time, absolute, in 100µs units
                /// (V4L2_CID_EXPOSURE_ABSOLUTE).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2ExposureAbsolute,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setDescription("Exposure time in 100us units (-1 = device default)."));

                /// @brief Enum @ref V4l2ExposureMode — auto exposure mode (V4L2_CID_EXPOSURE_AUTO).
                PROMEKI_DECLARE_ID(V4l2AutoExposure,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(Enum())
                                           .setEnumType(V4l2ExposureMode::Type)
                                           .setDescription("Auto exposure mode (empty = device default)."));

                /// @brief int — Gain (V4L2_CID_GAIN).  -1 = device default.
                PROMEKI_DECLARE_ID(V4l2Gain, VariantSpec()
                                                     .setType(DataTypeInt32)
                                                     .setDefault(int32_t(-1))
                                                     .setDescription("Gain (-1 = device default)."));

                /// @brief Enum @ref V4l2PowerLineMode — power line frequency filter (V4L2_CID_POWER_LINE_FREQUENCY).
                PROMEKI_DECLARE_ID(V4l2PowerLineFreq,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(Enum())
                                           .setEnumType(V4l2PowerLineMode::Type)
                                           .setDescription("Power line frequency (empty = device default)."));

                /// @brief int — JPEG compression quality 1-100
                /// (V4L2_CID_JPEG_COMPRESSION_QUALITY).  -1 = device default.
                /// Not all devices support this control.
                PROMEKI_DECLARE_ID(V4l2JpegQuality,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setDescription("JPEG compression quality 1-100 (-1 = device default)."));

                // ============================================================
                // TranscriptionEngine — speech-to-text sessions
                //
                // Knobs consumed by @ref TranscriptionEngine::configure.
                // The input @ref AudioDesc is carried implicitly by every
                // submitted @ref PcmAudioPayload on the source @ref Frame
                // — these keys only describe the *session* (which audio
                // stream/channels to listen to, language hint, model
                // hint, streaming-vs-batch behaviour, endpointing).
                // ============================================================

                /// @brief int — 0-based audio stream index on the input
                ///        @ref Frame that the engine should transcribe.
                ///
                /// Mirrors the @c streamIndex argument the encoder /
                /// decoder symmetry uses (see
                /// @ref AudioEncoder::selectInputPayload).  @c -1 means
                /// "the first PCM audio payload found, regardless of its
                /// @ref MediaPayload::streamIndex" — the natural choice
                /// when the source frame carries only one audio stream.
                PROMEKI_DECLARE_ID(TranscriptionStreamIndex,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setMin(int32_t(-1))
                                           .setDescription(
                                                   "0-based audio stream index on the source Frame "
                                                   "for transcription (-1 = first PCM audio payload found)."));

                /// @brief Enum @ref TranscriptionChannelMode — how the
                ///        engine selects which sample channels to feed
                ///        the speech-to-text decoder.
                ///
                /// - @c ChannelMap   — use the channels whose
                ///                     @ref ChannelRole appears in
                ///                     @ref TranscriptionChannelMap.
                /// - @c ChannelIndex — pick exactly the channel named
                ///                     by @ref TranscriptionChannelIndex.
                /// - @c DownmixAll   — sum every channel of the payload
                ///                     to mono.
                PROMEKI_DECLARE_ID(TranscriptionChannelMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(promeki::TranscriptionChannelMode::Type)
                                           .setDefault(promeki::TranscriptionChannelMode(
                                                   promeki::TranscriptionChannelMode::ChannelMap))
                                           .setDescription(
                                                   "How the engine selects channels from the input PCM payload "
                                                   "(ChannelMap, ChannelIndex, DownmixAll)."));

                /// @brief AudioChannelMap — role-based channel selection
                ///        for transcription.
                ///
                /// Consulted when @ref TranscriptionChannelMode is
                /// @c ChannelMap.  The engine downmixes the channels of
                /// the input PCM payload whose @ref ChannelRole appears
                /// in this map; channels whose role is absent are
                /// dropped.  An empty map means "use the dialog stem
                /// when present (@c FrontCenter), else fall back to
                /// downmix all" — engines may refine this default.
                ///
                /// Default is empty; pass @c {FrontCenter} for the 5.1
                /// dialog stem, @c {Mono} to pick the commentary track
                /// out of a mixed-stream buffer, etc.
                PROMEKI_DECLARE_ID(TranscriptionChannelMap,
                                   VariantSpec()
                                           .setType(DataTypeAudioChannelMap)
                                           .setDefault(promeki::AudioChannelMap())
                                           .setDescription(
                                                   "AudioChannelMap selecting roles for role-based "
                                                   "transcription channel selection."));

                /// @brief int — single channel index (0-based) when
                ///        @ref TranscriptionChannelMode is
                ///        @c ChannelIndex.  Ignored otherwise.
                PROMEKI_DECLARE_ID(TranscriptionChannelIndex,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription(
                                                   "Single 0-based channel index for index-based transcription "
                                                   "channel selection (used when ChannelMode = ChannelIndex)."));

                /// @brief String — BCP 47 language hint for the engine
                ///        (e.g. @c "en", @c "en-US", @c "es-MX").
                ///
                /// Empty (default) means "let the engine auto-detect or
                /// use its built-in default."  Engines without language
                /// detection treat an empty hint as their fall-back
                /// language (typically English).
                PROMEKI_DECLARE_ID(TranscriptionLanguage,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription(
                                                   "BCP 47 language hint (e.g. \"en-US\"); "
                                                   "empty = engine default / auto-detect."));

                /// @brief Enum @ref TranscriptionMode — streaming vs
                ///        batch session behaviour.
                ///
                /// @c Streaming — engine may emit interim partial cues
                ///                during @c submitFrame; finalised cues
                ///                follow once the endpoint heuristic
                ///                fires.
                /// @c Batch     — engine accumulates audio silently and
                ///                only emits cues after @c flush.
                ///
                /// Engines that only support one mode reject the other
                /// at @c configure with @c Error::NotSupported.
                PROMEKI_DECLARE_ID(TranscriptionSessionMode,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(promeki::TranscriptionMode::Type)
                                           .setDefault(promeki::TranscriptionMode(
                                                   promeki::TranscriptionMode::Streaming))
                                           .setDescription(
                                                   "Streaming (interim partials) vs Batch (final-only on flush) "
                                                   "session behaviour."));

                /// @brief String — engine-specific model hint
                ///        (e.g. @c "whisper.cpp:large-v3", @c "vosk:small-en").
                ///
                /// Empty (default) means "engine picks its own default
                /// model."  Format is engine-specific; engines that
                /// don't recognise the hint either fall back to their
                /// default or fail @c configure with
                /// @c Error::NotSupported per their own policy.
                PROMEKI_DECLARE_ID(TranscriptionModelHint,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription(
                                                   "Engine-specific model identifier hint "
                                                   "(empty = engine default)."));

                /// @brief bool — request speaker diarization output.
                ///
                /// When supported by the engine, populates each emitted
                /// @ref Subtitle::speaker with the diarised speaker
                /// identifier (typically @c "S1", @c "S2", …).  Engines
                /// without diarization treat this as a no-op and leave
                /// @c speaker empty.
                PROMEKI_DECLARE_ID(TranscriptionDiarization,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription(
                                                   "Request per-cue speaker diarization "
                                                   "(populates Subtitle::speaker when supported)."));

                /// @brief bool — request word-level timestamps.
                ///
                /// When supported, the engine splits long utterances
                /// into multiple smaller cues whose @c start / @c end
                /// align with word boundaries, rather than emitting
                /// one cue per sentence.  Useful for karaoke-style
                /// rendering and tighter sync.
                PROMEKI_DECLARE_ID(TranscriptionWordTimestamps,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription(
                                                   "Emit word-level timestamped cues rather than "
                                                   "sentence-level cues."));

                /// @brief bool — gate decoding on voice-activity
                ///        detection.
                ///
                /// When enabled, the engine consults its own VAD and
                /// skips decode on segments that look like silence /
                /// noise — useful for live capture where most audio is
                /// non-speech.  Disabled by default since some engines
                /// VAD-unconditionally and gain nothing from this.
                PROMEKI_DECLARE_ID(TranscriptionVad,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription(
                                                   "Enable voice-activity detection gating before decode."));

                /// @brief Duration — endpoint silence threshold.
                ///
                /// In @c Streaming mode, when no speech is observed for
                /// this long the engine finalises the pending partial
                /// cue and emits a final cue.  Ignored in @c Batch
                /// mode.  @c Duration::zero() (default) means "engine
                /// default" — typical values are 300-800 ms.
                PROMEKI_DECLARE_ID(TranscriptionEndpointSilence,
                                   VariantSpec()
                                           .setType(DataTypeDuration)
                                           .setDefault(Duration::zero())
                                           .setDescription(
                                                   "Streaming endpoint silence threshold "
                                                   "(Duration::zero() = engine default)."));

                // ============================================================
                // SubtitleCueBuilder — Transcript → Subtitle cue shaping
                //
                // The cue builder consumes Transcript values (typically
                // produced by a @ref TranscriptionEngine, but anywhere
                // word-timed text comes from works equally well) and
                // emits @ref Subtitle cues per a configurable layout /
                // merge / partial-gating policy.
                // ============================================================

                /// @brief int — maximum characters per cue line.
                ///        @c 0 disables wrapping.  Default 42 matches
                ///        the SRT / CEA-708 convention for English.
                PROMEKI_DECLARE_ID(SubtitleCueMaxCharsPerLine,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(42))
                                           .setMin(int32_t(0))
                                           .setDescription(
                                                   "Maximum characters per cue line (0 = no wrap)."));

                /// @brief int — maximum lines per cue.  Default 2
                ///        matches the SRT / CEA-608 / CEA-708 norm.
                PROMEKI_DECLARE_ID(SubtitleCueMaxLines,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(2))
                                           .setMin(int32_t(1))
                                           .setDescription("Maximum lines per cue."));

                /// @brief Duration — minimum cue display time.  The
                ///        builder extends the cue's @c end so the cue
                ///        stays on screen at least this long.
                ///        Default 1 s matches the WebVTT / Netflix
                ///        accessibility recommendation.
                PROMEKI_DECLARE_ID(SubtitleCueMinDuration,
                                   VariantSpec()
                                           .setType(DataTypeDuration)
                                           .setDefault(Duration::fromMilliseconds(1000))
                                           .setDescription("Minimum cue display duration."));

                /// @brief Duration — maximum cue display time.  The
                ///        builder truncates the cue's @c end so it
                ///        leaves the screen by this point.  Default
                ///        7 s matches the standard reading-speed cap.
                PROMEKI_DECLARE_ID(SubtitleCueMaxDuration,
                                   VariantSpec()
                                           .setType(DataTypeDuration)
                                           .setDefault(Duration::fromMilliseconds(7000))
                                           .setDescription("Maximum cue display duration."));

                /// @brief bool — emit cues for partial / interim
                ///        transcripts.  Default @c false — most
                ///        consumers (offline SRT writers, file-based
                ///        burn-in) only want finalised cues.  Live
                ///        captioning enables this to surface interim
                ///        hypotheses with @ref Subtitle::partial set.
                PROMEKI_DECLARE_ID(SubtitleCueEmitPartials,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription(
                                                   "Emit cues for partial transcripts (default off)."));

                /// @brief Enum @ref SubtitleAnchor — default cue
                ///        anchor.  The builder stamps this on every
                ///        emitted cue; transcripts whose source
                ///        metadata already carries an anchor override
                ///        this value.  Default @c BottomCenter.
                PROMEKI_DECLARE_ID(SubtitleCueAnchor,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setEnumType(promeki::SubtitleAnchor::Type)
                                           .setDefault(promeki::SubtitleAnchor(
                                                   promeki::SubtitleAnchor::BottomCenter))
                                           .setDescription("Default cue anchor for emitted Subtitles."));

                // ============================================================
                // AJA NTV2 (SDI / HDMI capture & playout) — Ntv2MediaIO
                //
                // The NTV2 MediaIO backend wraps AJA's libajantv2 SDK.
                // One MediaIO instance represents one *logical channel*
                // on an AJA card: a framebuffer + the SDI/HDMI port(s)
                // bound to it + an optional audio system + an optional
                // ANC engine.  The carrier-level configuration (which
                // physical ports, which SMPTE link standard, which
                // reference clock) is supplied via the generic
                // @ref SdiInputSignal / @ref SdiOutputSignal /
                // @ref HdmiInputSignal / @ref HdmiOutputSignal /
                // @ref VideoReference keys.  The keys in this block are
                // strictly AJA-specific identity / behaviour knobs.
                // ============================================================

                /// @brief int — AJA device index (0-based, as enumerated
                /// by @c CNTV2DeviceScanner).  -1 (default) routes
                /// identification through @ref Ntv2DeviceName instead.
                PROMEKI_DECLARE_ID(Ntv2DeviceIndex,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setDescription("AJA device index (0-based); -1 = use Ntv2DeviceName."));

                /// @brief String — AJA device locator: either a name
                /// shorthand recognised by @c CNTV2DeviceScanner
                /// (e.g. "kona5", "corvid44") or @c "serial:NNN" to bind
                /// by the physical board serial number.  Empty (default)
                /// means "use Ntv2DeviceIndex".
                PROMEKI_DECLARE_ID(Ntv2DeviceName,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("AJA device locator (name shorthand or \"serial:NNN\")."));

                /// @brief int — 1-based logical channel index on the
                /// card (matches AJA's @c NTV2Channel numbering).
                /// Identifies the framebuffer + AutoCirculate resource
                /// this MediaIO will own.
                PROMEKI_DECLARE_ID(Ntv2Channel,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(1))
                                           .setRange(int32_t(1), int32_t(8))
                                           .setDescription("1-based logical channel index on the AJA card."));

                /// @brief int — Audio system to use for this channel.
                /// -1 (default) auto-pairs with the channel index;
                /// 1..N selects an explicit system; 0 disables audio
                /// capture / playout for the channel.
                PROMEKI_DECLARE_ID(Ntv2AudioSystem,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(-1))
                                           .setRange(int32_t(-1), int32_t(8))
                                           .setDescription("NTV2 audio system (-1 = auto-pair with channel, 0 = disabled, 1..N = explicit)."));

                /// @brief bool — enable the ANC extractor (source mode)
                /// or inserter (sink mode) for this channel.  Requires
                /// hardware that reports @c CanDoCustomAnc; the backend
                /// returns @c Error::NotSupported at open time if the
                /// card lacks the ANC engine and this is true.
                PROMEKI_DECLARE_ID(Ntv2WithAnc,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Enable ANC extractor / inserter for this channel."));

                /// @brief bool — if true, leave AJA's retail services
                /// running rather than switching to @c NTV2_OEM_TASKS
                /// for the duration of the open.  Default false (mirror
                /// the demo behaviour, which is the only safe choice
                /// when libpromeki owns the card).
                PROMEKI_DECLARE_ID(Ntv2RetailServices,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Keep AJA retail services running (default false → OEM tasks)."));

                /// @brief bool — allow channels on this card to run at
                /// independent video formats (corresponds to AJA's
                /// @c MultiFormatMode).  Default true.
                PROMEKI_DECLARE_ID(Ntv2MultiFormatMode,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Allow per-channel independent video formats."));

                /// @brief bool — when true, the negotiator may accept
                /// a framestore PixelFormat in a different colour
                /// family from the wire and bridge the mismatch via
                /// the card's on-board CSC widgets.  Default false
                /// (CSC enabled).  Set true to keep the on-board CSCs
                /// out of negotiation — useful when the host already
                /// has GPU / SIMD CSC pipelines and the user wants the
                /// CSC engines reserved for their own routing.
                PROMEKI_DECLARE_ID(Ntv2DisableOnBoardCsc,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription(
                                                   "Refuse on-board CSC insertion in routing / "
                                                   "format negotiation; force a software CSC bridge "
                                                   "on every RGB ↔ YCbCr boundary."));

                /// @brief bool — page-lock host frame buffers via
                /// @c DMABufferLock so AutoCirculate DMA bypasses the
                /// pinning trip on every transfer.  Default true; set
                /// false to fall back to plain heap buffers when the
                /// kernel rejects the pin (e.g. RLIMIT_MEMLOCK
                /// exhausted).
                PROMEKI_DECLARE_ID(Ntv2BufferLockMode,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription("Page-lock host buffers for DMA throughput."));

                /// @brief int — VBI poll timeout (ms) used by the
                /// capture / playout worker so @ref MediaIO close can
                /// interrupt @c WaitForInputVerticalInterrupt in
                /// finite time.  Bounds how quickly
                /// @c cancelBlockingWork unwinds an in-flight blocking
                /// read.
                PROMEKI_DECLARE_ID(Ntv2VbiTimeoutMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(50))
                                           .setRange(int32_t(5), int32_t(1000))
                                           .setDescription("WaitForInputVerticalInterrupt poll timeout in ms."));

                /// @brief int — input-signal poll cadence (in VBIs)
                /// used by the capture worker to detect signal loss
                /// (and re-acquire when a signal returns).  The card's
                /// @c GetInputVideoFormat is consulted every Nth VBI;
                /// transitions emit
                /// @c MediaIO::errorOccurredSignal(Error::SignalLoss)
                /// on loss and a re-acquire log line on recovery.
                /// Default 15 — matches a 1-Hz cadence at 60 fps and a
                /// ~2-Hz cadence at 30 fps, balancing detection latency
                /// against IOCTL overhead.  Set 0 to disable the poll.
                PROMEKI_DECLARE_ID(Ntv2SignalPollIntervalVbi,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(15))
                                           .setRange(int32_t(0), int32_t(600))
                                           .setDescription(
                                                   "Input-signal poll cadence in VBIs; 0 disables "
                                                   "the periodic GetInputVideoFormat check."));

                /// @brief int — `PacingGate` skip-verdict lag threshold
                /// in ms for the NTV2 sink path.  Used only when an
                /// external @ref Clock has been bound via
                /// @c MediaIOPortGroup::setClock (Phase 6 external
                /// pacing).  Lag past which the playout worker drops
                /// the next frame instead of submitting late.  `0`
                /// (default) resolves to one frame interval at gate-
                /// arm time.
                PROMEKI_DECLARE_ID(Ntv2PaceSkipThresholdMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(5000))
                                           .setDescription(
                                                   "External-pacing PacingGate skip threshold (ms); "
                                                   "0 = one frame interval."));

                /// @brief int — `PacingGate` reanchor-verdict lag
                /// threshold in ms for the NTV2 sink path.  Used only
                /// under external pacing (see @ref Ntv2PaceSkipThresholdMs).
                /// Lag past which the gate re-anchors its timeline so
                /// the next frame starts fresh.  `0` (default) resolves
                /// to @c PacingGate::DefaultReanchorMultiple × frame
                /// interval at gate-arm time.
                PROMEKI_DECLARE_ID(Ntv2PaceReanchorThresholdMs,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(30000))
                                           .setDescription(
                                                   "External-pacing PacingGate reanchor threshold "
                                                   "(ms); 0 = 8 × frame interval."));

                /// @brief bool — master enable for NTV2 VPID stamping.
                ///
                /// When @c true (default), the sink open path writes
                /// SMPTE ST 352 VPID byte-4 overrides for transfer /
                /// colorimetry / luminance / RGB range based on the
                /// frame's colour description, and the source open
                /// path reads incoming VPID and stamps the detected
                /// transfer / colorimetry / range on every captured
                /// @ref Frame.  Setting @c false keeps the card's
                /// auto-derived VPID (good for legacy SDR pipelines
                /// where the receiver derives signalling from the
                /// video format alone).
                PROMEKI_DECLARE_ID(Ntv2VpidEnable,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(true)
                                           .setDescription(
                                                   "Enable NTV2 VPID overrides "
                                                   "(transfer / colorimetry / luminance / RGB range)."));

                /// @brief Enum @ref TransferCharacteristics — VPID
                /// transfer-characteristic override applied to sinks.
                ///
                /// `Auto` (default) means "derive from the open-time
                /// `ImageDesc` colour model via `ColorModel::toH273`."
                /// Any explicit value (e.g. `SMPTE2084` for PQ,
                /// `ARIB_STD_B67` for HLG, `BT709` for SDR) pins the
                /// VPID transfer field regardless of the frame's
                /// colorimetry.  Has no effect on source channels
                /// (VPID is read-only there).
                PROMEKI_DECLARE_ID(Ntv2VpidTransferOverride,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::TransferCharacteristics::Auto)
                                           .setEnumType(promeki::TransferCharacteristics::Type)
                                           .setDescription(
                                                   "Sink VPID transfer override "
                                                   "(Auto = derive from frame colour)."));

                /// @brief Enum @ref ColorPrimaries — VPID colorimetry
                /// override applied to sinks.
                ///
                /// `Auto` (default) means "derive from the open-time
                /// `ImageDesc` colour model."  Any explicit value
                /// (`BT709`, `BT2020`, …) pins the VPID colorimetry
                /// field.  Has no effect on source channels.
                PROMEKI_DECLARE_ID(Ntv2VpidColorimetryOverride,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::ColorPrimaries::Auto)
                                           .setEnumType(promeki::ColorPrimaries::Type)
                                           .setDescription(
                                                   "Sink VPID colorimetry override "
                                                   "(Auto = derive from frame colour)."));

                /// @brief Enum @ref VideoRange — VPID RGB-range
                /// override applied to sinks.
                ///
                /// `Unknown` (default) means "derive from the
                /// `ImageDesc` pixel format range bit, or fall back
                /// to narrow per the SMPTE ST 352 convention."
                /// `Limited` / `Full` pin the VPID range bit.  Has
                /// no effect on source channels.
                PROMEKI_DECLARE_ID(Ntv2VpidRangeOverride,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::VideoRange::Unknown)
                                           .setEnumType(promeki::VideoRange::Type)
                                           .setDescription(
                                                   "Sink VPID RGB-range override "
                                                   "(Unknown = derive from frame range)."));
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

#endif // PROMEKI_ENABLE_PROAV