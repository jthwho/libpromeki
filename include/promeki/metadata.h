/**
 * @file      metadata.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/variantdatabase.h>
#include <promeki/sharedptr.h>
#include <promeki/audiomarker.h>
#include <promeki/enums.h>
#include <promeki/mediatimestamp.h>
#include <promeki/framenumber.h>
#include <promeki/mediaduration.h>
#include <promeki/subtitle.h>
#include <promeki/timecode.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/eui64.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class StringList;

/**
 * @brief Key-value metadata container using typed Variant values.
 * @ingroup util
 *
 * Stores metadata entries keyed by well-known string-registered IDs.
 * Each value is stored as a Variant, supporting types such as String,
 * int, double, bool, Timecode, and Rational. Supports JSON
 * serialization and deserialization.
 *
 * Each ID is declared via @ref declareID with a mandatory @ref VariantSpec
 * that captures the accepted type, default value, and description.
 *
 * @par Storage and copy semantics
 * Metadata is a value type with internal copy-on-write sharing,
 * inherited from its @ref VariantDatabase base.  Copying a Metadata
 * is O(1); the entry map is only deep-copied when one of the aliased
 * handles is mutated.  This is the property that lets Metadata ride
 * on every Frame copy through a pipeline without deep-copying the
 * entry map at every stage.
 *
 * @par Example
 * @code
 * Metadata meta;
 * meta.set(Metadata::Title, String("My Video"));
 * meta.set(Metadata::FrameRate, Rational<int>(24, 1));
 *
 * String title = meta.get(Metadata::Title).get<String>();
 * bool has = meta.contains(Metadata::Copyright);  // false
 * @endcode
 */
class Metadata : public VariantDatabase<"Metadata"> {
        public:
                /** @brief Base class alias. */
                using Base = VariantDatabase<"Metadata">;

                using Base::Base;

                // ============================================================
                // Core metadata
                // ============================================================

                /// @brief SMPTE timecode associated with this media unit.
                PROMEKI_DECLARE_ID(Timecode,
                                   VariantSpec()
                                           .setType(DataTypeTimecode)
                                           .setDefault(promeki::Timecode())
                                           .setDescription("SMPTE timecode associated with this media unit."));

#if PROMEKI_ENABLE_PROAV
                /// @brief Subtitle cue active at this media unit.
                ///
                /// Frame-level attribution of the (single) subtitle that
                /// should be displayed concurrently with this media unit
                /// — typically stamped by a subtitle source / SubRip
                /// player onto the Frame whose timestamp matches the
                /// cue's @ref Subtitle::start.  Downstream consumers
                /// (renderer, MediaIO sinks that re-emit subtitles to
                /// a different transport) can read the cue without
                /// touching the source file or the ANC pipeline.
                PROMEKI_DECLARE_ID(Subtitle,
                                   VariantSpec()
                                           .setType(DataTypeSubtitle)
                                           .setDefault(promeki::Subtitle())
                                           .setDescription(
                                                   "Subtitle cue active at this media unit (start <= ts < end)."));
#endif

                /// @brief Gamma / transfer-function exponent.
                PROMEKI_DECLARE_ID(Gamma, VariantSpec()
                                                  .setType(DataTypeDouble)
                                                  .setDefault(0.0)
                                                  .setMin(0.0)
                                                  .setDescription("Gamma / transfer-function exponent."));

                /// @brief Title of the media.
                PROMEKI_DECLARE_ID(Title, VariantSpec()
                                                  .setType(DataTypeString)
                                                  .setDefault(String())
                                                  .setDescription("Title of the media."));

                /// @brief Copyright notice.
                PROMEKI_DECLARE_ID(Copyright, VariantSpec()
                                                      .setType(DataTypeString)
                                                      .setDefault(String())
                                                      .setDescription("Copyright notice."));

                /// @brief Software that created or last modified the media.
                PROMEKI_DECLARE_ID(Software, VariantSpec()
                                                     .setType(DataTypeString)
                                                     .setDefault(String())
                                                     .setDescription("Software that created or modified the media."));

                /// @brief Artist or creator name.
                PROMEKI_DECLARE_ID(Artist, VariantSpec()
                                                   .setType(DataTypeString)
                                                   .setDefault(String())
                                                   .setDescription("Artist or creator name."));

                /// @brief Free-form comment.
                PROMEKI_DECLARE_ID(Comment, VariantSpec()
                                                    .setType(DataTypeString)
                                                    .setDefault(String())
                                                    .setDescription("Free-form comment."));

                /// @brief Creation or origination date.
                PROMEKI_DECLARE_ID(Date, VariantSpec()
                                                 .setType(DataTypeString)
                                                 .setDefault(String())
                                                 .setDescription("Creation or origination date."));

                /// @brief Album name (audio media).
                PROMEKI_DECLARE_ID(Album, VariantSpec()
                                                  .setType(DataTypeString)
                                                  .setDefault(String())
                                                  .setDescription("Album name (audio media)."));

