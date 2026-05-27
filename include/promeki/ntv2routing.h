/**
 * @file      ntv2routing.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NTV2

#include <cstdint>
#include <promeki/enums_video.h>
#include <promeki/list.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief NTV2 crosspoint-routing helpers for single-link, dual-link,
 *        quad-link, and 12G SDI paths.
 * @ingroup proav
 *
 * Pure mappings: given the SMPTE @ref SdiLinkStandard, the
 * 1-based starting framestore channel, the 1-based starting SDI
 * port, and a few card-capability flags, produce a deterministic
 * list of (NTV2InputCrosspointID, NTV2OutputCrosspointID) pairs to
 * hand to @c CNTV2Card::Connect.
 *
 * Hardware-free.  The crosspoint IDs are passed as @c uint32_t so
 * the public header does not need to drag in the AJA SDK enums —
 * callers cast back to @c NTV2InputCrosspointID /
 * @c NTV2OutputCrosspointID when applying the connections.
 *
 * @par Scope
 *
 * - **Single-link** (@c SL_HD, @c SL_3GA, @c SL_3GB, @c SL_6G,
 *   @c SL_12G): one connection — `FB[ch] <- SDIIn[port]` for
 *   source mode, `SDIOut[port] <- FB[ch]YUV` for sink mode.
 * - **Quad-link Squares** (@c QL_3G_SQD): four straight pairs,
 *   one per quadrant.
 * - **Quad-link 2SI** (@c QL_3G_2SI): TSI-mux paths that bind
 *   four SDI ports to two framestores via the per-channel TSI
 *   mux block.
 * - **Dual-link** (@c DL_HD, @c DL_3G, @c DL_3GB): not yet built;
 *   returns an empty list.  Add when a card under test needs it.
 *
 * @par YUV-only for now
 *
 * Phase 5 only emits YUV framestore connections (CSC bridges left
 * to the planner).  The @p framebufferRgb argument is reserved so
 * the API doesn't need to change when RGB-framestore routing
 * lands; a non-empty list is still returned (with FB-RGB
 * crosspoints) when the flag is true, but the caller is expected
 * to wire its own CSC connections.
 */
namespace Ntv2Routing {

        /**
         * @brief One crosspoint connection.
         *
         * The crosspoint IDs are AJA's @c NTV2InputCrosspointID and
         * @c NTV2OutputCrosspointID values, stored as @c uint32_t so
         * the public header stays SDK-free.  Apply via
         * @c CNTV2Card::Connect(NTV2InputCrosspointID(input),
         * NTV2OutputCrosspointID(output), validate=false).
         */
        struct Connection {
                        uint32_t input;
                        uint32_t output;

                        bool operator==(const Connection &o) const {
                                return input == o.input && output == o.output;
                        }
                        bool operator!=(const Connection &o) const { return !(*this == o); }
        };

        /** @brief Convenience alias for the connection list. */
        using ConnectionList = ::promeki::List<Connection>;

        /**
         * @brief Per-call routing inputs.
         *
         * Gathered into a struct so the helper signatures don't
         * balloon as new toggles (CSC, signal RGB, future card-
         * specific knobs) are added.  Defaults match the Phase-5
         * single-link YUV path so existing call sites that only
         * fill the first few fields still get sensible behaviour.
         */
        struct Config {
                        /// SMPTE link standard requested by the caller.
                        SdiLinkStandard standard{SdiLinkStandard::Auto};
                        /// 1-based starting framestore channel.
                        int channelStart = 1;
                        /// 1-based starting SDI port.
                        int portStart = 1;
                        /// @c true on cards with a dedicated 12G
                        /// crosspoint (Kona 5 + newer).  When @c true
                        /// @ref SdiLinkStandard::SL_12G uses the
                        /// single-FB connection; otherwise it falls
                        /// back to a 2SI pattern.
                        bool can12gRouting = false;
                        /// @c true when the framestore is configured
                        /// for an RGB pixel format.
                        bool framebufferRgb = false;
                        /// @c true when the SDI / HDMI wire carries
                        /// RGB rather than YUV.  Default false because
                        /// SDI is overwhelmingly YUV; flip to @c true
                        /// for dual-link RGB and HDMI RGB.
                        bool signalRgb = false;
                        /// @c true when the helper may insert one
                        /// on-board CSC per quadrant to bridge a
                        /// framebuffer-vs-signal colour-family
                        /// mismatch.  When @c false the helper omits
                        /// CSCs and the resulting routing is only
                        /// valid when @ref framebufferRgb ==
                        /// @ref signalRgb.
                        bool allowOnBoardCsc = true;
                        /// Additional 1-based starting SDI ports for
                        /// fanout — each entry replays the same
                        /// link-standard routing as @ref portStart
                        /// but driving a different destination port
                        /// group, all sourced from the same
                        /// framestore.  Empty (the default) means
                        /// "no fanout."  Sink-mode only — source-mode
                        /// dispatch ignores this list because
                        /// mirroring an input doesn't make sense.
                        ::promeki::List<int> mirrorPortStarts;
        };

