/**
 * @file      enums_mediaio.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * MediaIO direction / payload-kind / container enums.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/namespace.h>
#include <promeki/enum.h>

PROMEKI_NAMESPACE_BEGIN

/** @addtogroup wellknownenums */
/** @{ */

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MediaIOOpenMode", "Open Mode", 0,
                                           {"Read", 0, "Read"}, {"Write", 1, "Write"}); // default: Read

                using TypedEnum<MediaIOOpenMode>::TypedEnum;

                static const MediaIOOpenMode Read;
                static const MediaIOOpenMode Write;
};

inline const MediaIOOpenMode MediaIOOpenMode::Read{0};
inline const MediaIOOpenMode MediaIOOpenMode::Write{1};

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MediaIODirection", "MediaIO Direction", 0,
                                           {"Source", 0, "Source"}, {"Sink", 1, "Sink"},
                                           {"Transform", 2, "Transform"}); // default: Source

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MediaPayloadKind", "Payload Kind", 0,
                                           {"Video", 0, "Video"}, {"Audio", 1, "Audio"},
                                           {"Metadata", 2, "Timed Metadata"}, {"Subtitle", 3, "Subtitle / Caption"},
                                           {"AncillaryData", 4, "Ancillary Data"},
                                           {"Custom", 5, "Custom"}); // default: Video

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("FrameType", "Frame Type", 0,
                                           {"Unknown", 0, "Unknown"}, {"I", 1, "Intra (I)"},
                                           {"P", 2, "Predicted (P)"}, {"B", 3, "Bidirectional (B)"},
                                           {"IDR", 4, "Instantaneous Decoder Refresh (IDR)"},
                                           {"BRef", 5, "Referenced B-Frame"}); // default: Unknown

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("QuickTimeLayout", "QuickTime Layout", 1,
                                           {"Classic", 0, "Classic (Single moov)"},
                                           {"Fragmented", 1, "Fragmented (moof / mdat)"}); // default: Fragmented

                using TypedEnum<QuickTimeLayout>::TypedEnum;

                static const QuickTimeLayout Classic;
                static const QuickTimeLayout Fragmented;
};

inline const QuickTimeLayout QuickTimeLayout::Classic{0};
inline const QuickTimeLayout QuickTimeLayout::Fragmented{1};

/**
 * @brief Well-known Enum type for the MPEG-TS AAC framing mode.
 *
 * Used as the value type for the @ref MediaConfig::MpegTsAacFraming
 * config key.  Drives the @c stream_type written into the PMT for the
 * audio elementary stream:
 *
 *  - @c Adts maps to @c stream_type @c 0x0F (ISO/IEC 13818-7).  The
 *    encoded bytes are passed verbatim — ADTS sync words and all —
 *    inside each PES packet.
 *  - @c Latm maps to @c stream_type @c 0x11 (ISO/IEC 14496-3
 *    LOAS / LATM).  The PMT signals LATM but the library's AAC
 *    encoder still emits ADTS today, so the on-wire bytes are
 *    unchanged.  Full ADTS → LATM transcoding is a follow-up.
 */
class MpegTsAacFraming : public TypedEnum<MpegTsAacFraming> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MpegTsAacFraming", "MPEG-TS AAC Framing", 0,
                                           {"Adts", 0, "ADTS (stream_type 0x0F)"},
                                           {"Latm", 1, "LATM (stream_type 0x11)"}); // default: Adts

                using TypedEnum<MpegTsAacFraming>::TypedEnum;

                static const MpegTsAacFraming Adts;
                static const MpegTsAacFraming Latm;
};

inline const MpegTsAacFraming MpegTsAacFraming::Adts{0};
inline const MpegTsAacFraming MpegTsAacFraming::Latm{1};

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("ImgSeqPathMode", "Image Sequence Path Mode", 0,
                                           {"Relative", 0, "Relative Path"},
                                           {"Absolute", 1, "Absolute Path"}); // default: Relative

                using TypedEnum<ImgSeqPathMode>::TypedEnum;

                static const ImgSeqPathMode Relative;
                static const ImgSeqPathMode Absolute;
};

inline const ImgSeqPathMode ImgSeqPathMode::Relative{0};
inline const ImgSeqPathMode ImgSeqPathMode::Absolute{1};

/**
 * @brief Well-known Enum type for the SRT video pacing mode.
 *
 * Mirrors @ref RtmpVideoPacing so the SRT sink can rate-limit
 * upstream-produced frames against a wall (or external) clock
 * before they hit the SRT message path.
 *
 * - @c Internal — bind a fresh wall clock at @c open() and pace
 *   each video frame against it.  Default — matches RTMP/RTP.
 * - @c External — leave the gate unbound at @c open(); arm only
 *   when a clock arrives via @c executeCmd(SetClock).
 * - @c None     — never pace; ship every video frame as soon as
 *   the strand picks it up.
 */
class SrtVideoPacing : public TypedEnum<SrtVideoPacing> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SrtVideoPacing", "SRT Video Pacing", 0,
                                           {"Internal", 0, "Internal Wall Clock"},
                                           {"External", 1, "External Clock"},
                                           {"None", 2, "None (Unpaced)"}); // default: Internal

                using TypedEnum<SrtVideoPacing>::TypedEnum;

                static const SrtVideoPacing Internal;
                static const SrtVideoPacing External;
                static const SrtVideoPacing None;
};

inline const SrtVideoPacing SrtVideoPacing::Internal{0};
inline const SrtVideoPacing SrtVideoPacing::External{1};
inline const SrtVideoPacing SrtVideoPacing::None{2};

/**
 * @brief Well-known Enum type for the SRT connection role.
 *
 * Drives @ref SrtSocketTransport's @c Mode in @ref SrtMediaIO.  The
 * three values map 1:1 onto libsrt's SRT_TRANSTYPE roles:
 *
 *  - @c Caller — actively dial out to a remote SRT listener.
 *  - @c Listener — bind locally and accept a single inbound caller.
 *  - @c Rendezvous — peer-to-peer simultaneous open against a
 *    prearranged peer (NAT traversal scenario).
 */
class SrtMode : public TypedEnum<SrtMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("SrtMode", "SRT Mode", 0,
                                           {"Caller", 0, "Caller (dial out)"},
                                           {"Listener", 1, "Listener (accept one peer)"},
                                           {"Rendezvous", 2, "Rendezvous (peer-to-peer)"}); // default: Caller

                using TypedEnum<SrtMode>::TypedEnum;

                static const SrtMode Caller;
                static const SrtMode Listener;
                static const SrtMode Rendezvous;
};

inline const SrtMode SrtMode::Caller{0};
inline const SrtMode SrtMode::Listener{1};
inline const SrtMode SrtMode::Rendezvous{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
