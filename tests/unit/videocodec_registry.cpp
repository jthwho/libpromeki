/**
 * @file      tests/videocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#include <doctest/doctest.h>
#include <promeki/videocodec.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/config.h>
#include <promeki/pixelformat.h>

using namespace promeki;

TEST_CASE("VideoCodec: well-known codecs resolve by ID") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.isValid());
        CHECK(h264.name() == "H264");
        CHECK_FALSE(h264.description().isEmpty());
        // Associates with the H264 PixelFormat entry.
        REQUIRE(!h264.compressedPixelFormats().isEmpty());
        CHECK(h264.compressedPixelFormats()[0] == PixelFormat::H264);

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.isValid());
        CHECK(hevc.name() == "HEVC");
        CHECK(hevc != h264);
}

TEST_CASE("VideoCodec: lookup by name returns the registered entry") {
        VideoCodec h264 = value(VideoCodec::lookup("H264"));
        CHECK(h264 == VideoCodec(VideoCodec::H264));

        VideoCodec hevc = value(VideoCodec::lookup("HEVC"));
        CHECK(hevc == VideoCodec(VideoCodec::HEVC));

        VideoCodec bogus = value(VideoCodec::lookup("not-a-real-codec"));
        CHECK_FALSE(bogus.isValid());
}

TEST_CASE("VideoCodec: codecs without a registered backend report null cleanly") {
        VideoCodec vp9(VideoCodec::VP9);
        CHECK(vp9.isValid());
        CHECK_FALSE(vp9.canEncode());
        CHECK_FALSE(vp9.canDecode());
        CHECK(error(vp9.createEncoder()).isError());
        CHECK(error(vp9.createDecoder()).isError());
}

#if PROMEKI_ENABLE_NVENC
TEST_CASE("VideoCodec: H264/HEVC encoders wired when NVENC is built in") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.canEncode());
        auto enc = h264.createEncoder();
        REQUIRE(isOk(enc));
        CHECK(value(enc)->codec().name() == "H264");
        CHECK(value(enc)->codec().backend().name() == "Nvidia");
        delete value(enc);

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.canEncode());
}
#endif

#if PROMEKI_ENABLE_NVDEC
TEST_CASE("VideoCodec: H264/HEVC decoders wired when NVDEC is built in") {
        VideoCodec h264(VideoCodec::H264);
        CHECK(h264.canDecode());
        auto dec = h264.createDecoder();
        REQUIRE(isOk(dec));
        CHECK(value(dec)->codec().name() == "H264");
        CHECK(value(dec)->codec().backend().name() == "Nvidia");
        delete value(dec);

        VideoCodec hevc(VideoCodec::HEVC);
        CHECK(hevc.canDecode());
}
#endif

TEST_CASE("VideoCodec: JPEG has wired-up encoder + decoder factories") {
        // JpegVideoCodec registers against VideoCodec::JPEG at
        // static-init time.  This documents the current wiring +
        // catches accidental unregistration.
        VideoCodec jpeg(VideoCodec::JPEG);
        CHECK(jpeg.canEncode());
        CHECK(jpeg.canDecode());
        auto enc = jpeg.createEncoder();
        auto dec = jpeg.createDecoder();
        REQUIRE(isOk(enc));
        REQUIRE(isOk(dec));
        CHECK(value(enc)->codec().name() == "JPEG");
        CHECK(value(dec)->codec().name() == "JPEG");
        CHECK(value(enc)->codec().backend().name() == "Turbo");
        CHECK(value(dec)->codec().backend().name() == "Turbo");
        delete value(enc);
        delete value(dec);
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
        // Names must be valid C identifiers — registerData rejects
        // hyphens and other non-identifier characters.
        d.id   = myId;
        d.name = "TestCustomCodec";
        d.desc = "Custom codec registered from a unit test";
        VideoCodec::registerData(std::move(d));

        VideoCodec by_id(myId);
        CHECK(by_id.isValid());
        CHECK(by_id.name() == "TestCustomCodec");

        VideoCodec by_name = value(VideoCodec::lookup("TestCustomCodec"));
        CHECK(by_name == by_id);
}

// ---------------------------------------------------------------------------
// Default-constructed wrapper / Invalid ID
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec: default ctor is invalid") {
        VideoCodec c;
        CHECK_FALSE(c.isValid());
        CHECK_FALSE(c.canEncode());
        CHECK_FALSE(c.canDecode());
        CHECK(c.availableEncoderBackends().isEmpty());
        CHECK(c.availableDecoderBackends().isEmpty());
        CHECK(c.encoderSupportedInputs().isEmpty());
        CHECK(c.decoderSupportedOutputs().isEmpty());
        CHECK(c.compressedPixelFormats().isEmpty());
        CHECK(c.toString().isEmpty());
}

TEST_CASE("VideoCodec: equality compares both Data pointer and pinned backend") {
        VideoCodec h264a(VideoCodec::H264);
        VideoCodec h264b(VideoCodec::H264);
        CHECK(h264a == h264b);

        auto bk = VideoCodec::registerBackend("VideoCodecTest_EqBackend");
        REQUIRE(isOk(bk));
        VideoCodec h264Pinned(VideoCodec::H264, value(bk));
        CHECK(h264Pinned != h264a);
        CHECK(h264Pinned.id() == h264a.id());
}

// ---------------------------------------------------------------------------
// Backend registry — registerBackend / lookupBackend
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::registerBackend rejects names that aren't C identifiers") {
        auto bad = VideoCodec::registerBackend("contains-hyphen");
        CHECK(error(bad).isError());
        CHECK(error(bad) == Error::Invalid);

        auto empty = VideoCodec::registerBackend("");
        CHECK(error(empty) == Error::Invalid);

        auto digit = VideoCodec::registerBackend("9starts");
        CHECK(error(digit) == Error::Invalid);
}

TEST_CASE("VideoCodec::registerBackend is idempotent") {
        auto a = VideoCodec::registerBackend("VideoCodecTest_RegRepeat");
        auto b = VideoCodec::registerBackend("VideoCodecTest_RegRepeat");
        REQUIRE(isOk(a));
        REQUIRE(isOk(b));
        CHECK(value(a) == value(b));
}

TEST_CASE("VideoCodec::lookupBackend returns IdNotFound when the name isn't registered") {
        auto res = VideoCodec::lookupBackend("VideoCodecTest_NeverRegistered");
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("VideoCodec::lookupBackend rejects invalid identifiers") {
        auto res = VideoCodec::lookupBackend("contains spaces");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("VideoCodec::lookupBackend resolves what registerBackend wrote") {
        auto reg = VideoCodec::registerBackend("VideoCodecTest_LookupRoundTrip");
        REQUIRE(isOk(reg));
        auto look = VideoCodec::lookupBackend("VideoCodecTest_LookupRoundTrip");
        REQUIRE(isOk(look));
        CHECK(value(reg) == value(look));
}

TEST_CASE("VideoCodec::Backend: default ctor is invalid; fromId reconstructs") {
        VideoCodec::Backend b;
        CHECK_FALSE(b.isValid());

        auto reg = VideoCodec::registerBackend("VideoCodecTest_Backend_FromId");
        REQUIRE(isOk(reg));
        VideoCodec::Backend orig = value(reg);
        CHECK(orig.isValid());

        VideoCodec::Backend cloned = VideoCodec::Backend::fromId(orig.id());
        CHECK(cloned == orig);
        CHECK(cloned.name() == "VideoCodecTest_Backend_FromId");
}

// ---------------------------------------------------------------------------
// fromString / toString
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::fromString: codec-only form returns an unpinned wrapper") {
        auto res = VideoCodec::fromString("H264");
        REQUIRE(isOk(res));
        VideoCodec c = value(res);
        CHECK(c.id() == VideoCodec::H264);
        CHECK_FALSE(c.backend().isValid());
}

TEST_CASE("VideoCodec::fromString: codec:backend form pins the backend") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_FromStringBackend");
        REQUIRE(isOk(bk));

        auto res = VideoCodec::fromString("H264:VideoCodecTest_FromStringBackend");
        REQUIRE(isOk(res));
        VideoCodec c = value(res);
        CHECK(c.id() == VideoCodec::H264);
        CHECK(c.backend() == value(bk));
        CHECK(c.toString() == "H264:VideoCodecTest_FromStringBackend");
}

TEST_CASE("VideoCodec::fromString: empty input is Error::Invalid") {
        auto res = VideoCodec::fromString("");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("VideoCodec::fromString: too many colons is Error::Invalid") {
        auto res = VideoCodec::fromString("H264:A:B");
        CHECK(error(res) == Error::Invalid);
}

TEST_CASE("VideoCodec::fromString: unknown codec is Error::IdNotFound") {
        auto res = VideoCodec::fromString("DefinitelyNotARealCodec");
        CHECK(error(res) == Error::IdNotFound);
}

TEST_CASE("VideoCodec::fromString: known codec + unknown backend is an error") {
        auto res = VideoCodec::fromString("H264:VideoCodecTest_NeverRegisteredBackend");
        CHECK(error(res).isError());
}

TEST_CASE("VideoCodec::toString: invalid codec returns empty; unpinned returns name") {
        VideoCodec invalid;
        CHECK(invalid.toString().isEmpty());

        VideoCodec unpinned(VideoCodec::H264);
        CHECK(unpinned.toString() == "H264");
}

TEST_CASE("VideoCodec::toString: pinned codec returns Name:Backend") {
        auto bk = VideoCodec::registerBackend("VideoCodecTest_ToStringBackend");
        REQUIRE(isOk(bk));
        VideoCodec c(VideoCodec::HEVC, value(bk));
        CHECK(c.toString() == "HEVC:VideoCodecTest_ToStringBackend");
}

// ---------------------------------------------------------------------------
// fromPixelFormat
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::fromPixelFormat: invalid PixelFormat -> invalid codec") {
        VideoCodec c = VideoCodec::fromPixelFormat(PixelFormat());
        CHECK_FALSE(c.isValid());
}

TEST_CASE("VideoCodec::fromPixelFormat: H264 PixelFormat -> H264 codec") {
        VideoCodec c = VideoCodec::fromPixelFormat(PixelFormat(PixelFormat::H264));
        CHECK(c.isValid());
        CHECK(c.id() == VideoCodec::H264);
}

TEST_CASE("VideoCodec::fromPixelFormat: HEVC PixelFormat -> HEVC codec") {
        VideoCodec c = VideoCodec::fromPixelFormat(PixelFormat(PixelFormat::HEVC));
        CHECK(c.isValid());
        CHECK(c.id() == VideoCodec::HEVC);
}

TEST_CASE("VideoCodec::fromPixelFormat: JPEG sub-format -> JPEG codec") {
        VideoCodec c = VideoCodec::fromPixelFormat(PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        CHECK(c.isValid());
        CHECK(c.id() == VideoCodec::JPEG);
}

TEST_CASE("VideoCodec::fromPixelFormat: uncompressed PixelFormat -> invalid codec") {
        // RGB8_sRGB isn't on any codec's compressedPixelFormats list.
        VideoCodec c = VideoCodec::fromPixelFormat(PixelFormat(PixelFormat::RGB8_sRGB));
        CHECK_FALSE(c.isValid());
}

// ---------------------------------------------------------------------------
// registerData — name validation
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::registerData drops malformed names") {
        VideoCodec::ID myId = VideoCodec::registerType();
        VideoCodec::Data d;
        d.id   = myId;
        d.name = "bad-name-with-hyphen";  // not a C identifier
        d.desc = "Should be rejected";
        VideoCodec::registerData(std::move(d));

        auto res = VideoCodec::lookup("bad-name-with-hyphen");
        CHECK(error(res).isError());
}

// ---------------------------------------------------------------------------
// registeredBackends — union over encoder + decoder registries
// ---------------------------------------------------------------------------

TEST_CASE("VideoCodec::registeredBackends: includes every backend with a wired session") {
        // The Passthrough backend (registered by tests/unit/videocodec.cpp)
        // and JPEG's "Turbo" backend should both show up regardless of
        // the order CMake stitches the test TUs together.
        auto backends = VideoCodec::registeredBackends();
        CHECK_FALSE(backends.isEmpty());

        bool sawTurbo = false;
        bool sawPassthrough = false;
        for(const auto &b : backends) {
                if(b.name() == "Turbo")        sawTurbo = true;
                if(b.name() == "Passthrough")  sawPassthrough = true;
        }
        CHECK(sawTurbo);
        CHECK(sawPassthrough);
}
