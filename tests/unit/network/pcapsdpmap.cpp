/**
 * @file      pcapsdpmap.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/pcapsdpmap.h>
#include <promeki/sdpsession.h>

using namespace promeki;

namespace {

SdpMediaDescription mediaLine(const String &type, uint16_t port, uint8_t pt, const String &rtpmap,
                              const String &conn) {
        SdpMediaDescription md;
        md.setMediaType(type);
        md.setPort(port);
        md.addPayloadType(pt);
        md.setAttribute("rtpmap", rtpmap);
        if(!conn.isEmpty()) md.setConnectionAddress(conn);
        return md;
}

} // namespace

TEST_CASE("PcapSdpMap: classifies a multi-essence ST 2110 SDP") {
        SdpSession sdp;
        sdp.setConnectionAddress("239.0.0.1/64"); // session-level fallback
        sdp.addMediaDescription(mediaLine("video", 5000, 96, "96 raw/90000", "239.10.10.1/64"));
        sdp.addMediaDescription(mediaLine("audio", 5002, 97, "97 L24/48000", "239.10.10.2/64"));
        sdp.addMediaDescription(mediaLine("video", 5004, 100, "100 smpte291/90000", "239.10.10.3/64"));

        PcapSdpMap map;
        REQUIRE(map.ingest(sdp).isOk());
        REQUIRE(map.flows().size() == 3);

        const PcapFlow *video = map.find(NetworkAddress(Ipv4Address(239, 10, 10, 1)), 5000);
        REQUIRE(video != nullptr);
        CHECK(video->kind == PcapFlowKind::Video);
        CHECK(video->payloadType == 96);
        CHECK(video->clockRate == 90000u);

        const PcapFlow *audio = map.find(NetworkAddress(Ipv4Address(239, 10, 10, 2)), 5002);
        REQUIRE(audio != nullptr);
        CHECK(audio->kind == PcapFlowKind::Audio);
        CHECK(audio->payloadType == 97);

        const PcapFlow *anc = map.find(NetworkAddress(Ipv4Address(239, 10, 10, 3)), 5004);
        REQUIRE(anc != nullptr);
        CHECK(anc->kind == PcapFlowKind::Anc);
        CHECK(anc->payloadType == 100);
        CHECK(anc->encoding == String("smpte291"));
}

TEST_CASE("PcapSdpMap: media line with no c= falls back to the session address") {
        SdpSession sdp;
        sdp.setConnectionAddress("239.5.5.5");
        sdp.addMediaDescription(mediaLine("video", 6000, 96, "96 smpte291/90000", ""));

        PcapSdpMap map;
        REQUIRE(map.ingest(sdp).isOk());
        const PcapFlow *f = map.find(NetworkAddress(Ipv4Address(239, 5, 5, 5)), 6000);
        REQUIRE(f != nullptr);
        CHECK(f->kind == PcapFlowKind::Anc);
}

TEST_CASE("PcapSdpMap: IPv6 connection address parses and matches") {
        SdpSession sdp;
        sdp.addMediaDescription(mediaLine("video", 7000, 96, "96 smpte291/90000", "ff3e::8000:1"));
        PcapSdpMap map;
        REQUIRE(map.ingest(sdp).isOk());
        auto [v6, e] = Ipv6Address::fromString("ff3e::8000:1");
        REQUIRE(e.isOk());
        const PcapFlow *f = map.find(NetworkAddress(v6), 7000);
        REQUIRE(f != nullptr);
        CHECK(f->kind == PcapFlowKind::Anc);
}

TEST_CASE("PcapSdpMap: a media line with an unparseable address is skipped") {
        SdpSession sdp;
        sdp.addMediaDescription(mediaLine("video", 8000, 96, "96 raw/90000", "not-an-ip"));
        sdp.addMediaDescription(mediaLine("video", 8002, 97, "97 raw/90000", "239.1.1.1"));
        PcapSdpMap map;
        REQUIRE(map.ingest(sdp).isOk());
        CHECK(map.flows().size() == 1); // only the parseable one survives
        CHECK(map.find(NetworkAddress(Ipv4Address(239, 1, 1, 1)), 8002) != nullptr);
}

TEST_CASE("PcapSdpMap: lookup miss returns nullptr") {
        SdpSession sdp;
        sdp.addMediaDescription(mediaLine("video", 5000, 96, "96 raw/90000", "239.1.2.3"));
        PcapSdpMap map;
        REQUIRE(map.ingest(sdp).isOk());
        CHECK(map.find(NetworkAddress(Ipv4Address(239, 1, 2, 3)), 9999) == nullptr); // wrong port
        CHECK(map.find(NetworkAddress(Ipv4Address(1, 2, 3, 4)), 5000) == nullptr);   // wrong addr
}
