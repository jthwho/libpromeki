/**
 * @file      phcclock.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/clockdomain.h>
#include <promeki/ntptime.h>
#include <promeki/phcclock.h>
#include <promeki/platform.h>

using namespace promeki;

namespace {

// Real-device tests skip cleanly when /dev/ptp0 isn't present (every
// CI host that doesn't run ptp4l, every dev workstation without a
// PHC-capable NIC).  /dev/ptp0 is a character device so we don't try
// to stat it — we probe by opening + closing through PhcClock itself.
bool phcDeviceAvailable() {
#if defined(PROMEKI_PLATFORM_LINUX)
        auto r = PhcClock::open(String("/dev/ptp0"));
        if (r.second().isError()) return false;
        return r.first().isOpen();
#else
        return false;
#endif
}

} // namespace

// ============================================================================
// PhcClock — open / close / move
// ============================================================================

TEST_CASE("PhcClock D2 — default-constructed is closed") {
        PhcClock clk;
        CHECK_FALSE(clk.isOpen());
        CHECK(clk.fileDescriptor() < 0);
}

TEST_CASE("PhcClock D2 — opening a nonexistent path fails with OpenFailed/NotSupported") {
        // On Linux the open() syscall is reached and yields OpenFailed
        // (ENOENT under the hood); on non-Linux platforms the wrapper
        // short-circuits to NotSupported.
        auto r = PhcClock::open(String("/dev/ptp-does-not-exist"));
        CHECK(r.second().isError());
#if defined(PROMEKI_PLATFORM_LINUX)
        CHECK(r.second() == Error::OpenFailed);
#else
        CHECK(r.second() == Error::NotSupported);
#endif
}

TEST_CASE("PhcClock D2 — closed clock returns NotOpen / NotSupported for reads") {
        PhcClock clk;
        auto     nowR = clk.now();
        CHECK(nowR.second().isError());
        auto ntpR = clk.ntpNow();
        CHECK(ntpR.second().isError());
        auto offR = clk.sysOffsetPrecise();
        CHECK(offR.second().isError());
        CHECK_FALSE(clk.isLocked());
        CHECK(clk.bindAsDomain(ClockDomain::Ptp) != Error::Ok);
}

TEST_CASE("PhcClock D2 — TAI-UTC offset has a sane default") {
        PhcClock clk;
        // The library compiles in the historically-correct value of 37
        // seconds (correct since 2017-01-01); a non-Linux build never
        // calls @c adjtimex but exposes the same default.
        CHECK(clk.taiUtcOffsetSeconds() == 37);
        clk.setTaiUtcOffsetSeconds(42);
        CHECK(clk.taiUtcOffsetSeconds() == 42);
}

#if defined(PROMEKI_PLATFORM_LINUX)
TEST_CASE("PhcClock D2 — bindAsDomain returns NotOpen on a closed clock") {
        PhcClock clk;
        CHECK(clk.bindAsDomain(ClockDomain::Ptp) == Error::NotOpen);
}
#else
TEST_CASE("PhcClock D2 — open returns NotSupported on non-Linux") {
        auto r = PhcClock::open();
        CHECK(r.second() == Error::NotSupported);
}
#endif

// ============================================================================
// Process-wide static helpers — Phase E40 LLTM TX-time deadline path.
// ============================================================================

TEST_CASE("PhcClock E40 — systemTaiUtcOffsetSeconds returns a sane integer") {
        // Either the kernel reports an authoritative offset (37 since
        // 2017-01-01) or the helper falls back to 37 on a host that
        // has never been told the TAI offset.  Either way the value
        // is in the IERS-published range.
        const int sec = PhcClock::systemTaiUtcOffsetSeconds();
        CHECK(sec >= 30);
        CHECK(sec <= 60);
}

TEST_CASE("PhcClock E40 — utcNsToTaiNs(0) returns 0 (no-deadline sentinel)") {
        CHECK(PhcClock::utcNsToTaiNs(0) == 0u);
        CHECK(PhcClock::utcNsToTaiNs(-1) == 0u);
        CHECK(PhcClock::utcNsToTaiNs(-1'000'000'000) == 0u);
}

TEST_CASE("PhcClock E40 — utcNsToTaiNs adds the system leap-second offset") {
        const int     sec = PhcClock::systemTaiUtcOffsetSeconds();
        const int64_t utcNs = 1'700'000'000'000'000'000LL; // arbitrary UTC instant
        const uint64_t taiNs = PhcClock::utcNsToTaiNs(utcNs);
        const int64_t  expected = utcNs + static_cast<int64_t>(sec) * 1'000'000'000LL;
        CHECK(taiNs == static_cast<uint64_t>(expected));
}

TEST_CASE("PhcClock E40 — utcNsToTaiNs is monotone in its input") {
        const uint64_t a = PhcClock::utcNsToTaiNs(1'000'000'000'000LL);
        const uint64_t b = PhcClock::utcNsToTaiNs(2'000'000'000'000LL);
        CHECK(b > a);
        CHECK(b - a == 1'000'000'000'000ULL);
}

// ============================================================================
// Real-device tests — skipped when /dev/ptp0 is absent.
// ============================================================================

TEST_CASE("PhcClock D2 — opens /dev/ptp0 when present" * doctest::skip(!phcDeviceAvailable())) {
        auto r = PhcClock::open(String("/dev/ptp0"));
        REQUIRE(r.second().isOk());
        const PhcClock &clk = r.first();
        CHECK(clk.isOpen());
        CHECK(clk.fileDescriptor() >= 0);
        CHECK(clk.devicePath() == "/dev/ptp0");
}

TEST_CASE("PhcClock D2 — now() returns a non-zero TAI nanosecond timestamp"
          * doctest::skip(!phcDeviceAvailable())) {
        auto r = PhcClock::open(String("/dev/ptp0"));
        REQUIRE(r.second().isOk());
        auto nowR = r.first().now();
        REQUIRE(nowR.second().isOk());
        CHECK(nowR.first().nanoseconds() > 0);
}

TEST_CASE("PhcClock D2 — ntpNow returns a non-epoch NtpTime"
          * doctest::skip(!phcDeviceAvailable())) {
        auto r = PhcClock::open(String("/dev/ptp0"));
        REQUIRE(r.second().isOk());
        auto ntpR = r.first().ntpNow();
        REQUIRE(ntpR.second().isOk());
        CHECK(ntpR.first().isValid());
        // 2020-01-01 in NTP seconds (rough lower bound — any device
        // running ptp4l will be well past this).
        constexpr uint32_t ntp2020 = 3786825600u;
        CHECK(ntpR.first().seconds() > ntp2020);
}

TEST_CASE("PhcClock D2 — bindAsDomain wires ClockDomain::Ptp"
          * doctest::skip(!phcDeviceAvailable())) {
        auto r = PhcClock::open(String("/dev/ptp0"));
        REQUIRE(r.second().isOk());
        PhcClock clk = std::move(r.first());
        REQUIRE(clk.bindAsDomain(ClockDomain::Ptp) == Error::Ok);
        CHECK(ClockDomain::hasNowProvider(ClockDomain::Ptp));
        const int64_t utc = ClockDomain::nowUtcNs(ClockDomain::Ptp);
        CHECK(utc > 0);
        clk.unbindDomain(ClockDomain::Ptp);
        CHECK_FALSE(ClockDomain::hasNowProvider(ClockDomain::Ptp));
}

TEST_CASE("PhcClock D2 — bindAsDomain UTC ≈ NtpTime::now within 1 s on a disciplined host"
          * doctest::skip(!phcDeviceAvailable())) {
        auto r = PhcClock::open(String("/dev/ptp0"));
        REQUIRE(r.second().isOk());
        PhcClock clk = std::move(r.first());
        REQUIRE(clk.bindAsDomain(ClockDomain::Ptp) == Error::Ok);

        const int64_t phcUtcNs = ClockDomain::nowUtcNs(ClockDomain::Ptp);
        REQUIRE(phcUtcNs > 0);

        // CLOCK_REALTIME via NtpTime::now() — convert to UTC ns since
        // the Unix epoch for direct comparison.
        const NtpTime  realNtp = NtpTime::now();
        const uint64_t realUnixSec = static_cast<uint64_t>(realNtp.seconds()) -
                                     NtpTime::UnixEpochOffsetSeconds;
        const int64_t  realNs = static_cast<int64_t>(realUnixSec) * 1'000'000'000LL +
                                static_cast<int64_t>(static_cast<uint64_t>(realNtp.fraction()) *
                                                     1'000'000'000ULL / 0x1'00000000ULL);
        const int64_t diff = phcUtcNs > realNs ? phcUtcNs - realNs : realNs - phcUtcNs;
        // 1 s tolerance — generous, but accommodates a host running
        // ptp4l without phc2sys (the CLOCK_REALTIME drift on such a
        // host can be hundreds of ms).
        CHECK(diff < 1'000'000'000LL);

        clk.unbindDomain(ClockDomain::Ptp);
}
