/**
 * @file      pcapflowrouter.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/ancpacket.h>
#include <promeki/buffer.h>
#include <promeki/pcapflowrouter.h>
#include <promeki/rtppacket.h>
#include <promeki/rtppayloadanc.h>
#include <promeki/sdpsession.h>
#include <promeki/st291packet.h>

using namespace promeki;

namespace {

struct Bytes {
                std::vector<uint8_t> d;
                void u8(uint8_t v) { d.push_back(v); }
                void le32(uint32_t v) {
                        d.push_back(static_cast<uint8_t>(v));
                        d.push_back(static_cast<uint8_t>(v >> 8));
                        d.push_back(static_cast<uint8_t>(v >> 16));
                        d.push_back(static_cast<uint8_t>(v >> 24));
                }
                void le16(uint16_t v) {
                        d.push_back(static_cast<uint8_t>(v));
                        d.push_back(static_cast<uint8_t>(v >> 8));
                }
                void be16(uint16_t v) {
                        d.push_back(static_cast<uint8_t>(v >> 8));
                        d.push_back(static_cast<uint8_t>(v));
                }
                void append(const std::vector<uint8_t> &b) {
                        for(uint8_t x : b) d.push_back(x);
                }
};

// UDP header + payload (checksum 0; not validated by the demux).
std::vector<uint8_t> udp(uint16_t sport, uint16_t dport, const std::vector<uint8_t> &pay) {
        Bytes w;
        w.be16(sport);
        w.be16(dport);
        w.be16(static_cast<uint16_t>(8 + pay.size()));
        w.be16(0);
        w.append(pay);
        return w.d;
}

// IPv4 (no options, no fragmentation) carrying a UDP segment.
std::vector<uint8_t> ipv4Udp(const uint8_t dst[4], uint16_t sport, uint16_t dport,
                             const std::vector<uint8_t> &pay) {
        std::vector<uint8_t> seg = udp(sport, dport, pay);
        Bytes w;
        w.u8(0x45);
        w.u8(0);
        w.be16(static_cast<uint16_t>(20 + seg.size()));
        w.be16(1);
        w.be16(0);
        w.u8(64);
        w.u8(17);
        w.be16(0);
        w.u8(10);
        w.u8(0);
        w.u8(0);
        w.u8(1); // src 10.0.0.1
        w.u8(dst[0]);
        w.u8(dst[1]);
        w.u8(dst[2]);
        w.u8(dst[3]);
        w.append(seg);
        return w.d;
}

std::vector<uint8_t> ethIpv4Udp(const uint8_t dst[4], uint16_t dport, const std::vector<uint8_t> &pay) {
        Bytes w;
        for(int i = 0; i < 12; ++i) w.u8(0);
        w.be16(0x0800);
        w.append(ipv4Udp(dst, 5000, dport, pay));
        return w.d;
}

// Assemble a classic little-endian (microsecond) pcap over Ethernet frames.
Buffer classicPcap(const std::vector<std::vector<uint8_t>> &frames) {
        Bytes c;
        c.le32(0xa1b2c3d4); // magic (LE, microseconds)
        c.le16(2);          // version major
        c.le16(4);          // version minor
        c.le32(0);          // thiszone
        c.le32(0);          // sigfigs
        c.le32(65535);      // snaplen
        c.le32(1);          // network = Ethernet
        uint32_t ts = 1000;
        for(const std::vector<uint8_t> &f : frames) {
                c.le32(ts++);                              // ts_sec
                c.le32(0);                                 // ts_usec
                c.le32(static_cast<uint32_t>(f.size()));   // incl_len
                c.le32(static_cast<uint32_t>(f.size()));   // orig_len
                c.append(f);
        }
        Buffer b(c.d.size());
        std::memcpy(b.data(), c.d.data(), c.d.size());
        b.setSize(c.d.size());
        return b;
}

// Like classicPcap, but each frame carries an explicit capture time given
// as a microsecond offset from a fixed base second — for jitter tests that
// need irregular inter-arrival spacing (classicPcap's 1 s/frame cadence is
// perfectly regular, i.e. zero jitter).
Buffer classicPcapAt(const std::vector<std::pair<uint32_t, std::vector<uint8_t>>> &frames) {
        Bytes c;
        c.le32(0xa1b2c3d4);
        c.le16(2);
        c.le16(4);
        c.le32(0);
        c.le32(0);
        c.le32(65535);
        c.le32(1);
        for(const auto &fr : frames) {
                const uint32_t usOffset = fr.first;
                c.le32(1000 + usOffset / 1000000u); // ts_sec
                c.le32(usOffset % 1000000u);        // ts_usec
                c.le32(static_cast<uint32_t>(fr.second.size()));
                c.le32(static_cast<uint32_t>(fr.second.size()));
                c.append(fr.second);
        }
        Buffer b(c.d.size());
        std::memcpy(b.data(), c.d.data(), c.d.size());
        b.setSize(c.d.size());
        return b;
}

// Copy an RtpPacket's wire bytes out into a vector.
std::vector<uint8_t> rtpBytes(const RtpPacket &pkt) {
        std::vector<uint8_t> v(pkt.size());
        std::memcpy(v.data(), pkt.data(), pkt.size());
        return v;
}

// Build a frame's worth of ANC RTP packets (two ST 291 packets), stamped
// with the given payload type / SSRC, marker on the last packet.
RtpPacket::List ancRtpFrame(uint8_t pt, uint32_t ssrc, uint32_t rtpTs, uint16_t startSeq) {
        List<uint16_t> udw1 = {0x01, 0x02, 0x03, 0x04};
        List<uint16_t> udw2 = {0x10, 0x20};
        AncPacket::List in;
        in.pushToBack(St291Packet::buildRaw(0x41, 0x01, udw1, 9));
        in.pushToBack(St291Packet::buildRaw(0x43, 0x02, udw2, 11));

        RtpPayloadAnc packer(pt);
        RtpPacket::List rtps = packer.packAncFrame(in, rtpTs);
        uint16_t seq = startSeq;
        for(RtpPacket &r : rtps) {
                r.setVersion(2);
                r.setPayloadType(pt);
                r.setSsrc(ssrc);
                r.setSequenceNumber(seq++);
        }
        return rtps;
}

// A single ANC RTP packet (one ST 291 record, marker set) with fully
// caller-controlled pt / ssrc / timestamp / sequence number — so the RTP
// health tests can drive precise seq gaps, SSRC flips, etc.  One packet
// per RTP frame keeps the seq arithmetic unambiguous.
RtpPacket oneAncPkt(uint8_t pt, uint32_t ssrc, uint32_t rtpTs, uint16_t seq) {
        List<uint16_t> udw = {0x01, 0x02, 0x03, 0x04};
        AncPacket::List in;
        in.pushToBack(St291Packet::buildRaw(0x41, 0x01, udw, 9));
        RtpPayloadAnc   packer(pt);
        RtpPacket::List rtps = packer.packAncFrame(in, rtpTs);
        REQUIRE(rtps.size() == 1);
        RtpPacket r = rtps[0];
        r.setVersion(2);
        r.setPayloadType(pt);
        r.setSsrc(ssrc);
        r.setSequenceNumber(seq);
        r.setMarker(true); // one packet == one complete frame
        return r;
}

SdpSession ancSdp(const String &addr, uint16_t port, uint8_t pt) {
        SdpMediaDescription md;
        md.setMediaType("video");
        md.setPort(port);
        md.addPayloadType(pt);
        md.setAttribute("rtpmap", String::number(pt) + " smpte291/90000");
        md.setConnectionAddress(addr);
        SdpSession sdp;
        sdp.addMediaDescription(md);
        return sdp;
}

const uint8_t kDst[4] = {239, 10, 10, 3};

} // namespace

TEST_CASE("PcapFlowRouter: decodes an SDP-labelled ANC flow end to end") {
        RtpPacket::List rtps = ancRtpFrame(100, 0x11223344, 0xCAFEBABE, 1000);
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : rtps) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer cap = classicPcap(frames);

        PcapFlowRouter router;
        REQUIRE(router.setSdp(ancSdp("239.10.10.3", 5004, 100)).isOk());

        List<PcapFlowRouter::RoutedAncFrame> got;
        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &f) { got.pushToBack(f); });
        REQUIRE(router.processBuffer(cap).isOk());

        REQUIRE(got.size() == 1);
        const PcapFlowRouter::RoutedAncFrame &f = got[0];
        CHECK(f.anc.packets.size() == 2);          // the two ST 291 packets round-trip
        CHECK(f.anc.rtpTimestamp == 0xCAFEBABE);
        CHECK_FALSE(f.anc.keepAlive);
        CHECK(f.ssrc == 0x11223344);
        CHECK(f.dst.port() == 5004);
        CHECK(f.dst.address() == NetworkAddress(Ipv4Address(239, 10, 10, 3)));
        CHECK(f.captureTime.isValid());
}

TEST_CASE("PcapFlowRouter: a manually-designated ANC flow decodes without SDP") {
        RtpPacket::List rtps = ancRtpFrame(100, 0xABCDEF01, 0x1000, 1);
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : rtps) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer cap = classicPcap(frames);

        PcapFlowRouter router; // no SDP at all
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004)); // any payload type

        List<PcapFlowRouter::RoutedAncFrame> got;
        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &f) { got.pushToBack(f); });
        REQUIRE(router.processBuffer(cap).isOk());

        REQUIRE(got.size() == 1);
        CHECK(got[0].anc.packets.size() == 2);
        CHECK(got[0].anc.rtpTimestamp == 0x1000u);
        // The manually-added flow is also visible in the discovery table, labelled Anc.
        REQUIRE(router.flowStats().size() == 1);
        CHECK(router.flowStats()[0].kind == PcapFlowKind::Anc);
}

TEST_CASE("PcapFlowRouter: two RTP timestamps produce two ANC frames") {
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : ancRtpFrame(100, 0xAA, 1000, 100)) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        for(const RtpPacket &r : ancRtpFrame(100, 0xAA, 2000, 200)) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer cap = classicPcap(frames);

        PcapFlowRouter router;
        REQUIRE(router.setSdp(ancSdp("239.10.10.3", 5004, 100)).isOk());
        int count = 0;
        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &) { ++count; });
        REQUIRE(router.processBuffer(cap).isOk());
        CHECK(count == 2);
}

TEST_CASE("PcapFlowRouter: auto-discovery tallies flows with no SDP") {
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : ancRtpFrame(100, 0x55, 1000, 1)) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer cap = classicPcap(frames);

        PcapFlowRouter router; // no SDP set
        int ancFrames = 0;
        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &) { ++ancFrames; });
        REQUIRE(router.processBuffer(cap).isOk());

        CHECK(ancFrames == 0); // unlabelled → no ANC decode
        REQUIRE(router.flowStats().size() == 1);
        const PcapFlowRouter::FlowStat &s = router.flowStats()[0];
        CHECK(s.dst.port() == 5004);
        CHECK(s.ssrc == 0x55);
        CHECK(s.payloadType == 100);
        CHECK(s.kind == PcapFlowKind::Unknown);
        CHECK(s.packets >= 1);
}

TEST_CASE("PcapFlowRouter: payload-type mismatch is not decoded as ANC") {
        // Capture carries PT 99, but the SDP labels the flow as PT 100.
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : ancRtpFrame(99, 0x77, 1000, 1)) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer cap = classicPcap(frames);

        PcapFlowRouter router;
        REQUIRE(router.setSdp(ancSdp("239.10.10.3", 5004, 100)).isOk());
        int ancFrames = 0;
        router.onAncFrame([&](const PcapFlowRouter::RoutedAncFrame &) { ++ancFrames; });
        REQUIRE(router.processBuffer(cap).isOk());

        CHECK(ancFrames == 0);                    // PT mismatch → skipped
        CHECK(router.flowStats().size() == 1);    // but still tallied
}

// ---- RTP-level health / anomaly detection ----------------------------

namespace {

using Anomaly = PcapFlowRouter::RtpAnomaly;

// Run a list of pre-built RTP packets through a router on a manually
// designated (any-PT) ANC flow, collecting every anomaly emitted.
List<Anomaly> runAnomalies(const RtpPacket::List &pkts, PcapFlowRouter &router) {
        std::vector<std::vector<uint8_t>> frames;
        for(const RtpPacket &r : pkts) frames.push_back(ethIpv4Udp(kDst, 5004, rtpBytes(r)));
        Buffer        cap = classicPcap(frames);
        List<Anomaly> out;
        router.onRtpAnomaly([&](const Anomaly &a) { out.pushToBack(a); });
        REQUIRE(router.processBuffer(cap).isOk());
        return out;
}

size_t countKind(const List<Anomaly> &as, Anomaly::Kind k) {
        size_t n = 0;
        for(const Anomaly &a : as) if(a.kind == k) ++n;
        return n;
}

} // namespace

TEST_CASE("PcapFlowRouter: a clean sequential flow reports no RTP anomalies") {
        RtpPacket::List pkts;
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 1000, 10));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 2000, 11));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 3000, 12));

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        List<Anomaly> as = runAnomalies(pkts, router);

        CHECK(as.isEmpty());
        REQUIRE(router.flowStats().size() == 1);
        CHECK(router.flowStats()[0].lostPackets == 0);
        CHECK(router.flowStats()[0].duplicatePackets == 0);
}

TEST_CASE("PcapFlowRouter: an SSRC change is reported once with both SSRCs") {
        RtpPacket::List pkts;
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 1000, 0));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 2000, 1));
        pkts.pushToBack(oneAncPkt(100, 0xBBBB, 3000, 2)); // new source

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        List<Anomaly> as = runAnomalies(pkts, router);

        REQUIRE(countKind(as, Anomaly::Kind::SsrcChange) == 1);
        for(const Anomaly &a : as) {
                if(a.kind == Anomaly::Kind::SsrcChange) {
                        CHECK(a.previous == 0xAAAAu);
                        CHECK(a.ssrc == 0xBBBBu);
                        CHECK(a.flowKind == PcapFlowKind::Anc);
                }
        }
        // The SSRC discontinuity must not also register as loss.
        CHECK(countKind(as, Anomaly::Kind::PacketLoss) == 0);
}

TEST_CASE("PcapFlowRouter: a sequence gap is reported as packet loss") {
        RtpPacket::List pkts;
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 1000, 0));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 2000, 1));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 3000, 3)); // seq 2 missing

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        List<Anomaly> as = runAnomalies(pkts, router);

        REQUIRE(countKind(as, Anomaly::Kind::PacketLoss) == 1);
        for(const Anomaly &a : as) {
                if(a.kind == Anomaly::Kind::PacketLoss) CHECK(a.count == 1u);
        }
        REQUIRE(router.flowStats().size() == 1);
        CHECK(router.flowStats()[0].lostPackets == 1);
}

TEST_CASE("PcapFlowRouter: a payload-type change is reported") {
        RtpPacket::List pkts;
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 1000, 0));
        pkts.pushToBack(oneAncPkt(96, 0xAAAA, 2000, 1)); // PT changed, same source

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004)); // any PT
        List<Anomaly> as = runAnomalies(pkts, router);

        REQUIRE(countKind(as, Anomaly::Kind::PayloadTypeChange) == 1);
        for(const Anomaly &a : as) {
                if(a.kind == Anomaly::Kind::PayloadTypeChange) CHECK(a.previous == 100u);
        }
}

TEST_CASE("PcapFlowRouter: an RTP timestamp regression is reported") {
        RtpPacket::List pkts;
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 5000, 0));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 6000, 1));
        pkts.pushToBack(oneAncPkt(100, 0xAAAA, 4000, 2)); // timestamp jumps backward

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        List<Anomaly> as = runAnomalies(pkts, router);

        REQUIRE(countKind(as, Anomaly::Kind::TimestampRegression) == 1);
        CHECK(router.flowStats()[0].timestampRegressions == 1);
}

TEST_CASE("PcapFlowRouter: jitter exceeding the threshold raises a warning") {
        // One packet per frame, regular 2700-tick (30 ms @ 90 kHz) RTP step,
        // but irregular capture spacing → interarrival jitter.
        const uint32_t rtpStep = 2700;
        const std::vector<uint32_t> arrivalsUs = {0, 30000, 95000, 130000, 165000, 260000, 295000, 330000};
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> frames;
        for(size_t i = 0; i < arrivalsUs.size(); ++i) {
                RtpPacket p = oneAncPkt(100, 0xAAAA, 1000 + static_cast<uint32_t>(i) * rtpStep,
                                        static_cast<uint16_t>(i));
                frames.push_back({arrivalsUs[i], ethIpv4Udp(kDst, 5004, rtpBytes(p))});
        }
        Buffer cap = classicPcapAt(frames);

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        router.setJitterWarnThreshold(Duration::fromNanoseconds(5'000'000)); // 5 ms

        List<Anomaly> as;
        router.onRtpAnomaly([&](const Anomaly &a) { as.pushToBack(a); });
        REQUIRE(router.processBuffer(cap).isOk());

        REQUIRE(countKind(as, Anomaly::Kind::JitterExceeded) >= 1);
        for(const Anomaly &a : as) {
                if(a.kind == Anomaly::Kind::JitterExceeded) {
                        CHECK(a.jitter.nanoseconds() > 5'000'000); // above the 5 ms threshold
                }
        }
        // The peak jitter lands on the flow row as a Duration.
        REQUIRE(router.flowStats().size() == 1);
        CHECK(router.flowStats()[0].maxJitter.nanoseconds() > 5'000'000);
}

TEST_CASE("PcapFlowRouter: jitter below the threshold raises no warning") {
        const uint32_t rtpStep = 2700;
        const std::vector<uint32_t> arrivalsUs = {0, 30000, 95000, 130000, 165000, 260000, 295000, 330000};
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> frames;
        for(size_t i = 0; i < arrivalsUs.size(); ++i) {
                RtpPacket p = oneAncPkt(100, 0xAAAA, 1000 + static_cast<uint32_t>(i) * rtpStep,
                                        static_cast<uint16_t>(i));
                frames.push_back({arrivalsUs[i], ethIpv4Udp(kDst, 5004, rtpBytes(p))});
        }
        Buffer cap = classicPcapAt(frames);

        PcapFlowRouter router;
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));
        router.setJitterWarnThreshold(Duration::fromNanoseconds(1'000'000'000)); // 1 s — far above

        List<Anomaly> as;
        router.onRtpAnomaly([&](const Anomaly &a) { as.pushToBack(a); });
        REQUIRE(router.processBuffer(cap).isOk());

        CHECK(countKind(as, Anomaly::Kind::JitterExceeded) == 0);
}

TEST_CASE("PcapFlowRouter: a zero jitter threshold disables jitter warnings") {
        const uint32_t rtpStep = 2700;
        const std::vector<uint32_t> arrivalsUs = {0, 30000, 95000, 130000, 165000, 260000};
        std::vector<std::pair<uint32_t, std::vector<uint8_t>>> frames;
        for(size_t i = 0; i < arrivalsUs.size(); ++i) {
                RtpPacket p = oneAncPkt(100, 0xAAAA, 1000 + static_cast<uint32_t>(i) * rtpStep,
                                        static_cast<uint16_t>(i));
                frames.push_back({arrivalsUs[i], ethIpv4Udp(kDst, 5004, rtpBytes(p))});
        }
        Buffer cap = classicPcapAt(frames);

        PcapFlowRouter router; // threshold left at the zero default
        router.addAncFlow(SocketAddress(Ipv4Address(239, 10, 10, 3), 5004));

        List<Anomaly> as;
        router.onRtpAnomaly([&](const Anomaly &a) { as.pushToBack(a); });
        REQUIRE(router.processBuffer(cap).isOk());

        CHECK(countKind(as, Anomaly::Kind::JitterExceeded) == 0);
        // Jitter is still measured and reported on the row even when warnings
        // are off.
        CHECK(router.flowStats()[0].maxJitter.nanoseconds() > 0);
}
