/**
 * @file      rtcppacket.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/list.h>
#include <promeki/ntptime.h>
#include <promeki/rtcppacket.h>
#include <promeki/string.h>
#include <chrono>
#include <cstdint>
#include <cstring>

using namespace promeki;

namespace {

uint16_t readU16BE(const uint8_t *p) {
        return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t readU32BE(const uint8_t *p) {
        return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

} // namespace

TEST_CASE("NtpTime: epoch offset and round-trip") {
        // Pick a fixed system_clock instant: 2024-01-01 00:00:00 UTC.
        // Unix seconds = 1704067200.  NTP seconds = 1704067200 +
        // 2208988800 = 3913056000.
        const auto unixT = std::chrono::system_clock::time_point(std::chrono::seconds(1704067200LL));
        NtpTime    n = NtpTime::fromSystemClock(unixT);
        CHECK(n.seconds() == 3913056000u);
        CHECK(n.fraction() == 0u);
}

TEST_CASE("NtpTime: subsecond fraction encoding") {
        // 0.5 second past Unix epoch should have fraction = 2^31.
        const auto unixT = std::chrono::system_clock::time_point(std::chrono::nanoseconds(500'000'000));
        NtpTime    n = NtpTime::fromSystemClock(unixT);
        CHECK(n.seconds() == NtpTime::UnixEpochOffsetSeconds);
        CHECK(n.fraction() == 0x80000000u);
}

TEST_CASE("NtpTime: toUint64 packs seconds in high 32 bits") {
        NtpTime  n(0xAABBCCDDu, 0x11223344u);
        uint64_t u = n.toUint64();
        CHECK(u == 0xAABBCCDD11223344ull);
}

TEST_CASE("NtpTime: toCompact32 returns middle 32 bits") {
        NtpTime  n(0xAABBCCDDu, 0x11223344u);
        uint32_t c = n.toCompact32();
        // Low 16 of seconds (CCDD) joined with high 16 of fraction (1122).
        CHECK(c == 0xCCDD1122u);
}

TEST_CASE("NtpTime + Duration: positive whole-second offset") {
        NtpTime base(100, 0);
        NtpTime r = base + Duration::fromSeconds(5);
        CHECK(r.seconds() == 105u);
        CHECK(r.fraction() == 0u);
}

TEST_CASE("NtpTime + Duration: half-second offset") {
        NtpTime base(0, 0);
        NtpTime r = base + Duration::fromMicroseconds(500'000);
        CHECK(r.seconds() == 0u);
        // 0.5 second → fraction = 2^31.
        CHECK(r.fraction() == 0x80000000u);
}

TEST_CASE("NtpTime + Duration: negative offset moves back") {
        NtpTime base(100, 0);
        NtpTime r = base + Duration::fromSeconds(-5);
        CHECK(r.seconds() == 95u);
        CHECK(r.fraction() == 0u);
}

TEST_CASE("NtpTime + Duration: nanosecond precision survives a (steady,wall) round-trip") {
        // Simulate the RtpMediaIO anchor-seeding formula:
        //   captureNtp = wallNow + (captureSteady - steadyNow) +
        //                offset.
        // For a known input MediaTimeStamp ts that's @c steadyNow +
        // 12345 ns, captureNtp must equal wallNow + 12345 ns to
        // within sub-NTP-fraction precision.  This pins the
        // arithmetic the smoke-test path will run when frame 0
        // arrives.
        const NtpTime  wallNow(2'000'000'000u, 0u);
        const Duration delta = Duration::fromNanoseconds(12345);
        const NtpTime  r = wallNow + delta;
        // 12345 ns ≈ 12345 * 2^32 / 1e9 ≈ 53017 in 1/2^32 s units.
        CHECK(r.seconds() == 2'000'000'000u);
        const uint64_t expected = (static_cast<uint64_t>(12345) << 32) / 1'000'000'000u;
        CHECK(static_cast<uint64_t>(r.fraction()) == expected);
}

TEST_CASE("NtpTime - Duration: equivalent to + (-d)") {
        NtpTime base(500, 0x80000000u);
        NtpTime forward = base + Duration::fromMilliseconds(100);
        NtpTime back    = forward - Duration::fromMilliseconds(100);
        CHECK(back == base);
}

TEST_CASE("RtcpPacket: SR shape — 28 bytes, PT=200, RC=0, length=6") {
        NtpTime ntp(3913056000u, 0x80000000u);
        Buffer  sr = RtcpPacket::buildSenderReport(0xDEADBEEFu, ntp,
                                                   /*rtpTs=*/0x12345678u,
                                                   /*pkts=*/100u,
                                                   /*octs=*/12345u);
        REQUIRE(sr.size() == 28);
        const uint8_t *p = static_cast<const uint8_t *>(sr.data());

        // V=2, P=0, RC=0
        CHECK(p[0] == 0x80u);
        // PT=200 (SR)
        CHECK(p[1] == 200u);
        // length = (28/4) - 1 = 6
        CHECK(readU16BE(p + 2) == 6u);
        // SSRC
        CHECK(readU32BE(p + 4) == 0xDEADBEEFu);
        // NTP seconds
        CHECK(readU32BE(p + 8) == 3913056000u);
        // NTP fraction
        CHECK(readU32BE(p + 12) == 0x80000000u);
        // RTP timestamp
        CHECK(readU32BE(p + 16) == 0x12345678u);
        // Sender packet count
        CHECK(readU32BE(p + 20) == 100u);
        // Sender octet count
        CHECK(readU32BE(p + 24) == 12345u);
}

