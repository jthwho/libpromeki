/**
 * @file      enums_rtmp.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * RTMP role / handshake / pacing enums.
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
 * @brief Well-known Enum type for the RTMP role of a session.
 *
 * Shared by @c RtmpHandshake, @c RtmpSession, the future
 * @c RtmpServer, and any other layer that needs to pick a side.
 * Declared once here rather than as a private nested enum on each
 * class so the role can be threaded through MediaConfig keys and
 * Variant payloads without losing type identity.
 */
class RtmpRole : public TypedEnum<RtmpRole> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtmpRole", "RTMP Role", 0, {"Client", 0, "Client"},
                                                   {"Server", 1, "Server"}); // default: Client

                using TypedEnum<RtmpRole>::TypedEnum;

                static const RtmpRole Client;
                static const RtmpRole Server;
};

inline const RtmpRole RtmpRole::Client{0};
inline const RtmpRole RtmpRole::Server{1};

/**
 * @brief Well-known Enum type for the RTMP handshake mode.
 *
 * Drives both @c RtmpHandshake::setMode and the
 * @c MediaConfig::RtmpHandshakeMode key.
 *
 * - @c Auto    — try Complex first, fall back to Simple on the first
 *                peer-side rejection / disconnect.  Default — matches
 *                the OBS / FFmpeg compatibility layer.
 * - @c Simple  — RTMP 1.0 §5.2.1 / FLV-spec handshake (1-byte version
 *                + 1536-byte random nonce).  Some destinations reject
 *                this form (Wowza, some nginx-rtmp builds, historically
 *                Twitch); use @c Complex / @c Auto on those.
 * - @c Complex — Adobe FMS3 "digest+key" handshake.  HMAC-SHA256 over
 *                the C1/S1 payload with the GenuineFP / GenuineFMS keys.
 *                Required by some destinations.
 */
class RtmpHandshakeMode : public TypedEnum<RtmpHandshakeMode> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtmpHandshakeMode", "RTMP Handshake Mode", 0,
                                                   {"Auto", 0, "Automatic (Complex, Fall Back to Simple)"},
                                                   {"Simple", 1, "Simple (RTMP 1.0)"},
                                                   {"Complex", 2, "Complex (Adobe FMS3 Digest)"}); // default: Auto

                using TypedEnum<RtmpHandshakeMode>::TypedEnum;

                static const RtmpHandshakeMode Auto;
                static const RtmpHandshakeMode Simple;
                static const RtmpHandshakeMode Complex;
};

inline const RtmpHandshakeMode RtmpHandshakeMode::Auto{0};
inline const RtmpHandshakeMode RtmpHandshakeMode::Simple{1};
inline const RtmpHandshakeMode RtmpHandshakeMode::Complex{2};

/**
 * @brief Well-known Enum type for the @c RtmpMediaIO sink-side video pacing source.
 *
 * Drives @c RtmpMediaIO's strand-side @c PacingGate clock-binding policy.
 * RTMP is single-TCP-stream and has no kernel-pacing analog, so without
 * an explicit gate a synthetic feeder (TPG, file relay) bursts a full
 * GOP onto the wire on every IDR.  This enum picks where the gate's
 * clock comes from.
 *
 * - @c Internal — bind the gate to a built-in @c WallClock paced
 *                 against @c MediaConfig::FrameRate (or the
 *                 @c pendingMediaDesc frame rate).  The default — most
 *                 live destinations want approximately real-time
 *                 cadence and the strand has no other backpressure
 *                 source besides the bounded MessageQueue.
 * - @c External — leave the gate unbound at @c Open.  Stays a no-op
 *                 until @c executeCmd(MediaIOCommandSetClock) arrives
 *                 (typically forwarded from an upstream capture
 *                 board's port-group clock through the planner).  No
 *                 fallback to internal wall clock — the gate is a
 *                 no-op until the external clock binds.
 * - @c None     — never arm the gate.  Strand floods the per-kind
 *                 PayloadQueues at the upstream rate; backpressure
 *                 comes only from the bounded MessageQueue.  Fast-
 *                 pump file ingest mode.
 *
 * An @c executeCmd(MediaIOCommandSetClock) with a non-null clock
 * always wins over @c Internal once it arrives.  A null clock from
 * @c setClock re-arms back to the @c Internal policy when the
 * configured mode is @c Internal, or stays unbound otherwise.
 */
class RtmpVideoPacing : public TypedEnum<RtmpVideoPacing> {
        public:
                PROMEKI_REGISTER_ENUM_TYPE_DISPLAY("RtmpVideoPacing", "RTMP Video Pacing", 0,
                                                   {"Internal", 0, "Internal Wall Clock"},
                                                   {"External", 1, "External Clock"},
                                                   {"None", 2, "None (Unpaced)"}); // default: Internal

                using TypedEnum<RtmpVideoPacing>::TypedEnum;

                static const RtmpVideoPacing Internal;
                static const RtmpVideoPacing External;
                static const RtmpVideoPacing None;
};

inline const RtmpVideoPacing RtmpVideoPacing::Internal{0};
inline const RtmpVideoPacing RtmpVideoPacing::External{1};
inline const RtmpVideoPacing RtmpVideoPacing::None{2};

/** @} */

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
