/**
 * @file      enums_rtp.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RTP sender pacing / clock / timestamp signalling enums.
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
 * @brief Well-known Enum type for RTP sender pacing mode.
 *
 * Selects how the RTP sink stages space packets out over time.
 * Drives the @ref MediaConfig::RtpPacingMode config key and the
 * equivalent runtime path inside @c RtpMediaIO.
 *
 * - @c Auto     — pick the best available mechanism at open time.
 *                 On Linux this resolves to @c KernelFq; on other
 *                 platforms it falls back to @c Userspace.  This
 *                 is the default — callers only need to set an
 *                 explicit mode when they want to override the
 *                 platform default (e.g. @c None for loopback /
 *                 LAN tests, @c TxTime for ST 2110-21 deployments).
 * - @c None     — burst all packets at once.  Appropriate only
 *                 for loopback / LAN tests or when the downstream
 *                 network is guaranteed to absorb the burst.
 * - @c Userspace — pace by sleeping between sends (the per-stream
 *                 TX thread + @c Cadence helper).  Works
 *                 everywhere without kernel configuration but ties
 *                 up the TX thread during the pacing window.
 * - @c KernelFq — push the rate to @c SO_MAX_PACING_RATE and let
 *                 the @c fq qdisc space the packets with zero
 *                 per-packet CPU cost.
 * - @c TxTime   — per-packet @c SCM_TXTIME deadlines via the ETF
 *                 qdisc.  Only enabled when the transport and
 *                 kernel both support it; falls back to @c KernelFq
 *                 otherwise.  Used for ST 2110-21-grade pacing.
 */
class RtpPacingMode : public TypedEnum<RtpPacingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtpPacingMode", "RTP Sender Pacing Mode", 4,
                                           {"None", 0, "None (Burst)"}, {"Userspace", 1, "Userspace (Software Pacing)"},
                                           {"KernelFq", 2, "Kernel Fair Queue (fq qdisc)"},
                                           {"TxTime", 3, "TX Time (ETF / SCM_TXTIME)"}, {"Auto", 4, "Automatic"}); // default: Auto

                using TypedEnum<RtpPacingMode>::TypedEnum;

                static const RtpPacingMode None;
                static const RtpPacingMode Userspace;
                static const RtpPacingMode KernelFq;
                static const RtpPacingMode TxTime;
                static const RtpPacingMode Auto;
};

inline const RtpPacingMode RtpPacingMode::None{0};
inline const RtpPacingMode RtpPacingMode::Userspace{1};
inline const RtpPacingMode RtpPacingMode::KernelFq{2};
inline const RtpPacingMode RtpPacingMode::TxTime{3};
inline const RtpPacingMode RtpPacingMode::Auto{4};

/**
 * @brief Well-known Enum type for the SDP @c ts-refclk source.
 *
 * Selects which clock-reference attribute the writer emits in its
 * SDP and seeds onto every active stream, per RFC 7273 / SMPTE
 * ST 2110-10:
 *
 * - @c Auto     — emit @c localmac for the autodetected primary
 *                 interface MAC; if @ref MediaConfig::RtpPtpGrandmaster
 *                 is non-null, upgrade to @c Ptp automatically.  This
 *                 is the default.
 * - @c LocalMac — force @c ts-refclk:localmac=&lt;mac&gt;; use
 *                 @ref MediaConfig::RtpRefClockLocalMac to override
 *                 the autodetected MAC.
 * - @c Ptp      — emit
 *                 @c ts-refclk:ptp=&lt;profile&gt;:&lt;gmid&gt;:&lt;domain&gt;
 *                 from @ref MediaConfig::RtpPtpProfile,
 *                 @ref MediaConfig::RtpPtpGrandmaster, and
 *                 @ref MediaConfig::RtpPtpDomain.  Required for
 *                 SMPTE ST 2110 deployments.
 * - @c None     — suppress @c ts-refclk emission; receivers fall
 *                 back to "trust the SR pair" tracking.
 */