        /**
         * @brief Builds the crosspoint connection list for an SDI
         *        capture (source-mode) path.
         *
         * @param standard         The SMPTE link standard requested
         *                         by the caller (typically resolved
         *                         from @ref SdiSignalConfig).
         * @param channel          1-based starting framestore channel.
         *                         For single-link this is the only
         *                         framestore; for quad-link this is
         *                         the first of four.
         * @param startPort        1-based starting SDI input port.
         * @param can12gRouting    @c true on cards that expose a
         *                         12G crosspoint that lets a single
         *                         framestore consume a 12G stream
         *                         directly (Kona 5 + newer).  When
         *                         @c true and @p standard is
         *                         @c SL_12G, the helper produces the
         *                         single-FB connection; when @c false
         *                         the helper falls back to a quad-link
         *                         pattern.
         * @param framebufferRgb   @c true when the framestore is
         *                         configured for an RGB pixel format
         *                         (reserved — Phase 5 still emits the
         *                         framestore-input crosspoints
         *                         directly, leaving CSC insertion to
         *                         the planner).
         * @return A possibly-empty list of connections.  Empty
         *         indicates an unsupported (standard, capability)
         *         combination; the caller should fail Open with
         *         @c Error::NotSupported.
         */
        ConnectionList sdiInputConnections(const SdiLinkStandard &standard, int channel, int startPort,
                                           bool can12gRouting, bool framebufferRgb);

        /**
         * @brief Struct-form overload of @ref sdiInputConnections.
         *
         * The struct-form is the canonical entry point — accepts the
         * full @ref Config so CSC, signal-RGB, and future toggles can
         * propagate without further signature churn.  The free-arg
         * overload is shorthand for the
         * @c (framebufferRgb=signalRgb=false, allowCsc=true) common
         * case.
         */
        ConnectionList sdiInputConnections(const Config &cfg);

        /**
         * @brief Builds the crosspoint connection list for an SDI
         *        playout (sink-mode) path.
         *
         * Inverse of @ref sdiInputConnections — the same
         * (standard, channel, port, caps) combination produces
         * connections from FB-output crosspoints into SDI-output
         * input crosspoints (and through TSI mux on the 2SI path).
         */
        ConnectionList sdiOutputConnections(const SdiLinkStandard &standard, int channel, int startPort,
                                            bool can12gRouting, bool framebufferRgb);

        /**
         * @brief Struct-form overload of @ref sdiOutputConnections.
         *
         * See @ref sdiInputConnections (Config) — same model.
         */
        ConnectionList sdiOutputConnections(const Config &cfg);

        /**
         * @brief Returns @c true when @p standard requires the
         *        per-channel TSI mux engine (i.e. the open path
         *        must call @c CNTV2Card::SetTsiFrameEnable).
         *
         * @p can12gRouting promotes @c SL_12G to TSI on Kona-5-class
         *  cards.  All quad-link 2SI variants always need TSI.
         */
        bool needsTsi(const SdiLinkStandard &standard, bool can12gRouting);

        /**
         * @brief Returns @c true when @p standard wants the legacy
         *        "4K Squares" framestore-bundling mode
         *        (@c CNTV2Card::Set4kSquaresEnable).
         */
        bool needsSquares(const SdiLinkStandard &standard);

} // namespace Ntv2Routing

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NTV2
