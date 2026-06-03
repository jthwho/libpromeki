/**
 * @file      packetdemux.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstring>
#include <vector>
#include <doctest/doctest.h>
#include <promeki/buffer.h>
#include <promeki/packetdemux.h>

using namespace promeki;

namespace {

// Big-endian (network order) wire builder.
struct Wire {
                std::vector<uint8_t> d;
                void u8(uint8_t v) { d.push_back(v); }
                void be16(uint16_t v) {
                        d.push_back(static_cast<uint8_t>(v >> 8));
                        d.push_back(static_cast<uint8_t>(v));
                }
                void be32(uint32_t v) {
                        d.push_back(static_cast<uint8_t>(v >> 24));
                        d.push_back(static_cast<uint8_t>(v >> 16));
                        d.push_back(static_cast<uint8_t>(v >> 8));
                        d.push_back(static_cast<uint8_t>(v));
                }
                void bytes(std::initializer_list<uint8_t> b) {
                        for(uint8_t x : b) d.push_back(x);
                }
                void append(const std::vector<uint8_t> &b) {
                        for(uint8_t x : b) d.push_back(x);
                }
};

std::vector<uint8_t> udpSeg(uint16_t sport, uint16_t dport, const std::vector<uint8_t> &pay) {
        Wire w;
        w.be16(sport);
        w.be16(dport);
        w.be16(static_cast<uint16_t>(8 + pay.size()));
        w.be16(0); // checksum (not validated)
        w.append(pay);
        return w.d;
}

// IPv4 header (no options) + L4 bytes. fragField is the raw flags+offset word.
std::vector<uint8_t> ipv4(const uint8_t s[4], const uint8_t dd[4], uint8_t proto, uint16_t id, uint16_t fragField,
                          const std::vector<uint8_t> &l4) {
        Wire w;
        w.u8(0x45);
        w.u8(0);
        w.be16(static_cast<uint16_t>(20 + l4.size()));
        w.be16(id);
        w.be16(fragField);
        w.u8(64);    // TTL
        w.u8(proto);
        w.be16(0);   // header checksum (not validated)
        w.bytes({s[0], s[1], s[2], s[3]});
        w.bytes({dd[0], dd[1], dd[2], dd[3]});
        w.append(l4);
        return w.d;
}

std::vector<uint8_t> ipv6(const uint8_t s[16], const uint8_t dd[16], uint8_t nextHdr,
                          const std::vector<uint8_t> &afterBase) {
        Wire w;
        w.be32(0x60000000); // version 6
        w.be16(static_cast<uint16_t>(afterBase.size()));
        w.u8(nextHdr);
        w.u8(64); // hop limit
        for(int i = 0; i < 16; ++i) w.u8(s[i]);
        for(int i = 0; i < 16; ++i) w.u8(dd[i]);
        w.append(afterBase);
        return w.d;
}

std::vector<uint8_t> eth(uint16_t ethertype, const std::vector<uint8_t> &l3) {
        Wire w;
        for(int i = 0; i < 12; ++i) w.u8(0); // dst + src MAC
        w.be16(ethertype);
        w.append(l3);
        return w.d;
}

BufferView toFrame(const std::vector<uint8_t> &v) {
        Buffer b(v.size() == 0 ? 1 : v.size());
        std::memcpy(b.data(), v.data(), v.size());
        b.setSize(v.size());
        return BufferView(b, 0, v.size());
}

const uint8_t kSrc4[4] = {192, 168, 1, 10};
const uint8_t kDst4[4] = {239, 1, 2, 3};
const std::vector<uint8_t> kPayload = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34};

void checkPayload(const UdpDatagram &dg, const std::vector<uint8_t> &expect) {
        REQUIRE(dg.payload.size() == expect.size());
        const uint8_t *p = dg.payload.data();
        for(size_t i = 0; i < expect.size(); ++i) CHECK(p[i] == expect[i]);
}

} // namespace

TEST_CASE("PacketDemux: Ethernet / IPv4 / UDP") {
        auto frame = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload)));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(frame));
        REQUIRE(r.status == DemuxStatus::Ok);
        CHECK(r.datagram.src.port() == 5000);
        CHECK(r.datagram.dst.port() == 5004);
        CHECK(r.datagram.dst.address() == NetworkAddress(Ipv4Address(239, 1, 2, 3)));
        CHECK(r.datagram.ipProtocol == 17);
        CHECK_FALSE(r.datagram.reassembled);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: Ethernet with a single 802.1Q VLAN tag") {
        std::vector<uint8_t> l3 = ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload));
        Wire w;
        for(int i = 0; i < 12; ++i) w.u8(0);
        w.be16(PacketDemux::EtherTypeVlan);
        w.be16(100); // VID 100
        w.be16(PacketDemux::EtherTypeIpv4);
        w.append(l3);
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(w.d));
        REQUIRE(r.status == DemuxStatus::Ok);
        CHECK(r.datagram.dst.port() == 5004);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: Ethernet with stacked QinQ VLAN tags") {
        std::vector<uint8_t> l3 = ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload));
        Wire w;
        for(int i = 0; i < 12; ++i) w.u8(0);
        w.be16(PacketDemux::EtherTypeVlanS); // outer S-VLAN
        w.be16(10);
        w.be16(PacketDemux::EtherTypeVlan); // inner C-VLAN
        w.be16(20);
        w.be16(PacketDemux::EtherTypeIpv4);
        w.append(l3);
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(w.d));
        REQUIRE(r.status == DemuxStatus::Ok);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: Ethernet / IPv6 / UDP") {
        uint8_t s6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        uint8_t d6[16] = {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        auto frame = eth(PacketDemux::EtherTypeIpv6, ipv6(s6, d6, 17, udpSeg(6000, 6004, kPayload)));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(frame));
        REQUIRE(r.status == DemuxStatus::Ok);
        CHECK(r.datagram.dst.port() == 6004);
        CHECK(r.datagram.dst.address() == NetworkAddress(Ipv6Address(d6)));
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: IPv6 Hop-by-Hop extension header is walked") {
        uint8_t s6[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
        uint8_t d6[16] = {0x20, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        std::vector<uint8_t> udp = udpSeg(6000, 6004, kPayload);
        // Minimal Hop-by-Hop: nextHeader=UDP, hdrExtLen=0, 6 pad bytes.
        std::vector<uint8_t> hbh = {17, 0, 0, 0, 0, 0, 0, 0};
        std::vector<uint8_t> after = hbh;
        after.insert(after.end(), udp.begin(), udp.end());
        auto frame = eth(PacketDemux::EtherTypeIpv6, ipv6(s6, d6, 0 /* HBH */, after));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(frame));
        REQUIRE(r.status == DemuxStatus::Ok);
        CHECK(r.datagram.dst.port() == 6004);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: Linux cooked v1 (SLL)") {
        Wire w;
        w.be16(0);          // packet type
        w.be16(1);          // ARPHRD
        w.be16(6);          // ll addr length
        for(int i = 0; i < 8; ++i) w.u8(0); // ll addr
        w.be16(PacketDemux::EtherTypeIpv4);
        w.append(ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload)));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::LinuxSll, toFrame(w.d));
        REQUIRE(r.status == DemuxStatus::Ok);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: Linux cooked v2 (SLL2)") {
        Wire w;
        w.be16(PacketDemux::EtherTypeIpv4); // protocol type
        w.be16(0);                          // reserved
        w.be32(2);                          // interface index
        w.be16(1);                          // ARPHRD
        w.u8(0);                            // packet type
        w.u8(6);                            // ll addr length
        for(int i = 0; i < 8; ++i) w.u8(0); // ll addr
        w.append(ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload)));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::LinuxSll2, toFrame(w.d));
        REQUIRE(r.status == DemuxStatus::Ok);
        checkPayload(r.datagram, kPayload);
}