class RtpRefClockMode : public TypedEnum<RtpRefClockMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtpRefClockMode", "SDP Reference Clock (ts-refclk) Mode", 0,
                                           {"Auto", 0, "Automatic"}, {"LocalMac", 1, "Local MAC"},
                                           {"Ptp", 2, "PTP (Precision Time Protocol)"}, {"None", 3, "None"}); // default: Auto

                using TypedEnum<RtpRefClockMode>::TypedEnum;

                static const RtpRefClockMode Auto;
                static const RtpRefClockMode LocalMac;
                static const RtpRefClockMode Ptp;
                static const RtpRefClockMode None;
};

inline const RtpRefClockMode RtpRefClockMode::Auto{0};
inline const RtpRefClockMode RtpRefClockMode::LocalMac{1};
inline const RtpRefClockMode RtpRefClockMode::Ptp{2};
inline const RtpRefClockMode RtpRefClockMode::None{3};

/**
 * @brief Well-known Enum type for the RFC 7273 @c mediaclk SDP attribute.
 *
 * RFC 7273 §5 distinguishes two ways a sender signals how its media
 * clock relates to the reference clock identified by @c ts-refclk:
 *
 * - @c mediaclk:direct=&lt;offset&gt; — the media clock is locked to
 *   the reference clock with a fixed RTP-timestamp offset.  A
 *   receiver can compute the wallclock instant of every sample from
 *   the on-wire RTP-TS and the SDP offset alone.  This is the
 *   default for synchronous capture paths (SDI ingest, AES67 audio
 *   capture, anywhere the wire format is sample-locked to the
 *   reference grid).
 *
 * - @c mediaclk:sender — the media clock is asynchronous to the
 *   reference clock.  The receiver must use RTCP Sender Reports to
 *   recover the sender's clock; the @c ts-refclk identifies the
 *   sender's reference frame but does not anchor the media clock to
 *   it.  Right for sources whose framing rate floats relative to
 *   PTP (free-running encoders, network-fed transcoders, anything
 *   where the source clock is not disciplined to the same PTP grid
 *   the wire-side advertises).
 *
 * Drives the @ref MediaConfig::RtpVideoMediaClkMode /
 * @ref MediaConfig::RtpAudioMediaClkMode /
 * @ref MediaConfig::RtpDataMediaClkMode config keys and the
 * @c RtpMediaIO::buildSdp emission path.
 *
 * @par Modes
 *  - @c Auto — pick based on the stream's ts-refclk decision.  When
 *               a reference clock is advertised (Ptp or LocalMac),
 *               emit @c mediaclk:direct=&lt;offset&gt;; with @c None
 *               omit the @c mediaclk attribute entirely.  This is
 *               the default and matches today's behaviour.
 *  - @c Direct — force @c mediaclk:direct=&lt;offset&gt; emission.
 *                Offset comes from the per-stream
 *                @ref RtpMediaClock::mediaClkDirectOffset (0 for a
 *                natural PTP anchor) with the @c
 *                MediaConfig::RtpMediaClkOffset legacy override.
 *  - @c Sender — emit bare @c mediaclk:sender (no parameters).  Use
 *                for sources whose media clock is asynchronous to
 *                the reference clock.
 */
class RtpMediaClkMode : public TypedEnum<RtpMediaClkMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtpMediaClkMode", "Media Clock (mediaclk) Mode", 0,
                                           {"Auto", 0, "Automatic"}, {"Direct", 1, "Direct (Locked Offset)"},
                                           {"Sender", 2, "Sender (Asynchronous)"}); // default: Auto

                using TypedEnum<RtpMediaClkMode>::TypedEnum;

                static const RtpMediaClkMode Auto;
                static const RtpMediaClkMode Direct;
                static const RtpMediaClkMode Sender;
};

