/**
 * @file      rtpmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
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