TEST_CASE("RtcpPacket: SDES with CNAME shape — header + chunk + null + pad") {
        // CNAME = "ab" (2 bytes).  Chunk layout:
        //   4 (SSRC) + 1 (type=1) + 1 (len=2) + 2 (CNAME) + 1 (NUL terminator)
        //   + pad to align: total chunk-after-SSRC bytes = 2+2+1 = 5 → pad
        //   to 8 → 3 padding zeros (totaling 4 zero bytes after CNAME).
        // Total packet: 4 (header) + 4 (SSRC) + 8 (chunk tail) = 16 bytes.
        Buffer sdes = RtcpPacket::buildSourceDescriptionCname(0xCAFEBABEu, String("ab"));
        REQUIRE(sdes.size() == 16);
        const uint8_t *p = static_cast<const uint8_t *>(sdes.data());

        // V=2, P=0, SC=1
        CHECK(p[0] == 0x81u);
        // PT=202 (SDES)
        CHECK(p[1] == 202u);
        // length = (16/4) - 1 = 3
        CHECK(readU16BE(p + 2) == 3u);
        // SSRC
        CHECK(readU32BE(p + 4) == 0xCAFEBABEu);
        // CNAME item: type=1, length=2
        CHECK(p[8] == 1u);
        CHECK(p[9] == 2u);
        // CNAME data
        CHECK(p[10] == 'a');
        CHECK(p[11] == 'b');
        // NUL terminator + padding zeros
        CHECK(p[12] == 0u);
        CHECK(p[13] == 0u);
        CHECK(p[14] == 0u);
        CHECK(p[15] == 0u);
}

TEST_CASE("RtcpPacket: SDES with empty CNAME") {
        // type + len + (no data) + NUL + pad.  itemBytes = 2; need 1
        // NUL; total chunk tail bytes = 2 + 1 + pad-to-4 = 4.  Total
        // packet = 4 (header) + 4 (SSRC) + 4 (chunk tail) = 12 bytes.
        Buffer sdes = RtcpPacket::buildSourceDescriptionCname(0x12345678u, String());
        REQUIRE(sdes.size() == 12);
        const uint8_t *p = static_cast<const uint8_t *>(sdes.data());
        CHECK(p[0] == 0x81u);
        CHECK(p[1] == 202u);
        CHECK(readU16BE(p + 2) == 2u);
        CHECK(readU32BE(p + 4) == 0x12345678u);
        CHECK(p[8] == 1u);  // type = CNAME
        CHECK(p[9] == 0u);  // length = 0
        CHECK(p[10] == 0u); // NUL terminator
        CHECK(p[11] == 0u); // pad
}

TEST_CASE("RtcpPacket: parseHeader extracts version/RC/PT/length") {
        // SR header bytes: V=2, P=0, RC=0, PT=200, length=6 (28 bytes).
        const uint8_t bytes[4] = {0x80u, 200u, 0x00u, 0x06u};
        RtcpPacket::Header h = RtcpPacket::parseHeader(bytes, sizeof(bytes));
        CHECK(h.isValid());
        CHECK(h.version == 2u);
        CHECK(h.padding == false);
        CHECK(h.rc == 0u);
        CHECK(h.pt == 200u);
        CHECK(h.lengthBytes == 28u);
}

