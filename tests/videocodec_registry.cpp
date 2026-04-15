/**
 * @file      tests/videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/videocodec.h>
#include <promeki/codec.h>
#include <promeki/config.h>
#include <promeki/pixeldesc.h>

using namespace promeki;

TEST_CASE("VideoCodec: well-known codecs resolve by ID") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.isValid());
        CHECK(h264.name() == "H264");
        CHECK_FALSE(h264.description().isEmpty());
        // Associates with the H264 PixelDesc entry.
        REQUIRE(!h264.compressedPixelDescs().isEmpty());
        CHECK(h264.compressedPixelDescs()[0] == PixelDesc::H264);

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.isValid());
        CHECK(hevc.name() == "HEVC");
        CHECK(hevc != h264);
}

TEST_CASE("VideoCodec: lookup by name returns the registered entry") {
        VideoCodec h264 = VideoCodec::lookup("H264");
        CHECK(h264 == VideoCodec(VideoCodec::H264));

        VideoCodec hevc = VideoCodec::lookup("HEVC");
        CHECK(hevc == VideoCodec(VideoCodec::HEVC));

        VideoCodec bogus = VideoCodec::lookup("not-a-real-codec");
        CHECK_FALSE(bogus.isValid());
}

TEST_CASE("VideoCodec: codecs without a registered backend report null cleanly") {
        // AV1 is metadata-only at this stage — the registry knows
        // about it (name / fourccs) but no backend has wired in
        // factories for it.  When a real AV1 encoder/decoder lands,
        // pick whatever still has no factory for this assertion.
        VideoCodec av1(VideoCodec::AV1);
        CHECK(av1.isValid());
        CHECK_FALSE(av1.canEncode());
        CHECK_FALSE(av1.canDecode());
        CHECK(av1.createEncoder() == nullptr);
        CHECK(av1.createDecoder() == nullptr);
}

#if PROMEKI_ENABLE_NVENC
TEST_CASE("VideoCodec: H264/HEVC encoders wired when NVENC is built in") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.canEncode());
        VideoEncoder *enc = h264.createEncoder();
        REQUIRE(enc != nullptr);
        CHECK(enc->name() == "H264");
        delete enc;

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.canEncode());
}
#endif

#if PROMEKI_ENABLE_NVDEC
TEST_CASE("VideoCodec: H264/HEVC decoders wired when NVDEC is built in") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.canDecode());
        VideoDecoder *dec = h264.createDecoder();
        REQUIRE(dec != nullptr);
        CHECK(dec->name() == "H264");
        delete dec;

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.canDecode());
}
#endif

TEST_CASE("VideoCodec: JPEG has wired-up encoder + decoder factories") {
        // Jpeg{Video,Image}Codec registers against VideoCodec::JPEG
        // at static-init time.  This documents the current wiring +
        // catches accidental unregistration.
        VideoCodec jpeg(VideoCodec::JPEG);
        CHECK(jpeg.canEncode());
        CHECK(jpeg.canDecode());
        VideoEncoder *enc = jpeg.createEncoder();
        VideoDecoder *dec = jpeg.createDecoder();
        REQUIRE(enc != nullptr);
        REQUIRE(dec != nullptr);
        CHECK(enc->name() == "JPEG");
        CHECK(dec->name() == "JPEG");
        delete enc;
        delete dec;
}

TEST_CASE("VideoCodec: registeredIDs() enumerates every well-known codec") {
        auto ids = VideoCodec::registeredIDs();
        CHECK(ids.contains(VideoCodec::H264));
        CHECK(ids.contains(VideoCodec::HEVC));
        CHECK(ids.contains(VideoCodec::JPEG));
        CHECK(ids.contains(VideoCodec::JPEG_XS));
        CHECK(ids.contains(VideoCodec::ProRes_422));
        // registerType is always reserved above every well-known ID.
        for(auto id : ids) CHECK(id != VideoCodec::Invalid);
}

TEST_CASE("VideoCodec: user-registered codecs flow through register/lookup") {
        VideoCodec::ID myId = VideoCodec::registerType();
        VideoCodec::Data d;
        d.id   = myId;
        d.name = "test-custom-codec";
        d.desc = "Custom codec registered from a unit test";
        VideoCodec::registerData(std::move(d));

        VideoCodec by_id(myId);
        CHECK(by_id.isValid());
        CHECK(by_id.name() == "test-custom-codec");

        VideoCodec by_name = VideoCodec::lookup("test-custom-codec");
        CHECK(by_name == by_id);
}
