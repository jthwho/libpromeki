/**
 * @file      tests/net/sdpsession.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/network/sdpsession.h>

using namespace promeki;

TEST_CASE("SdpSession") {

        SUBCASE("default construction") {
                SdpSession sdp;
                CHECK(sdp.sessionName().isEmpty());
                CHECK(sdp.sessionId() == 0);
                CHECK(sdp.sessionVersion() == 0);
                CHECK(sdp.connectionAddress().isEmpty());
                CHECK(sdp.mediaDescriptions().isEmpty());
        }

        SUBCASE("set session properties") {
                SdpSession sdp;
                sdp.setSessionName("Test Stream");
                sdp.setOrigin("vidgen", 12345, 1, "IN", "IP4", "192.168.1.1");
                sdp.setConnectionAddress("239.0.0.1");

                CHECK(sdp.sessionName() == "Test Stream");
                CHECK(sdp.originUsername() == "vidgen");
                CHECK(sdp.sessionId() == 12345);
                CHECK(sdp.sessionVersion() == 1);
                CHECK(sdp.originNetType() == "IN");
                CHECK(sdp.originAddrType() == "IP4");
                CHECK(sdp.originAddress() == "192.168.1.1");
                CHECK(sdp.connectionAddress() == "239.0.0.1");
        }

        SUBCASE("add media descriptions") {
                SdpSession sdp;

                SdpMediaDescription video;
                video.setMediaType("video");
                video.setPort(5004);
                video.setProtocol("RTP/AVP");
                video.addPayloadType(96);
                video.setAttribute("rtpmap", "96 raw/90000");

                SdpMediaDescription audio;
                audio.setMediaType("audio");
                audio.setPort(5006);
                audio.setProtocol("RTP/AVP");
                audio.addPayloadType(97);
                audio.setAttribute("rtpmap", "97 L24/48000/2");
                audio.setAttribute("ptime", "1");

                sdp.addMediaDescription(video);
                sdp.addMediaDescription(audio);

                CHECK(sdp.mediaDescriptions().size() == 2);
                CHECK(sdp.mediaDescriptions()[0].mediaType() == "video");
                CHECK(sdp.mediaDescriptions()[0].port() == 5004);
                CHECK(sdp.mediaDescriptions()[1].mediaType() == "audio");
                CHECK(sdp.mediaDescriptions()[1].port() == 5006);
        }

        SUBCASE("toString generates valid SDP") {
                SdpSession sdp;
                sdp.setSessionName("vidgen");
                sdp.setOrigin("-", 100, 1);
                sdp.setConnectionAddress("239.0.0.1");

                SdpMediaDescription video;
                video.setMediaType("video");
                video.setPort(5004);
                video.setProtocol("RTP/AVP");
                video.addPayloadType(96);
                video.setAttribute("rtpmap", "96 raw/90000");
                sdp.addMediaDescription(video);

                String text = sdp.toString();

                // Verify required SDP fields are present
                CHECK(text.str().find("v=0") != std::string::npos);
                CHECK(text.str().find("o=-") != std::string::npos);
                CHECK(text.str().find("s=vidgen") != std::string::npos);
                CHECK(text.str().find("c=IN IP4 239.0.0.1") != std::string::npos);
                CHECK(text.str().find("t=0 0") != std::string::npos);
                CHECK(text.str().find("m=video 5004 RTP/AVP 96") != std::string::npos);
                CHECK(text.str().find("a=rtpmap:96 raw/90000") != std::string::npos);
        }

        SUBCASE("fromString parses SDP") {
                String sdpText =
                        "v=0\r\n"
                        "o=vidgen 12345 1 IN IP4 192.168.1.1\r\n"
                        "s=Test Stream\r\n"
                        "c=IN IP4 239.0.0.1\r\n"
                        "t=0 0\r\n"
                        "m=video 5004 RTP/AVP 96\r\n"
                        "a=rtpmap:96 raw/90000\r\n"
                        "m=audio 5006 RTP/AVP 97\r\n"
                        "a=rtpmap:97 L24/48000/2\r\n"
                        "a=ptime:1\r\n";

                auto [sdp, err] = SdpSession::fromString(sdpText);
                CHECK(err.isOk());

                CHECK(sdp.sessionName() == "Test Stream");
                CHECK(sdp.originUsername() == "vidgen");
                CHECK(sdp.sessionId() == 12345);
                CHECK(sdp.sessionVersion() == 1);
                CHECK(sdp.connectionAddress() == "239.0.0.1");

                REQUIRE(sdp.mediaDescriptions().size() == 2);

                const auto &video = sdp.mediaDescriptions()[0];
                CHECK(video.mediaType() == "video");
                CHECK(video.port() == 5004);
                CHECK(video.protocol() == "RTP/AVP");
                CHECK(video.payloadTypes().size() == 1);
                CHECK(video.payloadTypes()[0] == 96);
                CHECK(video.attribute("rtpmap") == "96 raw/90000");

                const auto &audio = sdp.mediaDescriptions()[1];
                CHECK(audio.mediaType() == "audio");
                CHECK(audio.port() == 5006);
                CHECK(audio.attribute("rtpmap") == "97 L24/48000/2");
                CHECK(audio.attribute("ptime") == "1");
        }

        SUBCASE("round-trip: toString then fromString") {
                SdpSession original;
                original.setSessionName("roundtrip test");
                original.setOrigin("test", 999, 2, "IN", "IP4", "10.0.0.1");
                original.setConnectionAddress("239.1.2.3");

                SdpMediaDescription md;
                md.setMediaType("video");
                md.setPort(5004);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(96);
                md.setAttribute("rtpmap", "96 raw/90000");
                md.setAttribute("fmtp", "96 width=1920; height=1080");
                original.addMediaDescription(md);

                String text = original.toString();
                auto [parsed, err] = SdpSession::fromString(text);
                CHECK(err.isOk());

                CHECK(parsed.sessionName() == "roundtrip test");
                CHECK(parsed.originUsername() == "test");
                CHECK(parsed.sessionId() == 999);
                CHECK(parsed.connectionAddress() == "239.1.2.3");
                REQUIRE(parsed.mediaDescriptions().size() == 1);
                CHECK(parsed.mediaDescriptions()[0].mediaType() == "video");
                CHECK(parsed.mediaDescriptions()[0].port() == 5004);
                CHECK(parsed.mediaDescriptions()[0].attribute("rtpmap") == "96 raw/90000");
        }

        SUBCASE("AES67 SDP example") {
                String aes67Sdp =
                        "v=0\r\n"
                        "o=- 1234567890 1 IN IP4 192.168.0.10\r\n"
                        "s=AES67 Audio Stream\r\n"
                        "c=IN IP4 239.69.0.1/32\r\n"
                        "t=0 0\r\n"
                        "m=audio 5004 RTP/AVP 97\r\n"
                        "a=rtpmap:97 L24/48000/2\r\n"
                        "a=ptime:1\r\n"
                        "a=mediaclk:direct=0\r\n";

                auto [sdp, err] = SdpSession::fromString(aes67Sdp);
                CHECK(err.isOk());
                CHECK(sdp.sessionName() == "AES67 Audio Stream");
                // TTL suffix stripped from connection address
                CHECK(sdp.connectionAddress() == "239.69.0.1");

                REQUIRE(sdp.mediaDescriptions().size() == 1);
                const auto &audio = sdp.mediaDescriptions()[0];
                CHECK(audio.mediaType() == "audio");
                CHECK(audio.port() == 5004);
                CHECK(audio.attribute("ptime") == "1");
                CHECK(audio.attribute("mediaclk") == "direct=0");
        }

        SUBCASE("ST 2110 SDP example") {
                String st2110Sdp =
                        "v=0\r\n"
                        "o=- 9876543210 1 IN IP4 10.0.0.50\r\n"
                        "s=ST 2110 Video\r\n"
                        "c=IN IP4 239.10.20.30/32\r\n"
                        "t=0 0\r\n"
                        "m=video 5004 RTP/AVP 96\r\n"
                        "a=rtpmap:96 raw/90000\r\n"
                        "a=fmtp:96 sampling=YCbCr-4:2:2; width=1920; height=1080; depth=10; colorimetry=BT709\r\n"
                        "a=source-filter:incl IN IP4 239.10.20.30 10.0.0.50\r\n";

                auto [sdp, err] = SdpSession::fromString(st2110Sdp);
                CHECK(err.isOk());
                CHECK(sdp.sessionName() == "ST 2110 Video");
                CHECK(sdp.connectionAddress() == "239.10.20.30");

                REQUIRE(sdp.mediaDescriptions().size() == 1);
                const auto &video = sdp.mediaDescriptions()[0];
                CHECK(video.mediaType() == "video");
                CHECK(video.attribute("rtpmap") == "96 raw/90000");
                CHECK(video.attribute("source-filter").str().find("239.10.20.30") != std::string::npos);
        }
}

TEST_CASE("SdpSession media-level connection address") {

        SUBCASE("toString includes media-level connection") {
                SdpSession sdp;
                sdp.setSessionName("test");
                sdp.setOrigin("-", 1, 1);

                SdpMediaDescription md;
                md.setMediaType("video");
                md.setPort(5004);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(96);
                md.setConnectionAddress("239.0.0.5");
                sdp.addMediaDescription(md);

                String text = sdp.toString();
                // Session-level c= should be absent, media-level c= present
                CHECK(text.str().find("m=video 5004") != std::string::npos);
                CHECK(text.str().find("c=IN IP4 239.0.0.5") != std::string::npos);
        }

        SUBCASE("fromString parses media-level connection") {
                String sdpText =
                        "v=0\r\n"
                        "o=- 1 1 IN IP4 0.0.0.0\r\n"
                        "s=test\r\n"
                        "t=0 0\r\n"
                        "m=video 5004 RTP/AVP 96\r\n"
                        "c=IN IP4 239.0.0.5\r\n"
                        "a=rtpmap:96 raw/90000\r\n";

                auto [sdp, err] = SdpSession::fromString(sdpText);
                CHECK(err.isOk());
                CHECK(sdp.connectionAddress().isEmpty()); // No session-level c=
                REQUIRE(sdp.mediaDescriptions().size() == 1);
                CHECK(sdp.mediaDescriptions()[0].connectionAddress() == "239.0.0.5");
        }
}

TEST_CASE("SdpSession attribute insertion order in toString") {

        SUBCASE("toString outputs attributes in insertion order") {
                SdpSession sdp;
                sdp.setSessionName("order test");
                sdp.setOrigin("-", 1, 1);

                SdpMediaDescription md;
                md.setMediaType("video");
                md.setPort(5004);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(96);
                // Insert attributes in a specific order
                md.setAttribute("rtpmap", "96 raw/90000");
                md.setAttribute("fmtp", "96 width=1920; height=1080");
                md.setAttribute("source-filter", "incl IN IP4 239.0.0.1 10.0.0.1");
                md.setAttribute("mediaclk", "direct=0");
                sdp.addMediaDescription(md);

                String text = sdp.toString();

                // Find positions of each attribute line
                size_t posRtpmap = text.str().find("a=rtpmap:");
                size_t posFmtp = text.str().find("a=fmtp:");
                size_t posFilter = text.str().find("a=source-filter:");
                size_t posClk = text.str().find("a=mediaclk:");

                REQUIRE(posRtpmap != std::string::npos);
                REQUIRE(posFmtp != std::string::npos);
                REQUIRE(posFilter != std::string::npos);
                REQUIRE(posClk != std::string::npos);

                // Verify they appear in insertion order
                CHECK(posRtpmap < posFmtp);
                CHECK(posFmtp < posFilter);
                CHECK(posFilter < posClk);
        }

        SUBCASE("round-trip preserves attribute order") {
                SdpSession original;
                original.setSessionName("order roundtrip");
                original.setOrigin("-", 1, 1);

                SdpMediaDescription md;
                md.setMediaType("audio");
                md.setPort(5006);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(97);
                md.setAttribute("rtpmap", "97 L24/48000/2");
                md.setAttribute("ptime", "1");
                md.setAttribute("mediaclk", "direct=0");
                md.setAttribute("ts-refclk", "ptp=IEEE1588-2008");
                original.addMediaDescription(md);

                String text = original.toString();
                auto [parsed, err] = SdpSession::fromString(text);
                REQUIRE(err.isOk());
                REQUIRE(parsed.mediaDescriptions().size() == 1);

                const auto &attrs = parsed.mediaDescriptions()[0].attributes();
                REQUIRE(attrs.size() == 4);
                CHECK(attrs[0].first() == "rtpmap");
                CHECK(attrs[1].first() == "ptime");
                CHECK(attrs[2].first() == "mediaclk");
                CHECK(attrs[3].first() == "ts-refclk");
        }
}

TEST_CASE("SdpSession flag-only attributes") {

        SUBCASE("toString generates flag-only attribute") {
                SdpSession sdp;
                sdp.setSessionName("test");
                sdp.setOrigin("-", 1, 1);

                SdpMediaDescription md;
                md.setMediaType("audio");
                md.setPort(5006);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(97);
                md.setAttribute("recvonly", String());
                sdp.addMediaDescription(md);

                String text = sdp.toString();
                CHECK(text.str().find("a=recvonly\r\n") != std::string::npos);
        }

        SUBCASE("fromString parses flag-only attribute") {
                String sdpText =
                        "v=0\r\n"
                        "o=- 1 1 IN IP4 0.0.0.0\r\n"
                        "s=test\r\n"
                        "t=0 0\r\n"
                        "m=audio 5006 RTP/AVP 97\r\n"
                        "a=recvonly\r\n";

                auto [sdp, err] = SdpSession::fromString(sdpText);
                CHECK(err.isOk());
                REQUIRE(sdp.mediaDescriptions().size() == 1);
                CHECK(sdp.mediaDescriptions()[0].attribute("recvonly").isEmpty());
                // Verify it's in the attributes map
                CHECK(sdp.mediaDescriptions()[0].attributes().size() == 1);
        }
}

TEST_CASE("SdpMediaDescription") {

        SUBCASE("default construction") {
                SdpMediaDescription md;
                CHECK(md.mediaType().isEmpty());
                CHECK(md.port() == 0);
                CHECK(md.protocol().isEmpty());
                CHECK(md.payloadTypes().isEmpty());
                CHECK(md.attributes().isEmpty());
                CHECK(md.connectionAddress().isEmpty());
        }

        SUBCASE("set and get properties") {
                SdpMediaDescription md;
                md.setMediaType("video");
                md.setPort(5004);
                md.setProtocol("RTP/AVP");
                md.addPayloadType(96);
                md.addPayloadType(97);
                md.setAttribute("rtpmap", "96 raw/90000");
                md.setConnectionAddress("239.0.0.2");

                CHECK(md.mediaType() == "video");
                CHECK(md.port() == 5004);
                CHECK(md.protocol() == "RTP/AVP");
                CHECK(md.payloadTypes().size() == 2);
                CHECK(md.payloadTypes()[0] == 96);
                CHECK(md.payloadTypes()[1] == 97);
                CHECK(md.attribute("rtpmap") == "96 raw/90000");
                CHECK(md.connectionAddress() == "239.0.0.2");
        }

        SUBCASE("missing attribute returns empty string") {
                SdpMediaDescription md;
                CHECK(md.attribute("nonexistent").isEmpty());
        }

        SUBCASE("setAttribute overwrites existing value") {
                SdpMediaDescription md;
                md.setAttribute("rtpmap", "96 raw/90000");
                CHECK(md.attribute("rtpmap") == "96 raw/90000");
                md.setAttribute("rtpmap", "97 L24/48000/2");
                CHECK(md.attribute("rtpmap") == "97 L24/48000/2");
                // Should still be only one attribute
                CHECK(md.attributes().size() == 1);
        }

        SUBCASE("attributes preserve insertion order") {
                SdpMediaDescription md;
                md.setAttribute("rtpmap", "96 raw/90000");
                md.setAttribute("fmtp", "96 width=1920");
                md.setAttribute("source-filter", "incl IN IP4 239.0.0.1");
                md.setAttribute("mediaclk", "direct=0");

                const auto &attrs = md.attributes();
                REQUIRE(attrs.size() == 4);
                CHECK(attrs[0].first() == "rtpmap");
                CHECK(attrs[1].first() == "fmtp");
                CHECK(attrs[2].first() == "source-filter");
                CHECK(attrs[3].first() == "mediaclk");
        }

        SUBCASE("setAttribute update preserves order of other attributes") {
                SdpMediaDescription md;
                md.setAttribute("rtpmap", "96 raw/90000");
                md.setAttribute("fmtp", "96 width=1920");
                md.setAttribute("mediaclk", "direct=0");

                // Update the middle attribute
                md.setAttribute("fmtp", "96 width=3840");

                const auto &attrs = md.attributes();
                REQUIRE(attrs.size() == 3);
                CHECK(attrs[0].first() == "rtpmap");
                CHECK(attrs[1].first() == "fmtp");
                CHECK(attrs[1].second() == "96 width=3840");
                CHECK(attrs[2].first() == "mediaclk");
        }
}
