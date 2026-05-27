/**
 * @file      phcclock.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/phcclock.h>

#include <promeki/logger.h>
#include <promeki/platform.h>

#if defined(PROMEKI_PLATFORM_LINUX)
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/timex.h>
#include <time.h>
#include <unistd.h>

#include <linux/ptp_clock.h>
#endif

PROMEKI_NAMESPACE_BEGIN

#if defined(PROMEKI_PLATFORM_LINUX)

namespace {

// Linux's posix-timers handle for a /dev/ptpN file descriptor.
// Defined as a macro in some kernel headers but not exposed under
// every libc; provide a private fallback that matches Linux's
// kernel/posix-timers.c construction.
constexpr int phcFdToClockId(int fd) {
        return (~fd << 3) | 3;
}

// Reads the kernel's current TAI-UTC offset via adjtimex.  When the
// host runs ntpd / chrony / phc2sys the @c tai field is the
// authoritative value; on an un-disciplined host it falls back to 0
// and the caller substitutes a static default.
int readTaiUtcOffsetSeconds() {
        struct timex tx = {};
        const int    state = adjtimex(&tx);
        if (state < 0) {
                promekiWarnOnce("PhcClock: adjtimex failed (%s), defaulting TAI-UTC=37",
                                strerror(errno));
                return 37;
        }
        if (tx.tai > 0) return static_cast<int>(tx.tai);
        // adjtimex returns tai=0 when the host has never been told a
        // TAI offset; the constant 37 has been correct since
        // 2017-01-01 and remains correct until the next IERS-announced
        // leap second.
        return 37;
}

PhcClock::Caps readCapsLocked(int fd) {
        PhcClock::Caps   out;
        struct ptp_clock_caps caps = {};
        if (ioctl(fd, PTP_CLOCK_GETCAPS, &caps) != 0) {
                promekiWarn("PhcClock: PTP_CLOCK_GETCAPS failed: %s", strerror(errno));
                return out;
        }
        out.maxAdjustmentPpb = caps.max_adj;
        out.numAlarms = caps.n_alarm;
        out.numExtTs = caps.n_ext_ts;
        out.numPerOut = caps.n_per_out;
        out.numPins = caps.n_pins;
        out.ppsSupported = caps.pps != 0;
        out.crossTimestamping = caps.cross_timestamping != 0;
#if defined(PTP_CAPS_ADJ_PHASE)
        out.adjustPhase = (caps.adjust_phase != 0);
#else
        // Older kernels expose adjust_phase as a struct field but
        // without the matching capability bit.  Treat its presence as
        // a runtime probe done by callers that need it.
        out.adjustPhase = false;
#endif
        return out;
}

} // namespace

#endif // PROMEKI_PLATFORM_LINUX

PhcClock::PhcClock(PhcClock &&other) noexcept
    : _fd(other._fd), _path(std::move(other._path)), _caps(other._caps),
      _taiUtcOffsetSec(other._taiUtcOffsetSec) {
        other._fd = -1;
}

PhcClock &PhcClock::operator=(PhcClock &&other) noexcept {
        if (this != &other) {
                close();
                _fd = other._fd;
                _path = std::move(other._path);
                _caps = other._caps;
                _taiUtcOffsetSec = other._taiUtcOffsetSec;
                other._fd = -1;
        }
        return *this;
}

PhcClock::~PhcClock() {
        close();
}

Result<PhcClock> PhcClock::open(const String &path) {
#if defined(PROMEKI_PLATFORM_LINUX)
        const int fd = ::open(path.cstr(), O_RDWR | O_CLOEXEC);
        if (fd < 0) {
                promekiWarn("PhcClock: open(%s) failed: %s", path.cstr(), strerror(errno));
                return makeError<PhcClock>(Error::OpenFailed);
        }
        const Caps caps = readCapsLocked(fd);
        const int  taiUtc = readTaiUtcOffsetSeconds();
        PhcClock   clock(fd, path, caps, taiUtc);
        return makeResult<PhcClock>(std::move(clock));
#else
        (void)path;
        return makeError<PhcClock>(Error::NotSupported);
#endif
}

void PhcClock::close() {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (_fd >= 0) {
                ::close(_fd);
                _fd = -1;
        }
#endif
}

Result<TimeStamp> PhcClock::now() const {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (_fd < 0) {
                return makeError<TimeStamp>(Error::NotOpen);
        }
        const clockid_t cid = static_cast<clockid_t>(phcFdToClockId(_fd));
        struct timespec ts = {};
        if (clock_gettime(cid, &ts) != 0) {
                promekiWarnThrottled(5000, "PhcClock::now: clock_gettime failed: %s",
                                     strerror(errno));
                return makeError<TimeStamp>(Error::IOError);
        }
        const int64_t ns = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL +
                           static_cast<int64_t>(ts.tv_nsec);
        return makeResult<TimeStamp>(TimeStamp(ns));
#else
        return makeError<TimeStamp>(Error::NotSupported);
#endif
}

Result<NtpTime> PhcClock::ntpNow() const {
        auto tsr = now();
        if (tsr.second().isError()) {
                return makeError<NtpTime>(tsr.second());
        }
        // Convert TAI nanoseconds (since Unix epoch) → UTC nanoseconds
        // (since Unix epoch) by subtracting the TAI-UTC offset, then
        // shift to the NTP epoch (1900-01-01) by adding NtpTime's
        // UnixEpochOffsetSeconds.  TAI≥UTC, so the subtraction is
        // safe; clamp to non-negative for paranoia.
        const int64_t taiNs = tsr.first().nanoseconds();
        const int64_t leapNs = static_cast<int64_t>(_taiUtcOffsetSec) * 1'000'000'000LL;
        int64_t       utcNs = taiNs - leapNs;
        if (utcNs < 0) utcNs = 0;
        const uint64_t utcUnixSec = static_cast<uint64_t>(utcNs / 1'000'000'000LL);
        const uint64_t utcSubNs = static_cast<uint64_t>(utcNs % 1'000'000'000LL);
        const uint64_t ntpSec = utcUnixSec + NtpTime::UnixEpochOffsetSeconds;
        const uint64_t frac = (utcSubNs << 32) / 1'000'000'000ULL;
        return makeResult<NtpTime>(
                NtpTime(static_cast<uint32_t>(ntpSec & 0xFFFFFFFFu),
                        static_cast<uint32_t>(frac & 0xFFFFFFFFu)));
}

Result<PhcClock::SysOffset> PhcClock::sysOffsetPrecise() const {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (_fd < 0) {
                return makeError<SysOffset>(Error::NotOpen);
        }
        if (!_caps.crossTimestamping) {
                return makeError<SysOffset>(Error::NotSupported);
        }
        struct ptp_sys_offset_precise off = {};
        if (ioctl(_fd, PTP_SYS_OFFSET_PRECISE, &off) != 0) {
                promekiWarnThrottled(5000, "PhcClock::sysOffsetPrecise: ioctl failed: %s",
                                     strerror(errno));
                return makeError<SysOffset>(Error::IOError);
        }
        SysOffset out;
        out.sysRealtimeNs = static_cast<int64_t>(off.sys_realtime.sec) * 1'000'000'000LL +
                            static_cast<int64_t>(off.sys_realtime.nsec);
        out.phcNs = static_cast<int64_t>(off.device.sec) * 1'000'000'000LL +
                    static_cast<int64_t>(off.device.nsec);
        out.sysRealtimePostNs = static_cast<int64_t>(off.sys_monoraw.sec) * 1'000'000'000LL +
                                static_cast<int64_t>(off.sys_monoraw.nsec);
        return makeResult<SysOffset>(out);
#else
        return makeError<SysOffset>(Error::NotSupported);
#endif
}

Error PhcClock::refreshTaiUtcOffset() {
#if defined(PROMEKI_PLATFORM_LINUX)
        _taiUtcOffsetSec = readTaiUtcOffsetSeconds();
        return Error::Ok;
#else
        return Error::NotSupported;
#endif
}

Error PhcClock::bindAsDomain(const ClockDomain::ID &domain) {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (_fd < 0) return Error::NotOpen;
        // Capture the @c fd and the TAI-UTC offset by value rather
        // than @c this by pointer — the @c PhcClock handle is
        // move-constructible, but the underlying file descriptor is
        // stable: moves transfer ownership of the same fd value, and
        // @c close() invalidates it via the kernel.  A read against a
        // closed @c fd yields @c EBADF, which the lambda surfaces as
        // the documented @c 0 sentinel without crashing.  A
        // subsequent re-bind from the moved-to instance overwrites
        // the provider, so the stale lambda is replaced on the next
        // open + bind cycle.
        const int fd = _fd;
        const int taiUtc = _taiUtcOffsetSec;
        ClockDomain::setNowProvider(
                domain,
                ClockDomain::WallClockProvider([fd, taiUtc]() -> int64_t {
                        const clockid_t cid = static_cast<clockid_t>(phcFdToClockId(fd));
                        struct timespec ts = {};
                        if (clock_gettime(cid, &ts) != 0) return 0;
                        const int64_t taiNs = static_cast<int64_t>(ts.tv_sec) * 1'000'000'000LL +
                                              static_cast<int64_t>(ts.tv_nsec);
                        const int64_t leapNs = static_cast<int64_t>(taiUtc) * 1'000'000'000LL;
                        const int64_t utcNs = taiNs - leapNs;
                        return utcNs > 0 ? utcNs : 0;
                }));
        return Error::Ok;
#else
        (void)domain;
        return Error::NotSupported;
#endif
}

void PhcClock::unbindDomain(const ClockDomain::ID &domain) {
        ClockDomain::setNowProvider(domain, ClockDomain::WallClockProvider());
}

int PhcClock::systemTaiUtcOffsetSeconds() {
#if defined(PROMEKI_PLATFORM_LINUX)
        return readTaiUtcOffsetSeconds();
#else
        // Static fallback — the IERS-published TAI-UTC offset has been
        // 37 seconds since 2017-01-01 and stays correct until the next
        // announced leap second.
        return 37;
#endif
}

uint64_t PhcClock::utcNsToTaiNs(int64_t utcNs) {
        if (utcNs <= 0) return 0;
        const int     taiUtcSec = systemTaiUtcOffsetSeconds();
        const int64_t leapNs = static_cast<int64_t>(taiUtcSec) * 1'000'000'000LL;
        const int64_t taiNs = utcNs + leapNs;
        return taiNs > 0 ? static_cast<uint64_t>(taiNs) : 0;
}

bool PhcClock::isLocked(int64_t toleranceNs) const {
#if defined(PROMEKI_PLATFORM_LINUX)
        if (_fd < 0) return false;
        if (!_caps.crossTimestamping) return false;
        auto off = sysOffsetPrecise();
        if (off.second().isError()) return false;
        const SysOffset &s = off.first();
        const int64_t leapNs = static_cast<int64_t>(_taiUtcOffsetSec) * 1'000'000'000LL;
        // Convert PHC TAI → UTC and compare against the host's
        // CLOCK_REALTIME at the same instant.  A locked PHC with a
        // phc2sys-disciplined host should be within microseconds.
        const int64_t phcUtcNs = s.phcNs - leapNs;
        int64_t       diff = s.sysRealtimeNs - phcUtcNs;
        if (diff < 0) diff = -diff;
        return diff <= toleranceNs;
#else
        (void)toleranceNs;
        return false;
#endif
}

PROMEKI_NAMESPACE_END
