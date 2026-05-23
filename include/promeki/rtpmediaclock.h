/**
 * @file      rtpmediaclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/clockdomain.h>
#include <promeki/duration.h>
#include <promeki/framerate.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Pure-arithmetic mapping between a stream's frame index, its
 *        RTP timestamp, and its wallclock T_VD instant.
 * @ingroup network
 *
 * ST 2110-10 §4.12-4.14 distinguishes three layered timing concepts:
 *
 *  - **Media Clock** — the abstract rate at which essence samples are
 *    produced (90 kHz for video, 48 kHz / 96 kHz for audio).  Locked
 *    to a Common Reference Clock (PTP grandmaster) in a conformant
 *    deployment.
 *  - **RTP Clock** — the counter that actually populates the
 *    @c rtp.timestamp field.  Advances at the media clock rate; per
 *    RFC 3550 the starting value is arbitrary, but per ST 2110-10
 *    §7.4 a conformant sender aligns it to the SMPTE Epoch grid so a
 *    PTP-locked receiver can derive @c T_VD purely from the on-wire
 *    RTP-TS.
 *  - **Common Reference Clock** — the upstream PTP grandmaster.
 *    Phase D2 wires this in via @ref PhcClock /
 *    @ref ClockDomain::Ptp.
 *
 * @c RtpMediaClock owns the RTP-Clock side of that triple.  It
 * composes with @ref RtpStreamClock — the receiver-side
 * @c (NTP, RTP-TS) ↔ wallclock map — rather than replacing it: the
 * stream clock handles inter-stream sync via SRs, while this class
 * handles intra-stream per-frame RTP-TS derivation in pure
 * arithmetic, no per-frame @c system_clock samples.
 *
 * @par Anchor modes
 *  - @ref frameZeroAnchored — legacy mode where frame 0 maps to
 *    @c RTP-TS=0.  Equivalent to today's
 *    @c FrameRate::cumulativeTicks call site; a receiver can still
 *    derive wallclock from the RTCP SR but cannot use
 *    @c mediaclk:direct=0 alignment because the stream's RTP-TS
 *    isn't on the SMPTE Epoch grid.
 *  - @ref ptpAnchored — Phase D2 / D3 mode where frame 0 maps to a
 *    given UTC wallclock instant (typically read from
 *    @ref ClockDomain::Ptp at open time), and the corresponding
 *    @c RTP-TS is computed as the projection of that instant onto
 *    the media-clock grid: <tt>floor(anchorUtcNs * mediaClockHz /
 *    1e9) mod 2^32</tt>.  Conformant @c mediaclk:direct=0 emission
 *    follows directly.
 *
 * @par TR_OFFSET (ST 2110-10 §7.4)
 * Optional fixed offset added to @c T_VD beyond the natural
 * @c (N * T_FRAME) cadence.  Default zero — the broadcast-conformance
 * audit covers narrow-timing sender classes (Type N / NL / W) in
 * Phase E21, which will populate this from the @c TRO_DEFAULT table.
 * The library accepts a caller-supplied value today via
 * @ref setTrOffset so applications doing their own pacing math can
 * pin it explicitly.
 *
 * @par Thread Safety
 * Read-only once constructed; concurrent @ref rtpTsForFrame /
 * @ref tvdUtcNs calls on the same instance are safe.  Mutations
 * (@ref setTrOffset) are not thread-safe with concurrent reads;
 * serialise externally.  In typical use the writer's open-time
 * configuration constructs the clock once and the per-frame TX path
 * only reads.
 */
class RtpMediaClock {
        public:
                /**
                 * @brief Default-constructed clock is invalid.
                 *
                 * Every reader (@ref rtpTsForFrame, @ref tvdUtcNs,
                 * @ref tvdUtcNsForRtpTs, @ref mediaClkDirectOffset)
                 * returns @c 0 until the clock is configured via
                 * @ref frameZeroAnchored or @ref ptpAnchored.
                 */
                RtpMediaClock() = default;

                /**
                 * @brief Constructs a @c frame-zero-anchored clock.
                 *
                 * Frame 0 maps to @c RTP-TS=0 and the wallclock
                 * anchor is unset (so @ref tvdUtcNs returns @c 0).
                 * Matches today's @c FrameRate::cumulativeTicks
                 * behaviour — kept as the default when no PHC is
                 * bound.
                 *
                 * @param mediaClockHz Media-clock rate in Hz (e.g.
                 *                     90000 for ST 2110-20 video,
                 *                     48000 for L16 audio).
                 * @param rate         Frame-rate / packet-rate of
                 *                     the essence (for audio, the
                 *                     packet rate — packetsPerSecond).
                 */
                static RtpMediaClock frameZeroAnchored(uint32_t mediaClockHz, const FrameRate &rate);

                /**
                 * @brief Constructs a PTP-anchored clock.
                 *
                 * Frame 0 maps to wallclock @p anchorUtcNs and
                 * @c RTP-TS = floor(anchorUtcNs * mediaClockHz / 1e9)
                 * mod 2^32 — i.e. the RTP-TS that a PTP-locked
                 * receiver computing
                 * @c floor(wallclock * mediaClockHz / 1e9) would see
                 * at the same instant.
                 *
                 * A @c 0 / negative @p anchorUtcNs falls back to
                 * @ref frameZeroAnchored.
                 *
                 * @param mediaClockHz  Media-clock rate in Hz.
                 * @param rate          Frame / packet rate.
                 * @param anchorUtcNs   Wallclock UTC nanoseconds at
                 *                      frame 0.  Typically read from
                 *                      @c ClockDomain::nowUtcNs(
                 *                      ClockDomain::Ptp) at open time.
                 */
                static RtpMediaClock ptpAnchored(uint32_t mediaClockHz, const FrameRate &rate,
                                                 int64_t anchorUtcNs);