TEST_CASE("RtcpPacket: parseHeader rejects truncated input") {
        const uint8_t bytes[3] = {0x80u, 200u, 0x00u};
        RtcpPacket::Header h = RtcpPacket::parseHeader(bytes, sizeof(bytes));
        CHECK(h.isValid() == false);
        CHECK(h.version == 0u);
        CHECK(h.lengthBytes == 0u);
}

TEST_CASE("RtcpPacket: parseHeader rejects non-V=2 packets") {
        // V=0, P=0, RC=0, PT=200.
        const uint8_t bytes[4] = {0x00u, 200u, 0x00u, 0x06u};
        RtcpPacket::Header h = RtcpPacket::parseHeader(bytes, sizeof(bytes));
        CHECK(h.isValid() == false);
        CHECK(h.version == 0u);
}

TEST_CASE("RtcpPacket: parseSenderReport round-trips builder output") {
        const NtpTime ntp(3913056000u, 0x80000000u);
        Buffer        sr = RtcpPacket::buildSenderReport(0xDEADBEEFu, ntp,
                                                         /*rtpTs=*/0x12345678u,
                                                         /*pkts=*/100u,
                                                         /*octs=*/12345u);
        RtcpPacket::SenderReportInfo info;
        bool ok = RtcpPacket::parseSenderReport(static_cast<const uint8_t *>(sr.data()), sr.size(), &info);
        REQUIRE(ok);
        CHECK(info.ssrc == 0xDEADBEEFu);
        CHECK(info.ntp == ntp);
        CHECK(info.rtpTimestamp == 0x12345678u);
        CHECK(info.senderPacketCount == 100u);
        CHECK(info.senderOctetCount == 12345u);
}

TEST_CASE("RtcpPacket: parseSenderReport rejects wrong PT") {
        Buffer sdes = RtcpPacket::buildSourceDescriptionCname(0x12345678u, String("a"));
        RtcpPacket::SenderReportInfo info;
        bool ok = RtcpPacket::parseSenderReport(static_cast<const uint8_t *>(sdes.data()), sdes.size(), &info);
        CHECK(ok == false);
}

TEST_CASE("RtcpPacket: parseSenderReport rejects truncated SR") {
        // Build a real SR but lie about the size — only first 16 bytes
        // are visible, which can't cover the 28-byte sender info block.
        Buffer sr = RtcpPacket::buildSenderReport(0x1u, NtpTime(1u, 0u), 0u, 0u, 0u);
        RtcpPacket::SenderReportInfo info;
        bool ok = RtcpPacket::parseSenderReport(static_cast<const uint8_t *>(sr.data()), 16, &info);
        CHECK(ok == false);
}

TEST_CASE("RtcpPacket: findSenderReports walks compound SR + SDES") {
        const NtpTime ntp(3913056000u, 0x80000000u);
        Buffer        sr = RtcpPacket::buildSenderReport(0xCAFEBABEu, ntp,
                                                         /*rtpTs=*/42u,
                                                         /*pkts=*/7u,
                                                         /*octs=*/8u);
        Buffer        sdes = RtcpPacket::buildSourceDescriptionCname(0xCAFEBABEu, String("test"));
        List<Buffer>  parts;
        parts.pushToBack(sr);
        parts.pushToBack(sdes);
        Buffer compound = RtcpPacket::compound(parts);

        List<RtcpPacket::SenderReportInfo> srs =
                RtcpPacket::findSenderReports(static_cast<const uint8_t *>(compound.data()), compound.size());
        REQUIRE(srs.size() == 1);
        CHECK(srs[0].ssrc == 0xCAFEBABEu);
        CHECK(srs[0].ntp == ntp);
        CHECK(srs[0].rtpTimestamp == 42u);
        CHECK(srs[0].senderPacketCount == 7u);
        CHECK(srs[0].senderOctetCount == 8u);
}

TEST_CASE("RtcpPacket: findSenderReports skips unknown packet types") {
        // Build a compound consisting of an SDES (no SR) — no SRs to
        // surface, but the walk must terminate cleanly without
        // throwing or returning malformed data.
        Buffer       sdes = RtcpPacket::buildSourceDescriptionCname(0x1u, String("x"));
        List<Buffer> parts;
        parts.pushToBack(sdes);
        Buffer compound = RtcpPacket::compound(parts);
        List<RtcpPacket::SenderReportInfo> srs =
                RtcpPacket::findSenderReports(static_cast<const uint8_t *>(compound.data()), compound.size());
        CHECK(srs.size() == 0);
}

