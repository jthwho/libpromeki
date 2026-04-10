/**
 * @file      imagedesc.cpp
 * @author    Jason Howard <jth@howardlogic.com>
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/imagedesc.h>
#include <promeki/sdpsession.h>

using namespace promeki;

// ============================================================================
// Default construction
// ============================================================================

TEST_CASE("ImageDesc_Default") {
    ImageDesc desc;
    CHECK(!desc.isValid());
    CHECK(desc.pixelDesc().id() == PixelDesc::Invalid);
}

// ============================================================================
// Construction with size and pixel format
// ============================================================================

TEST_CASE("ImageDesc_Construct") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.isValid());
    CHECK(desc.width() == 1920);
    CHECK(desc.height() == 1080);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

TEST_CASE("ImageDesc_ConstructSize2D") {
    Size2Du32 sz(3840, 2160);
    ImageDesc desc(sz, PixelDesc::RGB8_sRGB);
    CHECK(desc.isValid());
    CHECK(desc.width() == 3840);
    CHECK(desc.height() == 2160);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("ImageDesc_SetSize") {
    ImageDesc desc;
    desc.setSize(Size2Du32(640, 480));
    CHECK(desc.width() == 640);
    CHECK(desc.height() == 480);

    desc.setSize(1280, 720);
    CHECK(desc.width() == 1280);
    CHECK(desc.height() == 720);
}

TEST_CASE("ImageDesc_SetPixelFormat") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGBA8_sRGB);

    desc.setPixelDesc(PixelDesc::RGB8_sRGB);
    CHECK(desc.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("ImageDesc_SetLinePad") {
    ImageDesc desc;
    CHECK(desc.linePad() == 0);

    desc.setLinePad(64);
    CHECK(desc.linePad() == 64);
}

TEST_CASE("ImageDesc_SetLineAlign") {
    ImageDesc desc;
    CHECK(desc.lineAlign() == 1);

    desc.setLineAlign(16);
    CHECK(desc.lineAlign() == 16);
}

TEST_CASE("ImageDesc_SetInterlaced") {
    ImageDesc desc;
    CHECK(desc.interlaced() == false);

    desc.setInterlaced(true);
    CHECK(desc.interlaced() == true);
}

// ============================================================================
// Copy semantics (plain value, no internal COW)
// ============================================================================

TEST_CASE("ImageDesc_CopyIsIndependent") {
    ImageDesc d1(1920, 1080, PixelDesc::RGBA8_sRGB);
    ImageDesc d2 = d1;

    d2.setSize(3840, 2160);
    CHECK(d1.width() == 1920);
    CHECK(d2.width() == 3840);
}

TEST_CASE("ImageDesc_CopyPixelFormatIndependent") {
    ImageDesc d1(1920, 1080, PixelDesc::RGBA8_sRGB);
    ImageDesc d2 = d1;

    d2.setPixelDesc(PixelDesc::RGB8_sRGB);
    CHECK(d1.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
    CHECK(d2.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

// ============================================================================
// Metadata
// ============================================================================

TEST_CASE("ImageDesc_Metadata") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    const Metadata &cm = desc.metadata();
    CHECK(cm.isEmpty());

    desc.metadata().set(Metadata::Title, String("Test Image"));
    CHECK(!desc.metadata().isEmpty());
    CHECK(desc.metadata().get(Metadata::Title).get<String>() == "Test Image");
}

// ============================================================================
// toString
// ============================================================================

TEST_CASE("ImageDesc_ToString") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    String s = desc.toString();
    CHECK(s.size() > 0);
}

// ============================================================================
// PlaneCount
// ============================================================================

TEST_CASE("ImageDesc_PlaneCount") {
    ImageDesc desc(1920, 1080, PixelDesc::RGBA8_sRGB);
    CHECK(desc.planeCount() > 0);
}

// ============================================================================
// fromSdp — derive an ImageDesc from an SDP media description
// ============================================================================

TEST_CASE("ImageDesc_fromSdp_JpegXs_422_10bit") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.setPort(5004);
    md.setProtocol("RTP/AVP");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp",
        "96 packetmode=0;rate=90000;sampling=YCbCr-4:2:2;"
        "depth=10;width=1920;height=1080;colorimetry=BT709;RANGE=NARROW");

    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV10_422_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_JpegXs_420_8bit") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:0;depth=8;width=640;height=480");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 640);
    CHECK(img.height() == 480);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV8_420_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_NonVideoReturnsInvalid") {
    SdpMediaDescription md;
    md.setMediaType("audio");
    md.setAttribute("rtpmap", "96 L16/48000/2");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK_FALSE(img.isValid());
}

TEST_CASE("ImageDesc_fromSdp_MjpegReturnsInvalid") {
    // MJPEG geometry lives in the RFC 2435 packet header, not SDP —
    // fromSdp should return invalid so the caller knows to read
    // the geometry off the first packet.
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(26);
    md.setAttribute("rtpmap", "26 JPEG/90000");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK_FALSE(img.isValid());
}

TEST_CASE("ImageDesc_fromSdp_JpegXsMissingFmtpReturnsInvalid") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    // No fmtp — no width/height info.
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK_FALSE(img.isValid());
}

TEST_CASE("ImageDesc_fromSdp_JpegXsFallbackDefaultPixelDesc") {
    // When the fmtp has geometry but unrecognised or missing
    // sampling/depth, fromSdp falls back to the library default
    // JPEG_XS_YUV10_422_Rec709.
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp", "96 width=1920;height=1080");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV10_422_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_JpegXs_422_12bit") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:2;depth=12;width=3840;height=2160");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 3840);
    CHECK(img.height() == 2160);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV12_422_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_JpegXs_420_10bit") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:0;depth=10;width=1280;height=720");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 1280);
    CHECK(img.height() == 720);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV10_420_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_JpegXs_420_12bit") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 jxsv/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:0;depth=12;width=640;height=480");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 640);
    CHECK(img.height() == 480);
    CHECK(img.pixelDesc().id() == PixelDesc::JPEG_XS_YUV12_420_Rec709);
}

// ============================================================================
// RFC 4175 raw video SDP round-trip
// ============================================================================

TEST_CASE("ImageDesc_fromSdp_Raw_RGB8") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 raw/90000");
    md.setAttribute("fmtp",
        "96 sampling=RGB;depth=8;width=1920;height=1080;colorimetry=BT709-2;RANGE=FULL");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.width() == 1920);
    CHECK(img.height() == 1080);
    CHECK(img.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("ImageDesc_fromSdp_Raw_UYVY8_Rec709") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 raw/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:2;depth=8;width=1920;height=1080;colorimetry=BT709-2;RANGE=NARROW");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.pixelDesc().id() == PixelDesc::YUV8_422_UYVY_Rec709);
}

TEST_CASE("ImageDesc_fromSdp_Raw_UYVY8_Rec601") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 raw/90000");
    md.setAttribute("fmtp",
        "96 sampling=YCbCr-4:2:2;depth=8;width=1280;height=720;colorimetry=BT601-5;RANGE=NARROW");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.pixelDesc().id() == PixelDesc::YUV8_422_UYVY_Rec601);
}

TEST_CASE("ImageDesc_fromSdp_Raw_RGBA8") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 raw/90000");
    md.setAttribute("fmtp",
        "96 sampling=RGBA;depth=8;width=1920;height=1080;colorimetry=BT709-2;RANGE=FULL");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK(img.isValid());
    CHECK(img.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

TEST_CASE("ImageDesc_toSdp_RGB8") {
    ImageDesc img(Size2Du32(1920, 1080), PixelDesc(PixelDesc::RGB8_sRGB));
    SdpMediaDescription md = img.toSdp(96);
    CHECK(md.mediaType() == "video");
    String fmtp;
    for(size_t i = 0; i < md.attributes().size(); i++) {
        if(md.attributes()[i].first() == "fmtp") fmtp = md.attributes()[i].second();
    }
    CHECK(fmtp.contains("sampling=RGB"));
    CHECK(fmtp.contains("depth=8"));
    CHECK_FALSE(fmtp.contains("RGBA"));
}

TEST_CASE("ImageDesc_toSdp_RGBA8") {
    ImageDesc img(Size2Du32(1920, 1080), PixelDesc(PixelDesc::RGBA8_sRGB));
    SdpMediaDescription md = img.toSdp(96);
    String fmtp;
    for(size_t i = 0; i < md.attributes().size(); i++) {
        if(md.attributes()[i].first() == "fmtp") fmtp = md.attributes()[i].second();
    }
    CHECK(fmtp.contains("sampling=RGBA"));
}

TEST_CASE("ImageDesc_toSdp_UYVY8_Rec709") {
    ImageDesc img(Size2Du32(1920, 1080), PixelDesc(PixelDesc::YUV8_422_UYVY_Rec709));
    SdpMediaDescription md = img.toSdp(96);
    String fmtp;
    for(size_t i = 0; i < md.attributes().size(); i++) {
        if(md.attributes()[i].first() == "fmtp") fmtp = md.attributes()[i].second();
    }
    CHECK(fmtp.contains("sampling=YCbCr-4:2:2"));
    CHECK(fmtp.contains("depth=8"));
    CHECK(fmtp.contains("colorimetry=BT709-2"));
}

TEST_CASE("ImageDesc_fromSdp_UnknownEncodingReturnsInvalid") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    md.setAttribute("rtpmap", "96 H264/90000");
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK_FALSE(img.isValid());
}

TEST_CASE("ImageDesc_fromSdp_MissingRtpmapReturnsInvalid") {
    SdpMediaDescription md;
    md.setMediaType("video");
    md.addPayloadType(96);
    // No rtpmap at all.
    ImageDesc img = ImageDesc::fromSdp(md);
    CHECK_FALSE(img.isValid());
}