TEST_CASE("PacketDemux: raw IPv4 link type (no link header)") {
        auto frame = ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Raw, toFrame(frame));
        REQUIRE(r.status == DemuxStatus::Ok);
        checkPayload(r.datagram, kPayload);
        // The explicit IPV4 link type takes the same path.
        DemuxResult r2 = dx.demux(PcapLinkType::Ipv4, toFrame(frame));
        REQUIRE(r2.status == DemuxStatus::Ok);
}

TEST_CASE("PacketDemux: non-UDP frame reports NotUdp") {
        // protocol 6 == TCP
        auto frame = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 6, 1, 0, {0, 0, 0, 0}));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(frame));
        CHECK(r.status == DemuxStatus::NotUdp);
        CHECK(r.datagram.ipProtocol == 6);
}

TEST_CASE("PacketDemux: IPv4 fragmentation is reassembled") {
        std::vector<uint8_t> bigPay(16);
        for(size_t i = 0; i < bigPay.size(); ++i) bigPay[i] = static_cast<uint8_t>(0xA0 + i);
        std::vector<uint8_t> udp = udpSeg(7000, 7004, bigPay); // 8 + 16 = 24 bytes of L4

        std::vector<uint8_t> l4a(udp.begin(), udp.begin() + 16);     // offset 0, MF=1
        std::vector<uint8_t> l4b(udp.begin() + 16, udp.end());       // offset 16, MF=0
        auto f1 = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 0x4242, 0x2000, l4a));
        auto f2 = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 0x4242, 0x0002, l4b));

        PacketDemux dx;
        DemuxResult r1 = dx.demux(PcapLinkType::Ethernet, toFrame(f1));
        CHECK(r1.status == DemuxStatus::Fragment);
        CHECK(dx.pendingReassemblies() == 1);

        DemuxResult r2 = dx.demux(PcapLinkType::Ethernet, toFrame(f2));
        REQUIRE(r2.status == DemuxStatus::Ok);
        CHECK(r2.datagram.reassembled);
        CHECK(r2.datagram.src.port() == 7000);
        CHECK(r2.datagram.dst.port() == 7004);
        checkPayload(r2.datagram, bigPay);
        CHECK(dx.pendingReassemblies() == 0); // entry consumed
}