inline const RtpMediaClkMode RtpMediaClkMode::Auto{0};
inline const RtpMediaClkMode RtpMediaClkMode::Direct{1};
inline const RtpMediaClkMode RtpMediaClkMode::Sender{2};

/**
 * @brief Well-known Enum type for the SMPTE ST 2110-21 sender type.
 *
 * ST 2110-21:2022 §7.1 defines three sender shapes that differ in
 * how aggressively packets are spread across the active portion of
 * the frame interval:
 *
 * - @c TypeN — Narrow, gapped.  Packets land only inside the
 *   active line interval (R_ACTIVE × T_FRAME); inter-packet gap
 *   carries no traffic.  Tightest VRX bound (worst-case
 *   1500×8 / MAXUDP bytes) and lowest receiver-side jitter
 *   budget.  Requires hardware-grade pacing (NIC TXTIME with
 *   sub-µs precision).
 * - @c TypeNL — Narrow, linear.  Same VRX_FULL / CMAX bounds as
 *   Type N but packets are spread linearly across the entire
 *   frame interval (T_RS_l = T_FRAME / N_PACKETS).  Achievable
 *   on stock Linux with SO_TXTIME + a real-time-scheduled
 *   userspace TX thread.
 * - @c TypeW — Wide, linear.  Looser VRX bound
 *   (1500×720 / MAXUDP) and CMAX floor of 16 — accommodates
 *   stock kernel fair-queue pacing without TXTIME.  Default for
 *   any sender that doesn't claim narrow.
 *
 * Plus two policy values:
 *
 * - @c Auto — derive from the bound scheduler / pacing mode at
 *   open time.  @c RtpPacingMode::KernelFq / @c Userspace map
 *   to @c TypeW; @c TxTime maps to @c TypeNL; @c None maps to
 *   @c Unknown.
 * - @c Unknown — the sender cannot honestly claim a type.
 *   Suppresses the @c TP fmtp emission so receivers fall back to
 *   "treat as Type A" (RFC 4175 §B / ST 2110-21 §7.2).
 *
 * Drives @ref MediaConfig::RtpVideoSenderType /
 * @ref MediaConfig::RtpAudioSenderType /
 * @ref MediaConfig::RtpDataSenderType and the @c TP fmtp emission
 * in @c RtpMediaIO::buildSdp.
 */
class RtpSenderType : public TypedEnum<RtpSenderType> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtpSenderType", "ST 2110-21 Sender Type", 0,
                                           {"Auto", 0, "Automatic"}, {"Unknown", 1, "Unknown"},
                                           {"TypeN", 2, "Type N (Narrow, Gapped)"}, {"TypeNL", 3, "Type NL (Narrow, Linear)"},
                                           {"TypeW", 4, "Type W (Wide, Linear)"}); // default: Auto

                using TypedEnum<RtpSenderType>::TypedEnum;

                static const RtpSenderType Auto;
                static const RtpSenderType Unknown;
                static const RtpSenderType TypeN;
                static const RtpSenderType TypeNL;
                static const RtpSenderType TypeW;
};

inline const RtpSenderType RtpSenderType::Auto{0};
inline const RtpSenderType RtpSenderType::Unknown{1};
inline const RtpSenderType RtpSenderType::TypeN{2};
inline const RtpSenderType RtpSenderType::TypeNL{3};
inline const RtpSenderType RtpSenderType::TypeW{4};

/**
 * @brief Well-known Enum type for the SMPTE ST 2110-10 @c TSMODE SDP fmtp parameter.
 *
 * ST 2110-10 §7.9 / §8.7 describe how a sender labels its RTP
 * timestamps so a downstream receiver knows whether to align essences
 * by @c (NTP, RTP-TS) anchor pairs, by sample instant, or to treat the
 * stamp as freshly minted.
 *
 * - @c Samp — RTP-TS reflects the original sample instant; the
 *             sender passed @ref Frame::captureTime through unmodified.
 * - @c New  — RTP-TS was created anew at egress (synthetic generators
 *             such as TPG, videogen).
 * - @c Pres — RTP-TS was preserved from input that did not signal
 *             @c SAMP (CSC / mixer / receive-process-send devices).
 */
