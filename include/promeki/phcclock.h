/**
 * @file      phcclock.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>
#include <promeki/clockdomain.h>
#include <promeki/error.h>
#include <promeki/namespace.h>
#include <promeki/ntptime.h>
#include <promeki/result.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/uniqueptr.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Userspace wrapper around a Linux PTP Hardware Clock (PHC)
 *        character device.
 * @ingroup network
 *
 * The pragmatic Phase D2 default for ST 2110 deployments is to let
 * @c linuxptp's @c ptp4l (and, when @c CLOCK_REALTIME alignment is
 * also wanted, @c phc2sys) drive a PHC on the NIC and to expose it
 * to userspace via @c /dev/ptpN.  @c PhcClock is the read-only
 * userspace handle to that device: it does **not** speak the IEEE
 * 1588 wire protocol itself, it just queries the kernel's view of
 * the disciplined hardware clock.
 *
 * @par Time domain
 * Linux PHC devices report **TAI** seconds (no leap-second jumps),
 * matching the SMPTE PTP profile (ST 2059-2) which runs in TAI.
 * @ref now returns a @ref TimeStamp whose nanoseconds are TAI seconds
 * since the Unix epoch (@c 1970-01-01).  @ref ntpNow converts that
 * to an @ref NtpTime by subtracting the current TAI-UTC offset (37
 * seconds since 2017-01-01; queried at construction via @c adjtimex
 * and refreshable via @ref refreshTaiUtcOffset).
 *
 * @par Capabilities + lock state
 * The Linux PHC API exposes capabilities via @c PTP_CLOCK_GETCAPS
 * (n_alarm / n_ext_ts / n_per_out / pps / n_pins / cross_timestamping
 * / adjust_phase).  Lock state per se is **not** in the PHC API — a
 * PHC clock is "locked" when @c ptp4l reports @c PORT_DATA_SET.state
 * = @c SLAVE and the steady-state offset is within the SMPTE profile's
 * acceptable bound.  @ref isLocked therefore relies on two indirect
 * signals: (a) cross-timestamping is supported, (b) a recent
 * @ref sysOffsetPrecise sample shows a sub-microsecond
 * @c (sys, phc) skew.  Applications that need authoritative lock
 * state should listen on @c ptp4l's UDS socket directly — the
 * library wrapper for that is out of scope here.
 *
 * @par Thread Safety
 * Concurrent @ref now / @ref ntpNow / @ref sysOffsetPrecise calls on
 * the same instance are safe — the underlying @c clock_gettime /
 * @c ioctl syscalls are kernel-side atomic.  @ref open / @ref close /
 * @ref refreshTaiUtcOffset are **not** thread-safe with concurrent
 * reads; serialise externally.
 *
 * @par Platform
 * Linux only.  On non-Linux @ref open returns
 * @c Error::NotSupported and every reader returns the equivalent.
 *
 * @par Example
 * @code
 * auto clk = PhcClock::open("/dev/ptp0");
 * if (clk.second().isError()) { ... }
 * NtpTime wall = clk.first().ntpNow();   // PTP-traceable wallclock
 * TimeStamp tai = clk.first().now();     // TAI nanoseconds
 * @endcode
 */
class PhcClock {
        public:
                /// @brief Default-constructed @c PhcClock is invalid.
                PhcClock() = default;

                /// @brief Move-only (file descriptor ownership).
                PhcClock(PhcClock &&other) noexcept;
                PhcClock &operator=(PhcClock &&other) noexcept;

                PhcClock(const PhcClock &) = delete;
                PhcClock &operator=(const PhcClock &) = delete;

                ~PhcClock();

                /**
                 * @brief Capabilities reported by @c PTP_CLOCK_GETCAPS.
                 *
                 * Mirrors the kernel's @c struct ptp_clock_caps with
                 * the fields the library actually consults today; new
                 * fields land here on demand.
                 */
                struct Caps {
                                int maxAdjustmentPpb = 0;       ///< Max frequency adjustment (parts-per-billion).
                                int numAlarms = 0;              ///< Programmable alarm count.
                                int numExtTs = 0;               ///< External timestamp channels.
                                int numPerOut = 0;              ///< Periodic output channels.
                                int numPins = 0;                ///< Programmable I/O pin count.
                                bool ppsSupported = false;      ///< 1 PPS event reporting available.
                                bool crossTimestamping = false; ///< Atomic @c (sys, phc, sys) reads via PTP_SYS_OFFSET_PRECISE.
                                bool adjustPhase = false;       ///< Phase-adjustment IOCTL available.
                };