                /// @brief @c true once a valid @c (mediaClockHz, frameRate) pair has been set.
                bool isValid() const { return _mediaClockHz > 0 && _frameRate.isValid(); }

                /// @brief Media-clock rate in Hz (e.g. 90000, 48000, 96000).
                uint32_t mediaClockHz() const { return _mediaClockHz; }

                /// @brief Frame / packet rate driving the per-step advance.
                const FrameRate &frameRate() const { return _frameRate; }

                /// @brief Wallclock UTC nanoseconds at @ref anchorFrameIndex; @c 0 = unset.
                int64_t anchorUtcNs() const { return _anchorUtcNs; }

                /// @brief RTP-TS at @ref anchorFrameIndex.
                uint32_t anchorRtpTs() const { return _anchorRtpTs; }

                /// @brief Frame index at which the anchor was pinned (typically 0).
                int64_t anchorFrameIndex() const { return _anchorFrameIndex; }

                /// @brief @c true when this clock has a PTP-grade wallclock anchor.
                bool hasPtpAnchor() const { return _anchorUtcNs > 0; }

                /// @brief Current TR_OFFSET (default @c Duration::zero).
                const Duration &trOffset() const { return _trOffset; }

                /**
                 * @brief Sets the ST 2110-10 §7.4 TR_OFFSET.
                 *
                 * Mutator — not thread-safe with concurrent
                 * @ref rtpTsForFrame / @ref tvdUtcNs reads.
                 */
                void setTrOffset(const Duration &d) { _trOffset = d; }

                /**
                 * @brief Returns the modular RTP-TS for @p frameIndex.
                 *
                 * Pure arithmetic.  For
                 * @c frameIndex >= @ref anchorFrameIndex computes
                 * <tt>anchorRtpTs +
                 * FrameRate::cumulativeTicks(mediaClockHz, frameIndex -
                 * anchorFrameIndex) + trOffsetTicks</tt> with modular
                 * @c uint32_t wrap.  For @c frameIndex < anchor the
                 * relative frame is negative and
                 * @c FrameRate::cumulativeTicks clamps to @c 0; the
                 * result is then just @c anchorRtpTs + trOffsetTicks.
                 *
                 * @param frameIndex Zero-based frame index.
                 * @return Modular RTP-TS, or @c 0 when the clock is
                 *         invalid.
                 */
                uint32_t rtpTsForFrame(int64_t frameIndex) const;

                /**
                 * @brief Returns the wallclock UTC nanoseconds for the
                 *        first sample of @p frameIndex.
                 *
                 * Returns @c 0 when the clock is
                 * @ref frameZeroAnchored (no wallclock anchor) or
                 * invalid.  For
                 * @c frameIndex >= @ref anchorFrameIndex computes
                 * <tt>anchorUtcNs +
                 * FrameRate::cumulativeTicks(1e9, frameIndex -
                 * anchorFrameIndex) + trOffsetNs</tt>.
                 *
                 * @param frameIndex Zero-based frame index.
                 * @return UTC nanoseconds since the Unix epoch, or 0.
                 */
                int64_t tvdUtcNs(int64_t frameIndex) const;

                /**
                 * @brief Inverse of @ref rtpTsForFrame — maps an
                 *        RTP-TS to its wallclock instant.
                 *
                 * Modular arithmetic; the caller is responsible for
                 * disambiguating which @c 2^32-tick wrap window the
                 * @p rtpTs lies in.  The library uses the receiver's
                 * SR-anchor convention — pick the nearest wallclock
                 * to the SR.
                 *
                 * Returns @c 0 when the clock is
                 * @ref frameZeroAnchored (no wallclock anchor) or
                 * invalid.
                 *
                 * @param rtpTs Modular RTP-TS.
                 * @return UTC nanoseconds since the Unix epoch, or 0.
                 */
                int64_t tvdUtcNsForRtpTs(uint32_t rtpTs) const;

                /**
                 * @brief Returns the RFC 7273 @c mediaclk:direct=<offset>
                 *        value.
                 *
                 * The RFC defines the offset as "the RTP timestamp
                 * that corresponds to the moment in time at which the
                 * reference clock value is zero" — i.e. the RTP-TS at
                 * wallclock UTC nanoseconds = 0.
                 *
                 * For a PTP-anchored clock with the natural anchor
                 * (@c anchorRtpTs = floor(anchorUtcNs * mediaClockHz
                 * / 1e9) mod 2^32), the offset is @c 0.  Non-natural
                 * anchors (where the application pinned a specific
                 * @c anchorRtpTs not aligned with the SMPTE Epoch
                 * grid) carry a non-zero offset.  Frame-zero-anchored
                 * clocks return @c 0 — the offset is meaningless
                 * without a wallclock anchor and the SDP emitter is
                 * expected to omit @c mediaclk:direct in that case.
                 */
                uint32_t mediaClkDirectOffset() const;

        private:
                explicit RtpMediaClock(uint32_t mediaClockHz, const FrameRate &rate)
                    : _mediaClockHz(mediaClockHz), _frameRate(rate) {}

                uint32_t  _mediaClockHz = 0;
                FrameRate _frameRate;
                int64_t   _anchorFrameIndex = 0;
                int64_t   _anchorUtcNs = 0;
                uint32_t  _anchorRtpTs = 0;
                // Default-constructed Duration is Invalid (INT64_MIN);
                // explicit zero is the documented "no offset" value
                // for ST 2110-10 §7.4 — every reader treats Invalid
                // as zero as a belt-and-braces guard but the in-class
                // initialiser is the primary fix.
                Duration  _trOffset = Duration::zero();
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
