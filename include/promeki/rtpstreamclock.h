/**
 * @file      rtpstreamclock.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/ntptime.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Maps RTP timestamps to/from NTP wallclock using an
 *        @c (NTP, RTP-TS) anchor pair plus a clock rate.
 * @ingroup network
 *
 * The same modular @c uint32_t arithmetic
 *
 * @f$ NTP(t) = anchor.ntp + (t - anchor.rtpTs) / clockRate @f$
 *
 * is used on both ends of an RTP stream:
 *
 * - **Writer side** — the anchor pair is established at open time
 *   from a captured wallclock instant (@ref RtpSession::setRtpAnchor)
 *   and the RTCP Sender Report's @c (NTP, RTP-TS) fields are derived
 *   from it whenever the scheduler emits an SR.
 * - **Reader side** — the anchor pair is the most-recently parsed
 *   incoming Sender Report (@ref RtpSession::receivedSr), and
 *   @ref toNtp turns any RTP timestamp arriving on the wire into a
 *   wallclock instant a multi-stream aggregator can use to align
 *   essences.
 *
 * Because both ends run identical arithmetic against the same
 * @c (NTP, RTP-TS) pair, a single SR observation per stream is
 * sufficient for cross-stream lip-sync — the sender's SR ties the
 * receiver's clock to the sender's capture wallclock without needing
 * a shared grandmaster.
 *
 * The @c uint32_t subtraction @c (t @c - @c anchor.rtpTs) wraps
 * modularly, so the mapping survives the every-13-hour-or-so
 * wraparound of a 90 kHz video clock and the every-day-or-so
 * wraparound of a 48 kHz audio clock without any explicit handling.
 *
 * @par Validity
 * A default-constructed @c RtpStreamClock is invalid until both an
 * SR and a clock rate are set.  Receivers should treat @ref toNtp /
 * @ref toRtpTs results as meaningful only after @ref isValid returns
 * @c true; before then aggregators must fall back to a
 * non-wallclock-aware path so they can still produce output during
 * the first sub-second window before any SR has been observed.
 */
class RtpStreamClock {
        public:
                RtpStreamClock() = default;

                /**
                 * @brief Constructs a clock with an SR anchor and a
                 *        clock rate.
                 *
                 * @param srNtp     The NTP wallclock from the SR.
                 * @param srRtpTs   The RTP-TS that aligns with @p srNtp.
                 * @param clockRate The stream's RTP timestamp clock rate
                 *                  in Hz (e.g. 90000 for video, 48000
                 *                  for L16 audio at 48 kHz).
                 */
                RtpStreamClock(const NtpTime &srNtp, uint32_t srRtpTs, uint32_t clockRate)
                    : _srNtp(srNtp), _srRtpTs(srRtpTs), _clockRate(clockRate),
                      _valid(clockRate > 0) {}

                /// @brief True once both an SR pair and a non-zero clock
                ///        rate have been supplied.
                bool isValid() const { return _valid; }

                /**
                 * @brief Updates the SR anchor pair without touching
                 *        the clock rate.
                 *
                 * @param srNtp   The NTP wallclock from the SR.
                 * @param srRtpTs The RTP-TS that aligns with @p srNtp.
                 */
                void setSr(const NtpTime &srNtp, uint32_t srRtpTs) {
                        _srNtp = srNtp;
                        _srRtpTs = srRtpTs;
                        _valid = _clockRate > 0;
                }

                /**
                 * @brief Updates the clock rate without touching the
                 *        SR anchor pair.
                 *
                 * @param clockRate The stream's RTP timestamp clock
                 *                  rate in Hz.
                 */
                void setClockRate(uint32_t clockRate) {
                        _clockRate = clockRate;
                        _valid = _clockRate > 0 && _srNtp.isValid();
                }

                /// @brief Returns the SR's NTP wallclock anchor.
                const NtpTime &srNtp() const { return _srNtp; }

                /// @brief Returns the SR's RTP-TS anchor.
                uint32_t srRtpTs() const { return _srRtpTs; }

                /// @brief Returns the stream's RTP timestamp clock rate
                ///        in Hz.
                uint32_t clockRate() const { return _clockRate; }

                /**
                 * @brief Maps an RTP timestamp to its NTP wallclock
                 *        instant.
                 *
                 * Pure-function arithmetic, no system clock samples.
                 * Returns @c NtpTime() when the clock has no valid
                 * @c (SR, clockRate) yet.
                 *
                 * @param rtpTs The RTP timestamp to convert.
                 */
                NtpTime toNtp(uint32_t rtpTs) const;

                /**
                 * @brief Maps an NTP wallclock instant back to its
                 *        RTP timestamp.
                 *
                 * Inverse of @ref toNtp.  Used by the reader-side
                 * aggregator to ask "what audio RTP-TS does this video
                 * wallclock @c T correspond to" so audio samples can
                 * be drained against the same wallclock window the
                 * video frame was captured in.
                 *
                 * Returns @c 0 when the clock has no valid
                 * @c (SR, clockRate) yet.  When @p ntp precedes
                 * @ref srNtp the result wraps modulo @c 2^32, mirroring
                 * how the forward mapping handles RTP-TS wrap.
                 *
                 * @param ntp The NTP wallclock instant to convert.
                 */
                uint32_t toRtpTs(const NtpTime &ntp) const;

        private:
                NtpTime  _srNtp;
                uint32_t _srRtpTs = 0;
                uint32_t _clockRate = 0;
                bool     _valid = false;
};

PROMEKI_NAMESPACE_END
