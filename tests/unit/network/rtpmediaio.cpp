/**
 * @file      rtpmediaio.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/application.h>
#include <promeki/rtpmediaio.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>

using namespace promeki;

TEST_CASE("RtpMediaIO::buildDefaultCname formats RFC 3550 user@host shape") {
        const String c = RtpMediaIO::buildDefaultCname(12345, 1, String("192.168.1.42"));
        CHECK(c == "promeki-12345-1@192.168.1.42");
}

TEST_CASE("RtpMediaIO::buildDefaultCname handles IPv6 bracketed host") {
        const String c = RtpMediaIO::buildDefaultCname(67890, 7, String("[2001:db8::42]"));
        CHECK(c == "promeki-67890-7@[2001:db8::42]");
}

TEST_CASE("RtpMediaIO::buildDefaultCname omits @ when host is empty") {
        // Final fallback path: every interface lookup failed AND
        // System::hostname() came back empty.  CNAME must still be
        // structurally valid (non-empty).
        const String c = RtpMediaIO::buildDefaultCname(42, 99, String());
        CHECK(c == "promeki-42-99");
}

TEST_CASE("RtpMediaIO::buildDefaultCname accepts hostname-style host") {
        // Non-IP host (FQDN fallback path) — passed through verbatim.
        const String c = RtpMediaIO::buildDefaultCname(1, 1, String("host.example.com"));
        CHECK(c == "promeki-1-1@host.example.com");
}

TEST_CASE("RtpMediaIO::objectId is unique per instance and monotonic") {
        // Each new RtpMediaIO bumps the process-local counter so two
        // instances in the same process get distinct CNAMEs even
        // before any destination is configured.
        RtpMediaIO a;
        RtpMediaIO b;
        RtpMediaIO c;
        CHECK(a.objectId() > 0);
        CHECK(b.objectId() == a.objectId() + 1);
        CHECK(c.objectId() == b.objectId() + 1);
}

TEST_CASE("RtpMediaIO::pickEgressHostForCname returns empty for null destination") {
        // Null destination + any-host fallback through firstNonLoopback
        // is environment-dependent; assert the contract that a routable
        // result *or* empty (never crash, never garbage) is returned.
        const String h = RtpMediaIO::pickEgressHostForCname(SocketAddress());
        // Either an empty string (no non-loopback iface available) or
        // a non-empty string formatted as either dotted-quad or
        // bracketed v6 — both are valid host portions for a CNAME.
        if (!h.isEmpty()) {
                const bool ipv4Like = h.contains(".");
                const bool ipv6Like = h.startsWith("[") && h.endsWith("]");
                CHECK((ipv4Like || ipv6Like));
        }
}

// ============================================================================
// AES67 §8.1 ptime formatting and parsing — RtpMediaIO::formatAes67Ptime /
// parseAes67PtimeUs
// ============================================================================

TEST_CASE("RtpMediaIO::formatAes67Ptime: AES67 Table 4 packet times at 48 kHz") {
        // The required common packet time at 48 kHz (AES67 §7.2.1)
        // is exactly 1 ms = 48 samples; the SDP value is the plain
        // integer string per AES67 §8.1.
        CHECK(RtpMediaIO::formatAes67Ptime(48, 48000) == "1");
        // Level B / ST 2110-30 §7 Table 2 — 125 µs = 6 samples at
        // 48 kHz.  Output is 0.125 ms (three decimals).
        CHECK(RtpMediaIO::formatAes67Ptime(6, 48000) == "0.125");
        // AES67 §7.2.2 recommended packet times.
        CHECK(RtpMediaIO::formatAes67Ptime(12, 48000) == "0.25");
        CHECK(RtpMediaIO::formatAes67Ptime(16, 48000) == "0.333");
        CHECK(RtpMediaIO::formatAes67Ptime(192, 48000) == "4");
}

TEST_CASE("RtpMediaIO::formatAes67Ptime: 96 kHz Level AX / BX packet times") {
        // 96 kHz / 1 ms = 96 samples (Level AX); 96 kHz / 125 µs =
        // 12 samples (Level BX).
        CHECK(RtpMediaIO::formatAes67Ptime(96, 96000) == "1");
        CHECK(RtpMediaIO::formatAes67Ptime(12, 96000) == "0.125");
}

TEST_CASE("RtpMediaIO::formatAes67Ptime: 44.1 kHz needs decimal precision") {
        // 1 ms at 44.1 kHz nominally rounds to 48 samples (AES67
        // §7.2.1 says 48 samples at 48 / 44.1 / 96 kHz for the 1 ms
        // case is wrong — Table 2 actually says 48 samples at 44.1
        // kHz too).  48 samples / 44100 Hz = 1.088 ms which rounds
        // to "1.088".  Receivers tolerant of the
        // round-to-nearest-sample rule per §8.1 will recover 48
        // samples from any value within half a sample period.
        CHECK(RtpMediaIO::formatAes67Ptime(48, 44100) == "1.088");
        // 6 samples at 44.1 kHz = 0.136 ms ("125 µs" cell in Table 2).
        CHECK(RtpMediaIO::formatAes67Ptime(6, 44100) == "0.136");
}

TEST_CASE("RtpMediaIO::formatAes67Ptime: defensive cases") {
        CHECK(RtpMediaIO::formatAes67Ptime(0, 48000) == "0");
        CHECK(RtpMediaIO::formatAes67Ptime(48, 0) == "0");
        CHECK(RtpMediaIO::formatAes67Ptime(-1, 48000) == "0");
}

TEST_CASE("RtpMediaIO::parseAes67PtimeUs: integer milliseconds") {
        CHECK(RtpMediaIO::parseAes67PtimeUs("1") == 1000);
        CHECK(RtpMediaIO::parseAes67PtimeUs("4") == 4000);
}

TEST_CASE("RtpMediaIO::parseAes67PtimeUs: decimal milliseconds") {
        CHECK(RtpMediaIO::parseAes67PtimeUs("0.125") == 125);
        CHECK(RtpMediaIO::parseAes67PtimeUs("0.25") == 250);
        CHECK(RtpMediaIO::parseAes67PtimeUs("0.333") == 333);
        // AES67 §8.1 Table 4 spells the 44.1 kHz / 1 ms case as 1.09;
        // our parser accepts the spec's two-decimal form and the
        // formatter's three-decimal form.
        CHECK(RtpMediaIO::parseAes67PtimeUs("1.09") == 1090);
        CHECK(RtpMediaIO::parseAes67PtimeUs("1.088") == 1088);
        // 44.1 kHz / 333 µs = 16 samples → 0.36 ms per Table 4.
        CHECK(RtpMediaIO::parseAes67PtimeUs("0.36") == 360);
}

TEST_CASE("RtpMediaIO::parseAes67PtimeUs: whitespace + malformed input") {
        CHECK(RtpMediaIO::parseAes67PtimeUs("  1  ") == 1000);
        // Empty / unparseable / non-positive values return 0 so the
        // caller keeps its configured default.
        CHECK(RtpMediaIO::parseAes67PtimeUs(String()) == 0);
        CHECK(RtpMediaIO::parseAes67PtimeUs("garbage") == 0);
        CHECK(RtpMediaIO::parseAes67PtimeUs("0") == 0);
        CHECK(RtpMediaIO::parseAes67PtimeUs("-1") == 0);
}

TEST_CASE("RtpMediaIO ptime round-trip: format → parse recovers sample count") {
        // The format/parse pair must round-trip exactly for every
        // ST 2110-30 §7 Table 2 conformance shape so a sender's SDP
        // can be re-parsed by a receiver and yield the same packet
        // shape.  We round-trip the sample count, not the string,
        // since §8.1 allows multiple equivalent decimal forms.
        struct Shape { int samples; int rateHz; };
        const Shape shapes[] = {
                {48, 48000},  // Level A   — 1 ms
                {96, 96000},  // Level AX  — 1 ms
                {6,  48000},  // Level B   — 125 µs
                {12, 96000},  // Level BX  — 125 µs
                // Level C / CX share the 6 / 12 samples cases (only
                // the channel-count band differs); covered above.
                {12, 48000},  // 250 µs    — AES67 recommended
                {16, 48000},  // 333 µs    — AES67 recommended
                {192, 48000}, // 4 ms      — AES67 recommended
                {48, 44100},  // 44.1 kHz / 1 ms — AES67 §7.1
        };
        for (const Shape &s : shapes) {
                const String  ptime = RtpMediaIO::formatAes67Ptime(s.samples, s.rateHz);
                const int     us    = RtpMediaIO::parseAes67PtimeUs(ptime);
                // Reverse: us × rate / 1e6 rounded to nearest sample
                // must match the original count (§8.1 half-sample
                // error guarantee).
                const int64_t recovered =
                        (static_cast<int64_t>(us) * s.rateHz + 500'000) / 1'000'000;
                CHECK(recovered == s.samples);
        }
}