                /// @brief License information.
                PROMEKI_DECLARE_ID(License, VariantSpec()
                                                    .setType(DataTypeString)
                                                    .setDefault(String())
                                                    .setDescription("License information."));

                /// @brief Track number (audio media).
                PROMEKI_DECLARE_ID(TrackNumber, VariantSpec()
                                                        .setTypes({DataTypeInt32, DataTypeString})
                                                        .setDefault(String())
                                                        .setDescription("Track number (audio media)."));

                /// @brief Genre (audio media).
                PROMEKI_DECLARE_ID(Genre, VariantSpec()
                                                  .setType(DataTypeString)
                                                  .setDefault(String())
                                                  .setDescription("Genre (audio media)."));

                /// @brief Enable Broadcast Wave Format metadata in audio files.
                PROMEKI_DECLARE_ID(EnableBWF, VariantSpec()
                                                      .setType(DataTypeBool)
                                                      .setDefault(false)
                                                      .setDescription("Enable Broadcast Wave Format metadata."));

                /// @brief Human-readable description of the content.
                PROMEKI_DECLARE_ID(Description, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("Human-readable description of the content."));

                /// @brief BWF originator name.
                PROMEKI_DECLARE_ID(Originator, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("BWF originator name."));

                /// @brief BWF originator reference.
                PROMEKI_DECLARE_ID(OriginatorReference, VariantSpec()
                                                                .setTypes({DataTypeString, DataTypeUUID})
                                                                .setDefault(String())
                                                                .setDescription("BWF originator reference."));

                /// @brief BWF origination date and time.
                PROMEKI_DECLARE_ID(OriginationDateTime, VariantSpec()
                                                                .setType(DataTypeString)
                                                                .setDefault(String())
                                                                .setDescription("BWF origination date and time."));

                /// @brief Frame rate of the associated video.
                PROMEKI_DECLARE_ID(FrameRate, VariantSpec()
                                                      .setTypes({DataTypeRational, DataTypeDouble,
                                                                 DataTypeFrameRate})
                                                      .setDefault(Rational<int>())
                                                      .setDescription("Frame rate of the associated video."));

                /// @brief Source that supplied the associated FrameRate (String).
                /// One of: @c "file" (read from the container/sidecar),
                /// @c "config" (caller-supplied override), or @c "default"
                /// (backend fell back to its built-in default).
                PROMEKI_DECLARE_ID(
                        FrameRateSource,
                        VariantSpec()
                                .setType(DataTypeString)
                                .setDefault(String())
                                .setDescription("Source of the FrameRate value (file, config, or default)."));

                /// @brief SMPTE UMID (Unique Material Identifier).
                PROMEKI_DECLARE_ID(UMID, VariantSpec()
                                                 .setTypes({DataTypeString, DataTypeUMID})
                                                 .setDefault(String())
                                                 .setDescription("SMPTE UMID (Unique Material Identifier)."));

                /// @brief BWF coding history string.
                PROMEKI_DECLARE_ID(CodingHistory, VariantSpec()
                                                          .setType(DataTypeString)
                                                          .setDefault(String())
                                                          .setDescription("BWF coding history string."));

                // ============================================================
                // Compression metadata
                // ============================================================