TEST_CASE("RtcpPacket: findSenderReports stops on a sub-packet length overrun") {
        // Construct a fake compound where the second sub-packet
        // claims a length larger than the buffer can supply.  The
        // walker must drop the malformed tail without surfacing a
        // partially-parsed SR.
        Buffer sr = RtcpPacket::buildSenderReport(0xAAu, NtpTime(1u, 0u), 1u, 1u, 1u);
        const size_t totalSize = sr.size() + 4;
        Buffer       compound(totalSize);
        std::memcpy(compound.data(), sr.data(), sr.size());
        // Lie a bit: place a header with V=2/PT=200/length=99 (way
        // past end of buffer).  The walker should bail.
        uint8_t *tail = static_cast<uint8_t *>(compound.data()) + sr.size();
        tail[0] = 0x80u;
        tail[1] = 200u;
        tail[2] = 0x00u;
        tail[3] = 99u;
        compound.setSize(totalSize);
        List<RtcpPacket::SenderReportInfo> srs =
                RtcpPacket::findSenderReports(static_cast<const uint8_t *>(compound.data()), compound.size());
        // Only the well-formed first SR makes it out.
        REQUIRE(srs.size() == 1);
        CHECK(srs[0].ssrc == 0xAAu);
}

TEST_CASE("RtcpPacket: findSenderReports surfaces multiple SRs in one compound") {
        Buffer sr1 = RtcpPacket::buildSenderReport(0xA1u, NtpTime(100u, 0u), 1u, 0u, 0u);
        Buffer sr2 = RtcpPacket::buildSenderReport(0xB2u, NtpTime(200u, 0u), 2u, 0u, 0u);
        List<Buffer> parts;
        parts.pushToBack(sr1);
        parts.pushToBack(sr2);
        Buffer compound = RtcpPacket::compound(parts);
        List<RtcpPacket::SenderReportInfo> srs =
                RtcpPacket::findSenderReports(static_cast<const uint8_t *>(compound.data()), compound.size());
        REQUIRE(srs.size() == 2);
        CHECK(srs[0].ssrc == 0xA1u);
        CHECK(srs[0].rtpTimestamp == 1u);
        CHECK(srs[1].ssrc == 0xB2u);
        CHECK(srs[1].rtpTimestamp == 2u);
}

TEST_CASE("RtcpPacket: compound concatenates packets") {
        Buffer a(4);
        Buffer b(8);
        std::memset(a.data(), 0xAA, 4);
        std::memset(b.data(), 0xBB, 8);
        a.setSize(4);
        b.setSize(8);
        List<Buffer> parts;
        parts.pushToBack(a);
        parts.pushToBack(b);
        Buffer compound = RtcpPacket::compound(parts);
        REQUIRE(compound.size() == 12);
        const uint8_t *p = static_cast<const uint8_t *>(compound.data());
        for (int i = 0; i < 4; i++) CHECK(p[i] == 0xAAu);
        for (int i = 4; i < 12; i++) CHECK(p[i] == 0xBBu);
}

TEST_CASE("RtcpPacket: RR shape — header + sender SSRC + zero blocks") {
        List<RtcpPacket::ReportBlock> blocks;
        Buffer                        rr = RtcpPacket::buildReceiverReport(0x12345678u, blocks);
        REQUIRE(rr.size() == 8u);
        const uint8_t *p = static_cast<const uint8_t *>(rr.data());
        // V=2, P=0, RC=0
        CHECK(p[0] == 0x80u);
        // PT=201
        CHECK(p[1] == RtcpPacket::ReceiverReport);
        // length = (8/4) - 1 = 1
        CHECK(p[2] == 0x00u);
        CHECK(p[3] == 0x01u);
        // sender SSRC
        CHECK(((static_cast<uint32_t>(p[4]) << 24) | (static_cast<uint32_t>(p[5]) << 16) |
               (static_cast<uint32_t>(p[6]) << 8) | p[7]) == 0x12345678u);
}

