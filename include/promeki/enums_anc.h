/**
 * @file      enums_anc.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Ancillary data (SMPTE 291) handling enums.
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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncCategory", "Ancillary Data Category", 0,
                                           {"Unknown", 0, "Unknown"}, {"Captions", 1, "Closed Captions"},
                                           {"Timecode", 2, "Timecode (ATC LTC / VITC)"},
                                           {"Splice", 3, "Splice Markers (SCTE-104 / 35)"},
                                           {"Aspect", 4, "Aspect / AFD"}, {"Hdr", 5, "HDR Metadata"},
                                           {"AudioMetadata", 6, "Audio Metadata"},
                                           {"Display", 7, "Display Hints"},
                                           {"Geolocation", 8, "Geolocation (MISB KLV)"},
                                           {"PayloadId", 9, "Payload Identifier (SMPTE ST 352)"},
                                           {"UserDefined", 10, "User Defined"}, {"Subtitles", 11, "Subtitles (RDD 8 / OP-47)"},
                                           {"Klv", 12, "KLV Metadata (SMPTE ST 336)"}, {"Sei", 13, "SEI User Data"},
                                           {"Vbi", 14, "VBI / Line-21 (ST 2031)"},
                                           {"Control", 15, "Control / Management"}); // default: Unknown

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncTransport", "Ancillary Data Transport", 0,
                                           {"Invalid", 0, "Invalid"}, {"St291", 1, "SMPTE ST 291 Packet"},
                                           {"NdiXml", 2, "NDI Metadata (XML)"},
                                           {"RtmpAmf", 3, "RTMP AMF0 Script Tag"},
                                           {"HdmiInfoFrame", 4, "HDMI InfoFrame"},
                                           {"MpegTsPrivate", 5, "MPEG-TS Private Section"},
                                           {"HlsSei", 6, "HLS SEI User Data"}); // default: Invalid

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncTransmissionModel", "ST 2110-40 Transmission Model", 0,
                                           {"Unsignalled", 0, "Unsignalled"},
                                           {"Lltm", 1, "Low-Latency Transmission Model (LLTM)"},
                                           {"Ctm", 2, "Compatible Transmission Model (CTM)"}); // default: Unsignalled

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncFidelity", "Translator Output Fidelity", 0,
                                           {"Default", 0, "Default"}, {"Strict", 1, "Strict (Minimal Fields)"},
                                           {"Full", 2, "Full (All Optional Fields)"}); // default: Default

                using TypedEnum<AncFidelity>::TypedEnum;

                static const AncFidelity Default;
                static const AncFidelity Strict;
                static const AncFidelity Full;
};

inline const AncFidelity AncFidelity::Default{0};
inline const AncFidelity AncFidelity::Strict{1};
inline const AncFidelity AncFidelity::Full{2};

/**
 * @brief Well-known Enum classifying the severity of one @ref AncDetails
 *        diagnostic issue.
 *
 * Stamped on each @c AncDetails::Issue raised while fully decoding an
 * @ref AncPacket for analysis.  Lets an inspector colour-code or filter
 * the issue list ("show me only the errors") without string-matching the
 * message text.
 *
 * - @c Info    — Purely informational observation about the packet that
 *                is not a defect (e.g. "bar data present").  Never
 *                indicates a problem.
 * - @c Warning — The packet decodes, but something diverges from the
 *                governing standard or is otherwise suspect (e.g. a
 *                non-conformant Data Count the decoder Postel-tolerated,
 *                a reserved bit set, a rate hint that had to be guessed).
 * - @c Error   — The packet could not be fully decoded: a truncated or
 *                structurally invalid payload, a checksum mismatch under
 *                strict validation, or missing required context.
 */
class AncDetailSeverity : public TypedEnum<AncDetailSeverity> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncDetailSeverity", "ANC Detail Severity", 0,
                                           {"Info", 0, "Info"}, {"Warning", 1, "Warning"},
                                           {"Error", 2, "Error"}); // default: Info

                using TypedEnum<AncDetailSeverity>::TypedEnum;

                static const AncDetailSeverity Info;
                static const AncDetailSeverity Warning;
                static const AncDetailSeverity Error;
};

inline const AncDetailSeverity AncDetailSeverity::Info{0};
inline const AncDetailSeverity AncDetailSeverity::Warning{1};
inline const AncDetailSeverity AncDetailSeverity::Error{2};

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncChecksumPolicy", "Checksum Policy", 0,
                                           {"PreserveOrRecompute", 0, "Preserve or Recompute"},
                                           {"AlwaysRecompute", 1, "Always Recompute"},
                                           {"StrictValidate", 2, "Strict Validate"}); // default: PreserveOrRecompute

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
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("AncOnUnsupported", "On Unsupported Input", 1,
                                           {"Skip", 0, "Skip"}, {"BestEffort", 1, "Best Effort"},
                                           {"Fail", 2, "Fail"}); // default: BestEffort

                using TypedEnum<AncOnUnsupported>::TypedEnum;

                static const AncOnUnsupported Skip;
                static const AncOnUnsupported BestEffort;
                static const AncOnUnsupported Fail;
};

inline const AncOnUnsupported AncOnUnsupported::Skip{0};
inline const AncOnUnsupported AncOnUnsupported::BestEffort{1};
inline const AncOnUnsupported AncOnUnsupported::Fail{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