                /**
                 * @brief Atomic @c (system_realtime, phc, system_realtime)
                 *        triplet from @c PTP_SYS_OFFSET_PRECISE.
                 *
                 * The kernel snapshots @c CLOCK_REALTIME both before
                 * and after reading the PHC, all without an interrupt
                 * window.  Callers compute the @c (steady, phc) bridge
                 * by averaging the two @c sysRealtime samples.  All
                 * fields are nanoseconds since the Unix epoch.
                 */
                struct SysOffset {
                                int64_t sysRealtimeNs = 0;     ///< CLOCK_REALTIME before the PHC read (averaged with @c sysRealtimePostNs by callers).
                                int64_t phcNs = 0;             ///< PHC clock at the synchronised instant (TAI).
                                int64_t sysRealtimePostNs = 0; ///< CLOCK_REALTIME after the PHC read.
                };

                /**
                 * @brief Opens a PHC character device.
                 *
                 * @param path @c /dev/ptpN device path.  Defaults to
                 *             @c "/dev/ptp0".
                 * @return A valid @c PhcClock on success; an error
                 *         @c Result on failure.  Returns
                 *         @c Error::NotSupported on non-Linux platforms.
                 */
                static Result<PhcClock> open(const String &path = String("/dev/ptp0"));

                /// @brief True when the underlying file descriptor is open.
                bool isOpen() const { return _fd >= 0; }

                /// @brief Returns the @c /dev/ptpN path this clock was opened on.
                const String &devicePath() const { return _path; }

                /// @brief Returns the underlying file descriptor (read-only).
                int fileDescriptor() const { return _fd; }

                /// @brief Returns the capabilities snapshot taken at @ref open.
                const Caps &caps() const { return _caps; }

                /**
                 * @brief Reads the PHC clock.
                 *
                 * @return A @ref TimeStamp whose nanoseconds are TAI
                 *         seconds since the Unix epoch.  Returns an
                 *         invalid @c TimeStamp when @ref isOpen is
                 *         @c false or the syscall fails.
                 */
                Result<TimeStamp> now() const;

                /**
                 * @brief Returns @ref now converted to an @ref NtpTime.
                 *
                 * Subtracts @ref taiUtcOffsetSeconds from the TAI
                 * nanoseconds to land at UTC, then converts to NTP
                 * wallclock form (seconds since 1900-01-01).
                 */
                Result<NtpTime> ntpNow() const;

                /**
                 * @brief Issues a @c PTP_SYS_OFFSET_PRECISE ioctl for a
                 *        cross-timestamped @c (sys, phc, sys) triplet.
                 *
                 * Returns @c Error::NotSupported when the underlying
                 * NIC does not advertise @c crossTimestamping in its
                 * @ref Caps.
                 */
                Result<SysOffset> sysOffsetPrecise() const;

                /// @brief Current TAI-UTC offset in seconds (37 since 2017-01-01).
                int taiUtcOffsetSeconds() const { return _taiUtcOffsetSec; }

                /// @brief Overrides the TAI-UTC offset manually (testing / stale-host fallback).
                void setTaiUtcOffsetSeconds(int seconds) { _taiUtcOffsetSec = seconds; }

                /**
                 * @brief Refreshes the TAI-UTC offset from the kernel
                 *        via @c adjtimex.
                 *
                 * Called once at @ref open; expose so applications
                 * running across a leap-second boundary can re-query
                 * without reopening the device.  Returns
                 * @c Error::NotSupported on non-Linux.
                 */
                Error refreshTaiUtcOffset();

