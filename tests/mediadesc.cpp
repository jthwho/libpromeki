/**
 * @file      mediadesc.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/mediadesc.h>
#include <promeki/sdpsession.h>

using namespace promeki;

TEST_CASE("MediaDesc_fromSdp_aggregates_video_and_audio") {
        // Build an SDP session with one JPEG XS video and one
        // L16 audio m= section.  MediaDesc::fromSdp should walk
        // both and populate the corresponding image / audio
        // lists via the per-type fromSdp() factories.
        SdpSession sdp;
        sdp.setSessionName("aggregate test");
        sdp.setConnectionAddress("239.0.0.1");

        SdpMediaDescription video;
        video.setMediaType("video");
        video.setPort(5004);
        video.setProtocol("RTP/AVP");
        video.addPayloadType(96);
        video.setAttribute("rtpmap", "96 jxsv/90000");
        video.setAttribute("fmtp",
                "96 sampling=YCbCr-4:2:2;depth=10;width=1280;height=720");
        sdp.addMediaDescription(video);

        SdpMediaDescription audio;
        audio.setMediaType("audio");
        audio.setPort(5006);
        audio.setProtocol("RTP/AVP");
        audio.addPayloadType(97);
        audio.setAttribute("rtpmap", "97 L16/48000/2");
        sdp.addMediaDescription(audio);

        MediaDesc md = MediaDesc::fromSdp(sdp);
        REQUIRE(md.imageList().size() == 1);
        CHECK(md.imageList()[0].width() == 1280);
        CHECK(md.imageList()[0].height() == 720);
        CHECK(md.imageList()[0].pixelDesc().id() ==
              PixelDesc::JPEG_XS_YUV10_422_Rec709);

        REQUIRE(md.audioList().size() == 1);
        CHECK(md.audioList()[0].dataType() == AudioDesc::PCMI_S16BE);
        CHECK(md.audioList()[0].sampleRate() == 48000.0f);
        CHECK(md.audioList()[0].channels() == 2);
}

TEST_CASE("MediaDesc_fromSdp_skips_unsupported_encodings") {
        // An SDP with a lone MJPEG video entry — MediaDesc::fromSdp
        // should skip it (MJPEG geometry lives in the RFC 2435
        // packet header, not the SDP) and return a MediaDesc with
        // an empty image list rather than an invalid one.
        SdpSession sdp;
        SdpMediaDescription video;
        video.setMediaType("video");
        video.addPayloadType(26);
        video.setAttribute("rtpmap", "26 JPEG/90000");
        sdp.addMediaDescription(video);

        MediaDesc md = MediaDesc::fromSdp(sdp);
        CHECK(md.imageList().isEmpty());
        CHECK(md.audioList().isEmpty());
}

TEST_CASE("MediaDesc_fromSdp_empty_session_returns_empty_desc") {
        SdpSession sdp;
        MediaDesc md = MediaDesc::fromSdp(sdp);
        CHECK(md.imageList().isEmpty());
        CHECK(md.audioList().isEmpty());
}

TEST_CASE("MediaDesc_fromSdp_multiple_video_tracks") {
        SdpSession sdp;
        // Two video tracks at different resolutions — both
        // should land in the image list.
        for(int i = 0; i < 2; i++) {
                SdpMediaDescription v;
                v.setMediaType("video");
                v.setPort(static_cast<uint16_t>(5004 + i * 2));
                v.setProtocol("RTP/AVP");
                v.addPayloadType(96);
                v.setAttribute("rtpmap", "96 jxsv/90000");
                v.setAttribute("fmtp",
                        String("96 sampling=YCbCr-4:2:2;depth=10;width=") +
                        String::number(1920 / (i + 1)) +
                        String(";height=") +
                        String::number(1080 / (i + 1)));
                sdp.addMediaDescription(v);
        }

        MediaDesc md = MediaDesc::fromSdp(sdp);
        REQUIRE(md.imageList().size() == 2);
        CHECK(md.imageList()[0].width() == 1920);
        CHECK(md.imageList()[0].height() == 1080);
        CHECK(md.imageList()[1].width() == 960);
        CHECK(md.imageList()[1].height() == 540);
}