TEST_CASE("RtcpPacket: RR with one report block matches RFC 3550 §6.4.2 layout") {
        RtcpPacket::ReportBlock b;
        b.ssrc = 0xAABBCCDDu;
        b.fractionLost = 0x42u;
        b.cumulativeLost = -3;
        b.extendedHighestSeq = 0x00010032u;
        b.interarrivalJitter = 0x01020304u;
        b.lsr = 0x40000000u;
        b.dlsr = 0x00010000u;
        List<RtcpPacket::ReportBlock> blocks;
        blocks.pushToBack(b);
        Buffer rr = RtcpPacket::buildReceiverReport(0xDEADBEEFu, blocks);
        REQUIRE(rr.size() == 32u); // 8 + 24
        const uint8_t *p = static_cast<const uint8_t *>(rr.data());
        // V=2, P=0, RC=1
        CHECK(p[0] == 0x81u);
        CHECK(p[1] == RtcpPacket::ReceiverReport);
        // length = (32/4) - 1 = 7
        CHECK(p[2] == 0x00u);
        CHECK(p[3] == 0x07u);
        // Sender SSRC at offset 4
        CHECK(p[4] == 0xDEu);
        CHECK(p[5] == 0xADu);
        CHECK(p[6] == 0xBEu);
        CHECK(p[7] == 0xEFu);
        // Block SSRC at offset 8
        CHECK(p[8] == 0xAAu);
        CHECK(p[9] == 0xBBu);
        CHECK(p[10] == 0xCCu);
        CHECK(p[11] == 0xDDu);
        // fractionLost
        CHECK(p[12] == 0x42u);
        // cumulativeLost = -3 -> 0xFFFFFD in 24-bit two's complement
        CHECK(p[13] == 0xFFu);
        CHECK(p[14] == 0xFFu);
        CHECK(p[15] == 0xFDu);
        // extendedHighestSeq
        CHECK(p[16] == 0x00u);
        CHECK(p[17] == 0x01u);
        CHECK(p[18] == 0x00u);
        CHECK(p[19] == 0x32u);
        // interarrivalJitter
        CHECK(p[20] == 0x01u);
        CHECK(p[21] == 0x02u);
        CHECK(p[22] == 0x03u);
        CHECK(p[23] == 0x04u);
        // lsr
        CHECK(p[24] == 0x40u);
        CHECK(p[25] == 0x00u);
        CHECK(p[26] == 0x00u);
        CHECK(p[27] == 0x00u);
        // dlsr
        CHECK(p[28] == 0x00u);
        CHECK(p[29] == 0x01u);
        CHECK(p[30] == 0x00u);
        CHECK(p[31] == 0x00u);
}

TEST_CASE("RtcpPacket: RR truncates excess blocks at RC=31") {
        List<RtcpPacket::ReportBlock> blocks;
        for (int i = 0; i < 40; i++) {
                RtcpPacket::ReportBlock b;
                b.ssrc = static_cast<uint32_t>(i + 1);
                blocks.pushToBack(b);
        }
        Buffer rr = RtcpPacket::buildReceiverReport(0u, blocks);
        // Header + sender SSRC + 31 × 24 = 8 + 744 = 752
        REQUIRE(rr.size() == 752u);
        const uint8_t *p = static_cast<const uint8_t *>(rr.data());
        CHECK((p[0] & 0x1Fu) == 31u);
}

TEST_CASE("RtcpPacket: BYE shape — 8 bytes, PT=203, RC=1") {
        Buffer bye = RtcpPacket::buildBye(0x55667788u);
        REQUIRE(bye.size() == 8u);
        const uint8_t *p = static_cast<const uint8_t *>(bye.data());
        CHECK(p[0] == 0x81u); // V=2, P=0, RC=1
        CHECK(p[1] == RtcpPacket::Goodbye);
        CHECK(p[2] == 0x00u);
        CHECK(p[3] == 0x01u);
        CHECK(p[4] == 0x55u);
        CHECK(p[5] == 0x66u);
        CHECK(p[6] == 0x77u);
        CHECK(p[7] == 0x88u);
}

TEST_CASE("RtcpPacket: findByeSources surfaces every BYE SSRC") {
        Buffer       sr = RtcpPacket::buildSenderReport(0xAAu, NtpTime(1u, 0u), 1u, 0u, 0u);
        Buffer       bye = RtcpPacket::buildBye(0xBBu);
        List<Buffer> parts;
        parts.pushToBack(sr);
        parts.pushToBack(bye);
        Buffer         compound = RtcpPacket::compound(parts);
        List<uint32_t> ssrcs = RtcpPacket::findByeSources(
                static_cast<const uint8_t *>(compound.data()), compound.size());
        REQUIRE(ssrcs.size() == 1u);
        CHECK(ssrcs[0] == 0xBBu);
}