TEST_CASE("PacketDemux: out-of-order IPv4 fragments still reassemble") {
        std::vector<uint8_t> bigPay(16, 0x5A);
        std::vector<uint8_t> udp = udpSeg(7000, 7004, bigPay);
        std::vector<uint8_t> l4a(udp.begin(), udp.begin() + 16);
        std::vector<uint8_t> l4b(udp.begin() + 16, udp.end());
        auto f1 = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 0x99, 0x2000, l4a));
        auto f2 = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 0x99, 0x0002, l4b));
        PacketDemux dx;
        // Deliver the last fragment first.
        CHECK(dx.demux(PcapLinkType::Ethernet, toFrame(f2)).status == DemuxStatus::Fragment);
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(f1));
        REQUIRE(r.status == DemuxStatus::Ok);
        checkPayload(r.datagram, bigPay);
}

TEST_CASE("PacketDemux: a snap-clipped UDP datagram reports Truncated") {
        std::vector<uint8_t> pay(40, 0x77);
        auto frame = eth(PacketDemux::EtherTypeIpv4, ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, pay)));
        frame.resize(frame.size() - 20); // chop the tail as a capture snaplen would
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(frame));
        CHECK(r.status == DemuxStatus::Truncated);
}

TEST_CASE("PacketDemux: unsupported link type") {
        auto frame = ipv4(kSrc4, kDst4, 17, 1, 0, udpSeg(5000, 5004, kPayload));
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType(200 /* not handled */), toFrame(frame));
        CHECK(r.status == DemuxStatus::Unsupported);
}

TEST_CASE("PacketDemux: a frame too short for its link header is Truncated") {
        std::vector<uint8_t> tiny = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
        PacketDemux dx;
        DemuxResult r = dx.demux(PcapLinkType::Ethernet, toFrame(tiny));
        CHECK(r.status == DemuxStatus::Truncated);
}