class RtpTsMode : public TypedEnum<RtpTsMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtpTsMode", "ST 2110-10 Timestamp Mode (TSMODE)", 0,
                                           {"Samp", 0, "Sample Instant (SAMP)"}, {"New", 1, "Newly Created (NEW)"},
                                           {"Pres", 2, "Preserved (PRES)"}); // default: Samp

                using TypedEnum<RtpTsMode>::TypedEnum;

                static const RtpTsMode Samp;
                static const RtpTsMode New;
                static const RtpTsMode Pres;
};

inline const RtpTsMode RtpTsMode::Samp{0};
inline const RtpTsMode RtpTsMode::New{1};
inline const RtpTsMode RtpTsMode::Pres{2};

/**
 * @brief Well-known Enum type for the metadata-stream wire format over RTP.
 *
 * Selects how the @c RtpMediaIO metadata stream serializes
 * per-frame @ref Metadata objects onto the wire.
 *
 * - @c JsonMetadata — serialize the @ref Metadata container via
 *                     its JSON toJson representation and ship the
 *                     resulting bytes as a dynamic-PT RTP payload
 *                     (see @ref RtpPayloadJson).  Simple, not
 *                     interoperable, useful for intra-promeki
 *                     round-trips and bring-up.
 * - @c St2110_40    — SMPTE ST 2110-40 / RFC 8331 Ancillary Data
 *                     over RTP.  Carries SMPTE ST 291 ANC packets
 *                     (closed captions, AFD, SCTE-104, VITC, etc.).
 *                     Placeholder entry — not yet implemented; the
 *                     backend rejects this value until the ANC
 *                     payload class lands.
 */
class MetadataRtpFormat : public TypedEnum<MetadataRtpFormat> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("MetadataRtpFormat", "Metadata RTP Wire Format", 0,
                                           {"JsonMetadata", 0, "JSON Metadata"},
                                           {"St2110_40", 1, "ST 2110-40 Ancillary Data (RFC 8331)"}); // default: JsonMetadata

                using TypedEnum<MetadataRtpFormat>::TypedEnum;

                static const MetadataRtpFormat JsonMetadata;
                static const MetadataRtpFormat St2110_40;
};

inline const MetadataRtpFormat MetadataRtpFormat::JsonMetadata{0};
inline const MetadataRtpFormat MetadataRtpFormat::St2110_40{1};

/**
 * @brief Well-known Enum type for @ref NullPacingMediaIO pacing strategy.
 *
 * Selects how the null-pacing sink times its frame consumption.
 * Used as the value of @ref MediaConfig::NullPacingMode.
 *
 * - @c Wallclock — emit one frame per @c 1/TargetFps wall-clock
 *                  interval; frames arriving inside an active interval
 *                  are dropped (counted in @ref MediaIOStats::FramesDropped)
 *                  rather than queued.  This is the default and the
 *                  mode used by the demo "fake playback device".
 * - @c Free      — drain every incoming frame at the upstream's
 *                  natural rate.  Useful as a passthrough sink for
 *                  measuring the upstream stage in isolation.
 */
class NullPacingMode : public TypedEnum<NullPacingMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("NullPacingMode", "Null Sink Pacing Mode", 0,
                                           {"Wallclock", 0, "Wall-Clock Paced"}, {"Free", 1, "Free-Running"});

                using TypedEnum<NullPacingMode>::TypedEnum;

                static const NullPacingMode Wallclock;
                static const NullPacingMode Free;
};

inline const NullPacingMode NullPacingMode::Wallclock{0};
inline const NullPacingMode NullPacingMode::Free{1};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