                /**
                 * @brief Heuristic lock-state probe (see class doc).
                 *
                 * @return @c true when @ref caps reports
                 *         @c crossTimestamping and the most recent
                 *         @ref sysOffsetPrecise (if available) shows
                 *         a @c |sysRealtime - phc - taiOffsetSec*1e9|
                 *         below @p toleranceNs.
                 *
                 * @param toleranceNs Lock-tolerance in nanoseconds.
                 *                    Default 1 ms — generous for SMPTE
                 *                    PTP profile interop but tight
                 *                    enough that an un-disciplined PHC
                 *                    is rejected.
                 */
                bool isLocked(int64_t toleranceNs = 1'000'000LL) const;

                /// @brief Releases the file descriptor; safe to call multiple times.
                void close();

                /**
                 * @brief Binds this clock as the wallclock-now provider
                 *        for @p domain.
                 *
                 * Installs a lambda that reads @ref now and converts
                 * TAI → UTC into a Unix-epoch nanosecond timestamp,
                 * matching the @ref ClockDomain::WallClockProvider
                 * contract.  Defaults to @ref ClockDomain::Ptp.
                 *
                 * The lambda captures @c this by pointer, so
                 * @c bindAsDomain must be called *after* the
                 * @c PhcClock is at its final storage address (move-
                 * after-bind would dangle the captured pointer).  Call
                 * @ref unbindDomain at teardown to clear the registry
                 * entry before the @c PhcClock destructs.
                 *
                 * Returns @c Error::NotOpen when the clock has no
                 * underlying file descriptor.
                 *
                 * @param domain The @ref ClockDomain::ID to bind to
                 *               (default @ref ClockDomain::Ptp).
                 */
                Error bindAsDomain(const ClockDomain::ID &domain = ClockDomain::Ptp);

                /**
                 * @brief Unbinds this clock from @p domain.
                 *
                 * Replaces the registry's provider with an empty
                 * function so subsequent @c ClockDomain::nowUtcNs
                 * calls return @c 0.  Safe to call multiple times.
                 */
                void unbindDomain(const ClockDomain::ID &domain = ClockDomain::Ptp);

                /**
                 * @brief Reads the kernel's current TAI-UTC offset via
                 *        @c adjtimex, independent of any open PHC
                 *        device.
                 *
                 * Process-wide system info — useful for code that
                 * needs the offset but doesn't have a @ref PhcClock
                 * instance handy (e.g. the ST 2110-40 §6.4 LLTM
                 * deadline math in the ANC packetizer).  Falls back
                 * to 37 (the post-2017-01-01 IERS-published value) on
                 * non-Linux or when @c adjtimex fails.
                 *
                 * @return Current TAI-UTC offset in seconds.
                 */
                static int systemTaiUtcOffsetSeconds();

                /**
                 * @brief Converts a UTC nanosecond timestamp to TAI
                 *        nanoseconds.
                 *
                 * Adds @ref systemTaiUtcOffsetSeconds × @c 1e9 to
                 * @p utcNs.  Used by the ST 2110-40 LLTM TX path to
                 * convert @c RtpMediaClock::tvdUtcNs(N) (UTC) into a
                 * @c CLOCK_TAI deadline that the kernel's
                 * @c SCM_TXTIME path consumes.  Returns @c 0
                 * unchanged so a packetizer that runs without a PTP
                 * anchor can stamp the batch unconditionally and the
                 * "no deadline" sentinel propagates.
                 *
                 * @param utcNs Nanoseconds since the Unix epoch in
                 *              the UTC timescale, or @c 0 for the
                 *              "no deadline" sentinel.
                 * @return Nanoseconds since the Unix epoch in the
                 *         TAI timescale, or @c 0 when @p utcNs was
                 *         @c 0.
                 */
                static uint64_t utcNsToTaiNs(int64_t utcNs);

        private:
                explicit PhcClock(int fd, String path, Caps caps, int taiUtcSec)
                    : _fd(fd), _path(std::move(path)), _caps(caps),
                      _taiUtcOffsetSec(taiUtcSec) {}

                int    _fd = -1;
                String _path;
                Caps   _caps;
                int    _taiUtcOffsetSec = 37; ///< Default since 2017-01-01.
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
