/**
 * @file      imagefileio_jpeg.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include "codectesthelpers.h"
#include <promeki/imagefileio.h>
#include <promeki/imagefile.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/mediaconfig.h>
#include <promeki/frame.h>

using namespace promeki;

// ============================================================================
// Handler registration
// ============================================================================

TEST_CASE("ImageFileIO JPEG: handler is registered") {
        const ImageFileIO *io = ImageFileIO::lookup(ImageFile::JPEG);
        CHECK(io != nullptr);
        CHECK(io->isValid());
        CHECK(io->canLoad());
        CHECK(io->canSave());
        CHECK(io->name() == "JPEG");
}

// ============================================================================
// Round-trip: save an RGB payload, load it back, verify pixels are close
// ============================================================================

TEST_CASE("ImageFileIO JPEG: RGB8 round-trip") {
        const char *fn = "/tmp/promeki_jpeg_rgb8.jpg";
        auto src = promeki::tests::makeGradientRGB8Payload(64, 48);

        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        CHECK(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        CHECK(lf.load() == Error::Ok);

        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        CHECK(loaded->isCompressed());
        CHECK(loaded->desc().pixelFormat().videoCodec().id() == VideoCodec::JPEG);
        CHECK(loaded->desc().width() == 64);
        CHECK(loaded->desc().height() == 48);

        auto cvp = sharedPointerCast<CompressedVideoPayload>(loaded);
        REQUIRE(cvp.isValid());
        auto decoded = promeki::tests::decodeCompressedPayload(
                cvp, PixelFormat(PixelFormat::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        REQUIRE(!decoded->isCompressed());
        CHECK(decoded->desc().width() == 64);
        CHECK(decoded->desc().height() == 48);

        const double mad = promeki::tests::rgb8MeanAbsDiffPayload(*src, *decoded);
        CHECK(mad < 5.0);

        std::remove(fn);
}

// ============================================================================
// Pass-through: loading JPEG returns a compressed payload; saving it back
// writes the original bytes verbatim.
// ============================================================================

TEST_CASE("ImageFileIO JPEG: pass-through preserves bytes") {
        const char *fnA = "/tmp/promeki_jpeg_passthrough_a.jpg";
        const char *fnB = "/tmp/promeki_jpeg_passthrough_b.jpg";

        auto src = promeki::tests::makeGradientRGB8Payload(96, 64);
        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fnA);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fnA);
        REQUIRE(lf.load() == Error::Ok);
        auto loaded = lf.videoPayload();
        REQUIRE(loaded.isValid());
        REQUIRE(loaded->isCompressed());

        ImageFile sf2(ImageFile::JPEG);
        sf2.setFilename(fnB);
        sf2.setVideoPayload(loaded);
        REQUIRE(sf2.save() == Error::Ok);

        FILE *fa = std::fopen(fnA, "rb");
        FILE *fb = std::fopen(fnB, "rb");
        REQUIRE(fa);
        REQUIRE(fb);
        std::fseek(fa, 0, SEEK_END);
        std::fseek(fb, 0, SEEK_END);
        const long la = std::ftell(fa);
        const long lb = std::ftell(fb);
        std::fseek(fa, 0, SEEK_SET);
        std::fseek(fb, 0, SEEK_SET);
        CHECK(la == lb);
        CHECK(la > 0);
        if(la == lb && la > 0) {
                std::vector<uint8_t> ba(la), bb(lb);
                CHECK(std::fread(ba.data(), 1, la, fa) == static_cast<size_t>(la));
                CHECK(std::fread(bb.data(), 1, lb, fb) == static_cast<size_t>(lb));
                CHECK(std::memcmp(ba.data(), bb.data(), la) == 0);
        }
        std::fclose(fa);
        std::fclose(fb);

        std::remove(fnA);
        std::remove(fnB);
}

// ============================================================================
// Payload::convert() encode path — TPG-style use case
// ============================================================================

TEST_CASE("VideoCodec JPEG: RGB8 -> JPEG round-trip via VideoEncoder/VideoDecoder") {
        auto src = promeki::tests::makeGradientRGB8Payload(80, 60);

        auto jpeg = promeki::tests::encodePayloadToCompressed(
                src, PixelFormat(PixelFormat::JPEG_YUV8_422_Rec709));
        REQUIRE(jpeg.isValid());
        CHECK(jpeg->desc().pixelFormat().videoCodec().id() == VideoCodec::JPEG);
        CHECK(jpeg->plane(0).size() > 0);

        auto decoded = promeki::tests::decodeCompressedPayload(
                jpeg, PixelFormat(PixelFormat::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().width() == src->desc().width());
        CHECK(decoded->desc().height() == src->desc().height());

        const double mad = promeki::tests::rgb8MeanAbsDiffPayload(*src, *decoded);
        CHECK(mad < 20.0);
}

TEST_CASE("VideoCodec JPEG: honours MediaConfig::JpegQuality") {
        auto src = promeki::tests::makeGradientRGB8Payload(128, 96);

        MediaConfig low;
        low.set(MediaConfig::JpegQuality, 10);
        auto lowQ = promeki::tests::encodePayloadToCompressed(
                src, PixelFormat(PixelFormat::JPEG_RGB8_sRGB), low);
        REQUIRE(lowQ.isValid());

        MediaConfig high;
        high.set(MediaConfig::JpegQuality, 95);
        auto highQ = promeki::tests::encodePayloadToCompressed(
                src, PixelFormat(PixelFormat::JPEG_RGB8_sRGB), high);
        REQUIRE(highQ.isValid());

        CHECK(highQ->plane(0).size() > lowQ->plane(0).size());
}

// ============================================================================
// MediaConfig forwarded through ImageFile / ImageFileIO_JPEG to the codec
// ============================================================================

TEST_CASE("ImageFileIO JPEG: save honours MediaConfig::JpegQuality") {
        const char *fnLo = "/tmp/promeki_jpeg_q10.jpg";
        const char *fnHi = "/tmp/promeki_jpeg_q95.jpg";
        auto src = promeki::tests::makeGradientRGB8Payload(128, 96);

        MediaConfig low;
        low.set(MediaConfig::JpegQuality, 10);
        ImageFile sfLo(ImageFile::JPEG);
        sfLo.setFilename(fnLo);
        sfLo.setVideoPayload(src);
        REQUIRE(sfLo.save(low) == Error::Ok);

        MediaConfig high;
        high.set(MediaConfig::JpegQuality, 95);
        ImageFile sfHi(ImageFile::JPEG);
        sfHi.setFilename(fnHi);
        sfHi.setVideoPayload(src);
        REQUIRE(sfHi.save(high) == Error::Ok);

        FILE *fa = std::fopen(fnLo, "rb");
        FILE *fb = std::fopen(fnHi, "rb");
        REQUIRE(fa);
        REQUIRE(fb);
        std::fseek(fa, 0, SEEK_END);
        std::fseek(fb, 0, SEEK_END);
        const long la = std::ftell(fa);
        const long lb = std::ftell(fb);
        std::fclose(fa);
        std::fclose(fb);
        CHECK(la > 0);
        CHECK(lb > la);

        std::remove(fnLo);
        std::remove(fnHi);
}

// ============================================================================
// Error paths
// ============================================================================

TEST_CASE("ImageFileIO JPEG: load nonexistent file returns error") {
        ImageFile lf(ImageFile::JPEG);
        lf.setFilename("/tmp/promeki_jpeg_nonexist.jpg");
        CHECK(lf.load() != Error::Ok);
}

TEST_CASE("ImageFileIO JPEG: load garbage file returns error") {
        const char *fn = "/tmp/promeki_jpeg_bad.jpg";
        FILE *fp = std::fopen(fn, "wb");
        REQUIRE(fp);
        const char garbage[] = "Definitely not a JPEG file, just some text.";
        std::fwrite(garbage, 1, sizeof(garbage), fp);
        std::fclose(fp);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        CHECK(lf.load() != Error::Ok);

        std::remove(fn);
}

TEST_CASE("ImageFileIO JPEG: save empty image returns error") {
        ImageFile sf(ImageFile::JPEG);
        sf.setFilename("/tmp/promeki_jpeg_empty.jpg");
        CHECK(sf.save() != Error::Ok);
}

// ============================================================================
// CompressedVideoPayload emission
// ============================================================================

TEST_CASE("ImageFileIO JPEG: load emits a CompressedVideoPayload") {
        const char *fn = "/tmp/promeki_jpeg_packet.jpg";
        auto src = promeki::tests::makeGradientRGB8Payload(64, 48);

        ImageFile sf(ImageFile::JPEG);
        sf.setFilename(fn);
        sf.setVideoPayload(src);
        REQUIRE(sf.save() == Error::Ok);

        ImageFile lf(ImageFile::JPEG);
        lf.setFilename(fn);
        REQUIRE(lf.load() == Error::Ok);

        const Frame &frame = lf.frame();
        auto vids = frame.videoPayloads();
        REQUIRE(vids.size() == 1);
        const auto *cvp = vids[0]->as<CompressedVideoPayload>();
        REQUIRE(cvp != nullptr);
        CHECK(cvp->desc().pixelFormat().isCompressed());
        CHECK(cvp->plane(0).size() > 0);

        std::remove(fn);
}