                /// @brief Compression level hint for lossy codecs.
                PROMEKI_DECLARE_ID(CompressionLevel,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Compression level hint for lossy codecs."));

                /// @brief Enable variable bit-rate encoding.
                PROMEKI_DECLARE_ID(EnableVBR, VariantSpec()
                                                      .setType(DataTypeBool)
                                                      .setDefault(false)
                                                      .setDescription("Enable variable bit-rate encoding."));

                /// @brief VBR quality setting (codec-specific).
                PROMEKI_DECLARE_ID(VBRQuality, VariantSpec()
                                                       .setType(DataTypeInt32)
                                                       .setDefault(int32_t(0))
                                                       .setDescription("VBR quality setting (codec-specific)."));

                /// @brief Internal: allocation hint for compressed pixel formats.
                /// Use CompressedVideoPayload::size() instead.
                PROMEKI_DECLARE_ID(CompressedSize,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Internal allocation hint for compressed pixel formats."));

                // ============================================================
                // Streaming / frame status
                // ============================================================

                /// @brief Signals end-of-stream to downstream nodes.
                PROMEKI_DECLARE_ID(EndOfStream, VariantSpec()
                                                        .setType(DataTypeBool)
                                                        .setDefault(false)
                                                        .setDescription("Signals end-of-stream to downstream nodes."));

                /// @brief Marks a media unit as carrying corrupt data.
                ///
                /// Set by decoders / parsers when a packet, frame, or audio
                /// buffer is known to be corrupt (checksum failure, truncated
                /// bitstream, unrecoverable error concealment).  Downstream
                /// nodes typically drop or pass-through marked units depending
                /// on policy.  Pair with @ref CorruptReason when a
                /// human-readable explanation is available.
                PROMEKI_DECLARE_ID(Corrupt, VariantSpec()
                                                    .setType(DataTypeBool)
                                                    .setDefault(false)
                                                    .setDescription("Media unit carries corrupt data."));

                /// @brief Human-readable explanation for a Corrupt marking.
                PROMEKI_DECLARE_ID(CorruptReason,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Human-readable reason for a Corrupt marking."));

                /// @brief Frame sequence number within a stream.
                PROMEKI_DECLARE_ID(FrameNumber, VariantSpec()
                                                        .setType(DataTypeFrameNumber)
                                                        .setDefault(promeki::FrameNumber())
                                                        .setDescription("Frame sequence number within a stream."));

                /// @brief Total media span as a (start, length) pair.
                ///
                /// Used at the container / clip level to record
                /// "this asset covers @c length frames starting at frame
                /// @c start".  Per-frame metadata should use
                /// @ref FrameNumber instead.
                PROMEKI_DECLARE_ID(Duration,
                                   VariantSpec()
                                           .setType(DataTypeMediaDuration)
                                           .setDefault(promeki::MediaDuration())
                                           .setDescription("Clip-level duration: starting frame plus length."));

                /// @brief Timestamp of when the library or device captured this data.
                ///
                /// Records when the frame was first observed by the library
                /// or the device it talks to.  For live capture this is
                /// typically SystemMonotonic; for network streams it is the
                /// moment the first packet arrived.
                PROMEKI_DECLARE_ID(CaptureTime,
                                   VariantSpec()
                                           .setType(DataTypeMediaTimeStamp)
                                           .setDefault(promeki::MediaTimeStamp())
                                           .setDescription("Timestamp when the library or device captured this data."));

                /// @brief FrameBridge publish timestamp for this frame.
                ///
                /// The moment the publisher placed this frame in the
                /// @ref FrameBridge output queue, expressed in the
                /// @ref ClockDomain::SystemMonotonic domain.  Set by
                /// @ref FrameBridgeMediaIO on every consumer-side
                /// frame so downstream stages can measure cross-process
                /// transport latency and correlate with other
                /// @ref MediaTimeStamp fields.
                PROMEKI_DECLARE_ID(FrameBridgeTimeStamp,
                                   VariantSpec()
                                           .setType(DataTypeMediaTimeStamp)
                                           .setDefault(promeki::MediaTimeStamp())
                                           .setDescription("FrameBridge publish timestamp (SystemMonotonic)."));

                /// @brief RTP timestamp from the packet header (uint32_t).
                ///
                /// The raw 32-bit RTP timestamp carried in the packet(s)
                /// that delivered this essence.  Clock rate is stream-defined
                /// (typically 90 kHz for video).
                PROMEKI_DECLARE_ID(RtpTimestamp, VariantSpec()
                                                         .setType(DataTypeUInt32)
                                                         .setDefault(uint32_t(0))
                                                         .setDescription("RTP timestamp from the packet header."));

                /// @brief Number of RTP packets that composed this essence (int32_t).
                PROMEKI_DECLARE_ID(RtpPacketCount,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Number of RTP packets that composed this essence."));

#if PROMEKI_ENABLE_PROAV
                /// @brief Per-payload audio event markers (@ref AudioMarkerList).
                ///
                /// Set by audio producers that need to annotate sample
                /// regions of the carried payload — most commonly
                /// network audio receivers (NDI, RTP, …) flagging
                /// synthesized silence, concealed packet loss, or
                /// discontinuities in the source timeline, but any
                /// stage that inspects or generates samples may stamp
                /// markers here.  Each entry locates a region in the
                /// owning @ref PcmAudioPayload by sample @c offset and
                /// @c length and tags it with an @ref AudioMarkerType.
                /// Empty (or absent) means the payload carries no
                /// noteworthy events.
                PROMEKI_DECLARE_ID(
                        AudioMarkers,
                        VariantSpec()
                                .setType(DataTypeAudioMarkerList)
                                .setDefault(AudioMarkerList())
                                .setDescription("Per-payload audio event markers (silence fills, "
                                                "concealed loss, discontinuities, …)."));
#endif

#if PROMEKI_ENABLE_NETWORK
                /// @brief PTP grandmaster clock identity (EUI-64).
                PROMEKI_DECLARE_ID(PtpGrandmasterId,
                                   VariantSpec()
                                           .setType(DataTypeEUI64)
                                           .setDefault(EUI64())
                                           .setDescription("PTP grandmaster clock identity (EUI-64)."));

                /// @brief PTP domain number (0-127).
                PROMEKI_DECLARE_ID(PtpDomainNumber, VariantSpec()
                                                            .setType(DataTypeUInt8)
                                                            .setDefault(uint8_t(0))
                                                            .setMax(uint8_t(127))
                                                            .setDescription("PTP domain number (0-127)."));
#endif

                /// @brief Number of times this frame was repeated due to underrun (int).
                PROMEKI_DECLARE_ID(FrameRepeated,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Number of times frame repeated due to underrun."));

                /// @brief Number of frames dropped immediately before this one (int).
                PROMEKI_DECLARE_ID(FrameDropped, VariantSpec()
                                                         .setType(DataTypeInt32)
                                                         .setDefault(int32_t(0))
                                                         .setMin(int32_t(0))
                                                         .setDescription("Number of frames dropped before this one."));

                /// @brief Number of input frames the FrameSync dropped between
                /// this output and the previous fresh emit (int32_t).
                ///
                /// Set by @ref FrameSync on every output frame.  The value is
                /// always 0 on a repeat output; any input frames that were
                /// discarded while the output was stuck on a repeat are
                /// accumulated and reported on the next fresh emit so a
                /// downstream consumer can pinpoint exactly where the drop
                /// occurred in the output timeline and how many source frames
                /// were skipped.
                PROMEKI_DECLARE_ID(
                        FrameSyncDrop,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription(
                                        "Input frames dropped between this FrameSync emit and the previous one."));

                /// @brief Position of this output within a FrameSync repeat
                /// sequence (int32_t).
                ///
                /// Set by @ref FrameSync on every output frame.  Zero on a
                /// fresh emit; 1, 2, 3, ... on successive repeats of the
                /// currently held frame.  Resets to 0 on the next fresh emit.
                PROMEKI_DECLARE_ID(
                        FrameSyncRepeat,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Position within a FrameSync repeat sequence (0 = fresh emit)."));

                /// @brief This frame arrived later than its scheduled time (bool).
                PROMEKI_DECLARE_ID(FrameLate, VariantSpec()
                                                      .setType(DataTypeBool)
                                                      .setDefault(false)
                                                      .setDescription("Frame arrived later than scheduled."));

                /// @brief This frame is a keyframe / intra frame (bool).
                PROMEKI_DECLARE_ID(FrameKeyframe, VariantSpec()
                                                          .setType(DataTypeBool)
                                                          .setDefault(false)
                                                          .setDescription("Frame is a keyframe / intra frame."));

                /// @brief Request the encoder to emit an IDR/keyframe for
                /// this frame (bool).  Set by upstream stages (e.g. after
                /// a recording pause/unpause) to signal that this frame is
                /// not temporally related to the previous one.
                PROMEKI_DECLARE_ID(ForceKeyframe,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Request encoder to emit an IDR for this frame."));

                /// @brief This frame's MediaDesc differs from the previously
                /// reported one (bool).
                PROMEKI_DECLARE_ID(MediaDescChanged,
                                   VariantSpec()
                                           .setType(DataTypeBool)
                                           .setDefault(false)
                                           .setDescription("Frame's MediaDesc differs from previously reported."));

                // ============================================================
                // Session / capture environment
                //
                // These keys describe the environment a container was
                // written in.  They're populated automatically by
                // backends whose files are meant for debugging or
                // forensic replay (e.g. PMDF), but any writer is free
                // to stamp them.
                // ============================================================

                /// @brief Runtime hostname of the machine that produced the file.
                PROMEKI_DECLARE_ID(SessionHostname,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Runtime hostname of the machine that produced the file."));

                /// @brief Process ID of the writer at capture time.
                PROMEKI_DECLARE_ID(SessionProcessId, VariantSpec()
                                                             .setType(DataTypeInt64)
                                                             .setDefault(int64_t(0))
                                                             .setDescription("Writer process ID at capture time."));

                /// @brief Full libpromeki build identity (name, version,
                /// repo ident, type, date/time, build hostname).  Written
                /// by debug-oriented sinks so readers can locate the
                /// exact library revision that produced a file.
                PROMEKI_DECLARE_ID(LibraryBuildInfo,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("libpromeki build identity (version, repo, date, host)."));

                /// @brief Platform / compiler / C++ standard the
                /// library was compiled against.
                PROMEKI_DECLARE_ID(LibraryPlatform,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Platform / compiler / C++ standard of the writer."));

                /// @brief Library feature flags enabled at build time
                /// (e.g. @c "NETWORK PROAV MUSIC PNG JPEG AUDIO CSC").
                PROMEKI_DECLARE_ID(LibraryFeatures,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Library feature flags enabled at build time."));

                // ============================================================
                // DPX file info
                // ============================================================

                /// @brief Original source filename (from previous save).
                PROMEKI_DECLARE_ID(FileOrigName, VariantSpec()
                                                         .setType(DataTypeString)
                                                         .setDefault(String())
                                                         .setDescription("Original source filename."));

                /// @brief Project name.
                PROMEKI_DECLARE_ID(Project, VariantSpec()
                                                    .setType(DataTypeString)
                                                    .setDefault(String())
                                                    .setDescription("Project name."));

                /// @brief Reel or input device name.
                PROMEKI_DECLARE_ID(Reel, VariantSpec()
                                                 .setType(DataTypeString)
                                                 .setDefault(String())
                                                 .setDescription("Reel or input device name."));

                // ============================================================
                // DPX film info
                // ============================================================

                /// @brief Film manufacturer ID code (2 chars).
                PROMEKI_DECLARE_ID(FilmMfgID, VariantSpec()
                                                      .setType(DataTypeString)
                                                      .setDefault(String())
                                                      .setDescription("Film manufacturer ID code (2 chars)."));

                /// @brief Film type (2 chars).
                PROMEKI_DECLARE_ID(FilmType, VariantSpec()
                                                     .setType(DataTypeString)
                                                     .setDefault(String())
                                                     .setDescription("Film type (2 chars)."));

                /// @brief Film offset in perfs (2 chars).
                PROMEKI_DECLARE_ID(FilmOffset, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("Film offset in perfs (2 chars)."));

                /// @brief Film prefix (6 chars).
                PROMEKI_DECLARE_ID(FilmPrefix, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("Film prefix (6 chars)."));

                /// @brief Film count (4 chars).
                PROMEKI_DECLARE_ID(FilmCount, VariantSpec()
                                                      .setType(DataTypeString)
                                                      .setDefault(String())
                                                      .setDescription("Film count (4 chars)."));

                /// @brief Film format (e.g. "Academy").
                PROMEKI_DECLARE_ID(FilmFormat, VariantSpec()
                                                       .setType(DataTypeString)
                                                       .setDefault(String())
                                                       .setDescription("Film format (e.g. Academy)."));

                /// @brief Sequence position (frame number in sequence).
                PROMEKI_DECLARE_ID(FilmSeqPos,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Sequence position (frame number in sequence)."));

                /// @brief Sequence length (total frames in sequence).
                PROMEKI_DECLARE_ID(FilmSeqLen, VariantSpec()
                                                       .setType(DataTypeInt32)
                                                       .setDefault(int32_t(0))
                                                       .setMin(int32_t(0))
                                                       .setDescription("Sequence length (total frames in sequence)."));

                /// @brief Held count (1 = default, >1 = repeated frame).
                PROMEKI_DECLARE_ID(FilmHoldCount,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(1))
                                           .setMin(int32_t(1))
                                           .setDescription("Held count (1 = default, >1 = repeated frame)."));

                /// @brief Film shutter angle in degrees.
                PROMEKI_DECLARE_ID(FilmShutter, VariantSpec()
                                                        .setType(DataTypeDouble)
                                                        .setDefault(0.0)
                                                        .setMin(0.0)
                                                        .setMax(360.0)
                                                        .setDescription("Film shutter angle in degrees."));

                /// @brief Film frame identification (e.g. keycode).
                PROMEKI_DECLARE_ID(FilmFrameID, VariantSpec()
                                                        .setType(DataTypeString)
                                                        .setDefault(String())
                                                        .setDescription("Film frame identification (e.g. keycode)."));

                /// @brief Film slate information.
                PROMEKI_DECLARE_ID(FilmSlate, VariantSpec()
                                                      .setType(DataTypeString)
                                                      .setDefault(String())
                                                      .setDescription("Film slate information."));

                // ============================================================
                // DPX TV info
                // ============================================================

                /// @brief Field number within an interlaced frame (0 or 1).
                PROMEKI_DECLARE_ID(FieldID,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setRange(int32_t(0), int32_t(1))
                                           .setDescription("Field number within an interlaced frame (0 or 1)."));

                // ============================================================
                // DPX image element info
                // ============================================================

                /// @brief SMPTE 268M transfer characteristic code.
                PROMEKI_DECLARE_ID(TransferCharacteristic,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("SMPTE 268M transfer characteristic code."));

                /// @brief SMPTE 268M colorimetric specification code.
                PROMEKI_DECLARE_ID(Colorimetric,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("SMPTE 268M colorimetric specification code."));

                /// @brief Image orientation code (0 = left-to-right, top-to-bottom).
                PROMEKI_DECLARE_ID(
                        Orientation,
                        VariantSpec()
                                .setType(DataTypeInt32)
                                .setDefault(int32_t(0))
                                .setMin(int32_t(0))
                                .setDescription("Image orientation code (0 = left-to-right, top-to-bottom)."));

                // ============================================================
                // HDR metadata (SMPTE ST 2086 / CTA-861.3)
                // ============================================================

                /// @brief Mastering display color volume (SMPTE ST 2086).
                PROMEKI_DECLARE_ID(MasteringDisplay,
                                   VariantSpec()
                                           .setType(DataTypeMasteringDisplay)
                                           .setDescription("Mastering display color volume (SMPTE ST 2086)."));

                /// @brief Content light level information (CTA-861.3).
                PROMEKI_DECLARE_ID(ContentLightLevel,
                                   VariantSpec()
                                           .setType(DataTypeContentLightLevel)
                                           .setDescription("Content light level info (MaxCLL / MaxFALL)."));

                // ============================================================
                // Codec VUI / color description (ISO/IEC 23091-4 / ITU-T H.273)
                // ============================================================
                //
                // These mirror the @ref MediaConfig encoder-side keys, but
                // on output: a @ref VideoDecoder parses the bitstream's VUI
                // (H.264/HEVC) or color description (AV1) and stamps the
                // recovered values here on every decoded @ref Image.  They
                // are distinct from the legacy DPX @c TransferCharacteristic
                // / @c Colorimetric keys above — those carry SMPTE 268M
                // codepoints from DPX files, while these carry H.273
                // codepoints from a compressed bitstream.

                /// @brief Color primaries observed in the decoded bitstream.
                PROMEKI_DECLARE_ID(VideoColorPrimaries,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::ColorPrimaries::Unspecified)
                                           .setEnumType(promeki::ColorPrimaries::Type)
                                           .setDescription("VUI color primaries (ISO/IEC 23091-4)."));

                /// @brief Transfer characteristics observed in the decoded bitstream.
                PROMEKI_DECLARE_ID(VideoTransferCharacteristics,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::TransferCharacteristics::Unspecified)
                                           .setEnumType(promeki::TransferCharacteristics::Type)
                                           .setDescription("VUI transfer characteristics (ISO/IEC 23091-4)."));

                /// @brief Matrix coefficients observed in the decoded bitstream.
                PROMEKI_DECLARE_ID(VideoMatrixCoefficients,
                                   VariantSpec()
                                           .setType(DataTypeEnum)
                                           .setDefault(promeki::MatrixCoefficients::Unspecified)
                                           .setEnumType(promeki::MatrixCoefficients::Type)
                                           .setDescription("VUI matrix coefficients (ISO/IEC 23091-4)."));

                /// @brief Value range observed in the decoded bitstream.
                PROMEKI_DECLARE_ID(VideoRange, VariantSpec()
                                                       .setType(DataTypeEnum)
                                                       .setDefault(promeki::VideoRange::Unknown)
                                                       .setEnumType(promeki::VideoRange::Type)
                                                       .setDescription("VUI video range (Unknown / Limited / Full)."));

                /// @brief Scan mode observed in the decoded bitstream, or
                /// supplied as a per-frame encoder override.
                ///
                /// On decode, an NVDEC picture's @c progressive_frame and
                /// @c top_field_first bits are mapped here:
                /// @c progressive_frame=1 → @c Progressive;
                /// @c progressive_frame=0 with @c top_field_first=1 →
                /// @c InterlacedEvenFirst; with @c top_field_first=0 →
                /// @c InterlacedOddFirst.  On encode, this key overrides
                /// the session-level @ref MediaConfig::VideoScanMode for
                /// a single picture, which lets a stream carry mixed
                /// scan modes when the codec path supports it.
                PROMEKI_DECLARE_ID(VideoScanMode, VariantSpec()
                                                          .setType(DataTypeEnum)
                                                          .setDefault(promeki::VideoScanMode::Unknown)
                                                          .setEnumType(promeki::VideoScanMode::Type)
                                                          .setDescription("Raster scan mode "
                                                                          "(Progressive / Interlaced*)."));

                // ============================================================
                // Encoder per-frame statistics
                //
                // These keys are populated by a @ref VideoEncoder on each
                // emitted @ref CompressedVideoPayload.  They describe how
                // the encoder coded a specific frame and are useful for
                // rate-control tuning, quality analytics, and diagnosing
                // bitrate / latency anomalies.  A decoder will typically
                // not repopulate them when re-decoding a stream.
                // ============================================================

                /// @brief Average quantization parameter of the encoded
                /// frame.  Codec-dependent range (H.264 / HEVC: 0-51,
                /// AV1: 0-255).  Lower is higher quality / higher
                /// bitrate.
                PROMEKI_DECLARE_ID(CodecFrameAvgQP, VariantSpec()
                                                            .setType(DataTypeInt32)
                                                            .setDefault(int32_t(0))
                                                            .setMin(int32_t(0))
                                                            .setDescription("Average QP of the encoded frame."));

                /// @brief Coding complexity of the encoded frame — Sum of
                /// Absolute Transformed Differences over the whole
                /// picture as reported by the encoder.
                PROMEKI_DECLARE_ID(CodecFrameSatd,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Frame SATD (Sum of Absolute Transformed Differences)."));

                /// @brief Index of this frame in encode order
                /// (encoder-internal sequence).
                ///
                /// Encoders that reorder frames (B-pyramid, HEVC RA) emit
                /// frames in encode order while the stream is consumed
                /// in display order.  Pair with
                /// @ref CodecDisplayOrderIdx to measure reorder depth.
                PROMEKI_DECLARE_ID(CodecEncodeOrderIdx, VariantSpec()
                                                                .setType(DataTypeUInt32)
                                                                .setDefault(uint32_t(0))
                                                                .setDescription("Frame index in encode order."));

                /// @brief Index of this frame in display order.
                PROMEKI_DECLARE_ID(CodecDisplayOrderIdx, VariantSpec()
                                                                 .setType(DataTypeUInt32)
                                                                 .setDefault(uint32_t(0))
                                                                 .setDescription("Frame index in display order."));

                /// @brief Temporal scalability layer ID of this frame
                /// (0 = base).
                ///
                /// For temporally-scalable streams (H.264 SVC, HEVC
                /// TSVC, AV1 temporal layers), higher layers can be
                /// dropped by a downstream selector to reduce bitrate
                /// or frame rate without touching the base layer.
                PROMEKI_DECLARE_ID(CodecTemporalId,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Temporal scalability layer ID (0 = base layer)."));

                /// @brief Offset from the most recent keyframe, in
                /// display order.
                ///
                /// Zero on a keyframe; 1, 2, 3, ... on subsequent frames
                /// until the next keyframe resets the count.  Useful
                /// for diagnosing GOP structure and rate-control
                /// behaviour across a GOP.
                PROMEKI_DECLARE_ID(CodecGopPosition,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Offset from last keyframe (0 = keyframe)."));

                /// @brief Number of intra-coded blocks in the encoded
                /// frame.
                ///
                /// The block unit is codec-defined: macroblocks (H.264),
                /// coding tree blocks (HEVC), superblocks (AV1).
                /// Populated only when the encoder is configured to
                /// report rate-control statistics.
                PROMEKI_DECLARE_ID(CodecIntraBlockCount,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Count of intra-coded blocks (codec-specific unit)."));

                /// @brief Number of inter-coded blocks in the encoded
                /// frame.
                ///
                /// The block unit matches @ref CodecIntraBlockCount
                /// (MB / CTB / SB).  Populated only when the encoder is
                /// configured to report rate-control statistics.
                PROMEKI_DECLARE_ID(CodecInterBlockCount,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setMin(int32_t(0))
                                           .setDescription("Count of inter-coded blocks (codec-specific unit)."));

                /// @brief Average motion vector X component for the
                /// encoded frame.
                ///
                /// Units are codec-defined (typically quarter-pixel for
                /// H.264 / HEVC).  Populated only when the encoder is
                /// configured to report rate-control statistics.
                PROMEKI_DECLARE_ID(CodecAvgMotionVectorX,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Average motion vector X (codec-defined units)."));

                /// @brief Average motion vector Y component for the
                /// encoded frame.
                PROMEKI_DECLARE_ID(CodecAvgMotionVectorY,
                                   VariantSpec()
                                           .setType(DataTypeInt32)
                                           .setDefault(int32_t(0))
                                           .setDescription("Average motion vector Y (codec-defined units)."));

                /// @brief Out-of-band codec parameter sets describing this
                /// stream, as a raw Annex-B byte sequence.
                ///
                /// Holds the start-code-prefixed NAL units a decoder needs
                /// to initialize before any slice data:
                /// - H.264: SPS + PPS (RFC 6184 sprop-parameter-sets)
                /// - HEVC : VPS + SPS + PPS (RFC 7798 sprop-vps/sps/pps)
                /// - AV1  : OBU_SEQUENCE_HEADER
                ///
                /// A @ref VideoEncoder MediaIO populates this on every
                /// emitted @ref CompressedVideoPayload (the contents are
                /// stable across the GOP, copying them per-frame is
                /// cheap), giving downstream MediaIO stages a config-free
                /// channel for parameter sets.  The transport sinks use it
                /// to seed out-of-band signaling — @ref RtpMediaIO embeds
                /// the values in the SDP @c sprop-* fmtp parameters before
                /// the first RTP packet flies, so receivers reading the
                /// SDP file can probe successfully without waiting for the
                /// first IDR to arrive.
                ///
                /// The byte stream is identical in shape to what
                /// @ref H264Bitstream / @ref HevcBitstream Annex-B walkers
                /// expect, so consumers can run a single forEachAnnexBNal
                /// over it and classify each NAL by type.
                PROMEKI_DECLARE_ID(CodecParameterSets,
                                   VariantSpec()
                                           .setType(DataTypeString)
                                           .setDefault(String())
                                           .setDescription("Out-of-band codec parameter sets "
                                                           "(Annex-B SPS/PPS/VPS or AV1 sequence header)."));

                // ============================================================
                // Frontend layout hints
                //
                // Generic per-stage layout hints for graphical editors
                // (the promeki-pipeline demo persists Vue Flow node
                // positions through these).  Stored on
                // @ref MediaPipelineConfig::Stage::metadata so they
                // round-trip with the rest of the config.  The wire
                // names use the @c "Frontend." prefix to namespace UI
                // hints away from media metadata; the C++ identifiers
                // drop the dot because identifiers cannot contain one.
                // ============================================================

                /// @brief X coordinate (pixels) of this stage in a graphical editor.
                static inline const ID FrontendX = declareID(
                        "Frontend.X", VariantSpec()
                                              .setType(DataTypeDouble)
                                              .setDefault(0.0)
                                              .setDescription("Frontend X coordinate (pixels) for graphical editors."));

                /// @brief Y coordinate (pixels) of this stage in a graphical editor.
                static inline const ID FrontendY = declareID(
                        "Frontend.Y", VariantSpec()
                                              .setType(DataTypeDouble)
                                              .setDefault(0.0)
                                              .setDescription("Frontend Y coordinate (pixels) for graphical editors."));

                // ============================================================
                // Methods
                // ============================================================

                /**
                 * @brief Converts a metadata ID to its string name.
                 * @param id The metadata ID.
                 * @return The name string.
                 */
                static String idToString(ID id) { return id.name(); }

                /**
                 * @brief Converts a string name to a metadata ID.
                 * @param val The name string.
                 * @return The corresponding ID.
                 */
                static ID stringToID(const String &val) { return ID(val); }

                /**
                 * @brief Deserializes a Metadata object from a JSON object.
                 * @param json The source JSON object.
                 * @param err  Optional error output.
                 * @return The deserialized Metadata.
                 */
                static Metadata fromJson(const JsonObject &json, Error *err = nullptr);

                /**
                 * @brief Serializes this Metadata to a JSON-encoded string.
                 *
                 * Equivalent to @c toJson().toString(indent) — a thin
                 * convenience wrapper that lets a Metadata round-trip
                 * through a plain @ref String without the caller having
                 * to know about @ref JsonObject.  Used by
                 * @ref AncTranslateConfig and any other config object
                 * that needs a single-call serialization for log
                 * messages, command-line tools, or
                 * @ref MediaConfig string forms.
                 *
                 * @param indent JSON indentation (0 = compact, default).
                 * @return The encoded JSON document.
                 */
                String toString(unsigned int indent = 0) const;

                /**
                 * @brief Parses a JSON-encoded Metadata document.
                 *
                 * Inverse of @ref toString.  Equivalent to
                 * @c fromJson(JsonObject::parse(str)) but propagates
                 * parse-time errors (malformed JSON) via @p err in
                 * addition to the spec-validation errors already
                 * surfaced by @ref fromJson.
                 *
                 * @param str The JSON document to parse.
                 * @param err Optional error output: @c Ok on full
                 *            success, @c Error::Invalid when the JSON
                 *            failed to parse or one or more keys
                 *            rejected spec coercion.
                 * @return The reconstructed Metadata; default-constructed
                 *         when @p str fails to parse as a JSON object.
                 */
                static Metadata fromString(const String &str, Error *err = nullptr);

                /** @brief Constructs an empty Metadata object. */
                Metadata() = default;

                /**
                 * @brief Returns a human-readable dump of all metadata entries.
                 * @return A StringList with one entry per metadata key-value pair.
                 */
                StringList dump() const;

                /**
                 * @brief Returns true if both Metadata objects contain the same entries.
                 * @param other The Metadata to compare against.
                 * @return true if equal.
                 */
                bool operator==(const Metadata &other) const;

                /**
                 * @brief Fills in standard defaults used for MediaIO writes.
                 *
                 * Each of the fields below is populated only if the
                 * corresponding key is not already present (via
                 * @ref VariantDatabase::setIfMissing), so caller-supplied
                 * values are always preserved.  This helper is intended to
                 * be called by @ref MediaIO on the container metadata just
                 * before dispatching a writer open; it is @em not called
                 * automatically by any other part of the Metadata class.
                 *
                 * Defaults populated:
                 *
                 *  - `Date` — current wall-clock UTC date, formatted as
                 *    @c "YYYY-MM-DD".
                 *  - `OriginationDateTime` — current wall-clock UTC
                 *    timestamp, formatted as @c "YYYY-MM-DDTHH:MM:SS"
                 *    (BWF-friendly ISO 8601).
                 *  - `Software` — the Application name if
                 *    @ref Application::appName is non-empty, otherwise
                 *    @c "libpromeki (https://howardlogic.com)".
                 *  - `Originator` — always
                 *    @c "libpromeki howardlogic.com".  BWF caps this
                 *    field at 32 characters; this value fits and acts
                 *    as a persistent libpromeki signature.
                 *  - `OriginatorReference` — a fresh UUID v7
                 *    (time-sortable) as a string.
                 *  - @ref UMID — a fresh Extended SMPTE 330M UMID with
                 *    a random material number and a @c "MEKI"
                 *    organization tag in the source pack.
                 */
                void applyMediaIOWriteDefaults();
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
