/**
 * @file      ntptime.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <chrono>
#include <cstdint>
#include <promeki/duration.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief 64-bit NTP wall-clock timestamp (RFC 5905).
 * @ingroup time
 *
 * The NTP timestamp format is two 32-bit fields:
 * - @c seconds() — seconds since 1900-01-01 00:00:00 UTC.
 * - @c fraction() — fractional second in units of 1/2^32 s
 *   (i.e. ~233 ps resolution).
 *
 * Distinct from @ref TimeStamp, which is monotonic
 * @c std::chrono::steady_clock and therefore not directly usable as a
 * cross-machine wall-clock reference.  NTP time is wall-clock and is
 * the format RTCP Sender Reports (RFC 3550 §6.4.1) carry to let
 * receivers correlate RTP timestamps from independent streams to a
 * common clock.
 *
 * @par Construction sources
 * - @ref now — captures @c std::chrono::system_clock::now() and
 *   converts it to NTP form.
 * - @ref fromSystemClock — converts a caller-supplied system_clock
 *   instant.  Useful when several streams must derive their NTP /
 *   RTP-timestamp pairs from a single shared anchor instant.
 *
 * @par Wire encoding
 * RTCP SR carries the value as two consecutive 32-bit big-endian words
 * — @c seconds first, @c fraction second.  RTCP timestamp fields used
 * for delay measurement (@c LSR / @c DLSR) carry the @ref toCompact32
 * "middle 32 bits" form (lower 16 of @c seconds joined with upper 16
 * of @c fraction).
 */
class NtpTime {
        public:
                /// @brief Seconds offset from the NTP epoch (1900-01-01) to
                ///        the Unix epoch (1970-01-01) — 70 years × 365.25 d
                ///        × 86400 s, accounting for 17 leap days between
                ///        1900 and 1970.
                static constexpr uint64_t UnixEpochOffsetSeconds = 2208988800ULL;

                /// @brief Default-constructed instance is "epoch" (NTP 0).
                NtpTime() = default;

                /// @brief Constructs from raw @c seconds / @c fraction fields.
                NtpTime(uint32_t sec, uint32_t frac) : _seconds(sec), _fraction(frac) {}

                /// @brief Captures the current wall-clock time as an NTP timestamp.
                static NtpTime now() { return fromSystemClock(std::chrono::system_clock::now()); }

                /// @brief Converts a @c std::chrono::system_clock instant to NTP form.
                ///
                /// Useful when multiple streams must anchor to a single
                /// captured instant (so the per-stream RTCP SRs all carry
                /// NTP / RTP-timestamp pairs derived from the same wall
                /// clock).
                static NtpTime fromSystemClock(std::chrono::system_clock::time_point tp) {
                        // system_clock has implementation-defined epoch
                        // (Unix 1970-01-01 on every platform we ship to).
                        // Bridge to NTP via the constant above.
                        const auto nsSinceUnix =
                                std::chrono::duration_cast<std::chrono::nanoseconds>(tp.time_since_epoch()).count();
                        // Negative inputs (pre-1970) clamp to NTP epoch
                        // because NtpTime is unsigned.
                        if (nsSinceUnix < 0) return NtpTime(0, 0);
                        const uint64_t totalNs = static_cast<uint64_t>(nsSinceUnix) +
                                                 (UnixEpochOffsetSeconds * 1'000'000'000ULL);
                        const uint64_t secs = totalNs / 1'000'000'000ULL;
                        const uint64_t remNs = totalNs % 1'000'000'000ULL;
                        // fraction = remNs * 2^32 / 1e9.  Compute with
                        // 64-bit intermediate so the 2^32 multiply
                        // doesn't overflow.
                        const uint64_t frac = (remNs << 32) / 1'000'000'000ULL;
                        return NtpTime(static_cast<uint32_t>(secs & 0xFFFFFFFFu),
                                       static_cast<uint32_t>(frac & 0xFFFFFFFFu));
                }

                /// @brief Returns the seconds field.
                uint32_t seconds() const { return _seconds; }

                /// @brief Returns the fractional-seconds field (1/2^32 s units).
                uint32_t fraction() const { return _fraction; }

                /// @brief Returns the 64-bit packed form, seconds in high 32 bits.
                uint64_t toUint64() const {
                        return (static_cast<uint64_t>(_seconds) << 32) | static_cast<uint64_t>(_fraction);
                }

                /// @brief Returns the "compact 32-bit" form RTCP uses in
                ///        @c LSR / @c DLSR fields — the middle 32 bits of
                ///        the 64-bit timestamp (low 16 of @c seconds joined
                ///        with high 16 of @c fraction).
                uint32_t toCompact32() const { return ((_seconds & 0xFFFFu) << 16) | (_fraction >> 16); }

                bool isValid() const { return _seconds != 0 || _fraction != 0; }

                bool operator==(const NtpTime &o) const { return _seconds == o._seconds && _fraction == o._fraction; }
                bool operator!=(const NtpTime &o) const { return !(*this == o); }

                /**
                 * @brief Returns this NTP timestamp shifted forward
                 *        by @p d.
                 *
                 * Negative durations move the timestamp backward.
                 * Internally promotes the 64-bit packed form
                 * (seconds in high 32, fraction in low 32) to a
                 * fractional-seconds count, applies the offset in
                 * 1/2^32 s units (so nanosecond precision survives),
                 * and re-packs.  Wraps modularly across the
                 * 2^32-second boundary, mirroring how RTCP-SR
                 * interpretation handles it.
                 *
                 * Used by the RTP TX path to convert a
                 * @ref MediaTimeStamp captured against
                 * @c steady_clock to NTP wallclock via a single
                 * observed @c (steady, wall) reference instant —
                 * @c wallNow + (captureSteady - steadyNow).
                 *
                 * @param d Duration offset to add.
                 * @return The shifted NtpTime.
                 */
                NtpTime operator+(const Duration &d) const {
                        const int64_t  ns = d.nanoseconds();
                        const uint64_t packed = (static_cast<uint64_t>(_seconds) << 32) |
                                                static_cast<uint64_t>(_fraction);
                        // Split the offset into whole-seconds + sub-
                        // second nanoseconds.  Each piece converts to
                        // the 1/2^32-second packed representation
                        // independently, so the intermediate never
                        // overflows 64 bits even when the user hands
                        // us a multi-day duration:
                        //   * wholeSec contributes wholeSec << 32.
                        //   * subNs ∈ (-1e9, +1e9) maps via
                        //     subNs * 2^32 / 1e9 — and 1e9 * 2^32 fits
                        //     in 63 bits (≈ 4.29e18 < 2^63).
                        // Sums happen in uint64_t (well-defined modular
                        // arithmetic) — overflow at the 2^64 boundary
                        // wraps the same way NTP receivers handle
                        // wraparound across the 2^32-second epoch.
                        const int64_t  wholeSec = ns / 1'000'000'000;
                        const int64_t  subNs    = ns % 1'000'000'000;
                        const int64_t  subFrac  = (subNs << 32) / 1'000'000'000;
                        const uint64_t deltaPacked =
                                (static_cast<uint64_t>(wholeSec) << 32) + static_cast<uint64_t>(subFrac);
                        const uint64_t result = packed + deltaPacked;
                        return NtpTime(static_cast<uint32_t>(result >> 32) & 0xFFFFFFFFu,
                                       static_cast<uint32_t>(result & 0xFFFFFFFFu));
                }

                /// @brief See @ref operator+(const Duration &).
                NtpTime operator-(const Duration &d) const { return *this + (-d); }

        private:
                uint32_t _seconds = 0;
                uint32_t _fraction = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
