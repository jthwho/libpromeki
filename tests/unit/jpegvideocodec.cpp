/**
 * @file      tests/jpegvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end coverage for JpegVideoEncoder / JpegVideoDecoder via the
 * typed VideoCodec session API.  The pre-Phase-4 JpegImageCodec base
 * was retired with the ImageCodec abstraction; the libjpeg-turbo
 * plumbing now lives directly on the encoder / decoder session
 * classes.
 */

#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include "codectesthelpers.h"

#include <promeki/jpegvideocodec.h>
#include <promeki/videocodec.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>
#include <promeki/compressedvideopayload.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

// ---------------------------------------------------------------------------
// Test payload helpers (mirror the pre-Phase-4 JpegImageCodec test fixtures)
// ---------------------------------------------------------------------------

static UncompressedVideoPayload::Ptr createTestImage(int width, int height,
                                                     PixelFormat::ID pixfmt = PixelFormat::RGB8_sRGB) {
        auto     img = UncompressedVideoPayload::allocate(ImageDesc(width, height, pixfmt));
        int      comps = (pixfmt == PixelFormat::RGBA8_sRGB) ? 4 : 3;
        uint8_t *data = img.modify()->data()[0].data();
        size_t   stride = img->desc().pixelFormat().memLayout().lineStride(0, width);
        for (int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for (int x = 0; x < width; x++) {
                        row[x * comps + 0] = (uint8_t)(x * 255 / width);
                        row[x * comps + 1] = (uint8_t)(y * 255 / height);
                        row[x * comps + 2] = 128;
                        if (comps == 4) row[x * comps + 3] = 255;
                }
        }
        return img;
}

static UncompressedVideoPayload::Ptr createTestYCbCrImage(int width, int height, PixelFormat::ID pd) {
        auto     img = UncompressedVideoPayload::allocate(ImageDesc(width, height, pd));
        uint8_t *data = img.modify()->data()[0].data();
        size_t   stride = img->desc().pixelFormat().memLayout().lineStride(0, width);
        bool     isUYVY = (pd == PixelFormat::YUV8_422_UYVY_Rec709);
        for (int y = 0; y < height; y++) {
                uint8_t *row = data + y * stride;
                for (int x = 0; x < width / 2; x++) {
                        uint8_t luma0 = (uint8_t)(16 + (x * 2) * 219 / width);
                        uint8_t luma1 = (uint8_t)(16 + (x * 2 + 1) * 219 / width);
                        uint8_t cb = 128, cr = 128;
                        if (isUYVY) {
                                row[x * 4 + 0] = cb;
                                row[x * 4 + 1] = luma0;
                                row[x * 4 + 2] = cr;
                                row[x * 4 + 3] = luma1;
                        } else {
                                row[x * 4 + 0] = luma0;
                                row[x * 4 + 1] = cb;
                                row[x * 4 + 2] = luma1;
                                row[x * 4 + 3] = cr;
                        }
                }
        }
        return img;
}

static UncompressedVideoPayload::Ptr createPlanarImage(int width, int height, PixelFormat::ID pd) {
        auto img = UncompressedVideoPayload::allocate(ImageDesc(width, height, pd));
        if (!img.isValid()) return img;
        const auto &ml = img->desc().pixelFormat().memLayout();
        uint8_t    *y = img.modify()->data()[0].data();
        size_t      ySize = ml.planeSize(0, width, height);
        for (size_t i = 0; i < ySize; i++) y[i] = (uint8_t)(16 + i * 219 / ySize);
        for (size_t p = 1; p < img->desc().pixelFormat().planeCount(); p++) {
                size_t pSize = ml.planeSize(p, width, height);
                std::memset(img.modify()->data()[p].data(), 128, pSize);
        }
        return img;
}

// ---------------------------------------------------------------------------
// One-shot helpers — push a single frame through a session and pull
// the resulting packet (encoder) or frame (decoder).  Equivalent to the
// codectesthelpers.h utilities but exposed inline so the tests can
// pin a specific Subsampling/Quality on the encoder before submit.
// ---------------------------------------------------------------------------

static CompressedVideoPayload::Ptr encodeOneFrame(const UncompressedVideoPayload::Ptr &src, const MediaConfig &cfg) {
        MediaConfig sessionCfg = cfg;
        if (!sessionCfg.contains(MediaConfig::OutputPixelFormat)) {
                sessionCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_RGB8_sRGB));
        }
        auto encResult = VideoCodec(VideoCodec::JPEG).createEncoder(&sessionCfg);
        if (error(encResult).isError()) return CompressedVideoPayload::Ptr();
        VideoEncoder *enc = value(encResult);
        Error         err = enc->submitPayload(src);
        if (err.isError()) {
                delete enc;
                return CompressedVideoPayload::Ptr();
        }
        CompressedVideoPayload::Ptr pkt = enc->receiveCompressedPayload();
        delete enc;
        return pkt;
}

static UncompressedVideoPayload::Ptr decodeOneFrame(const CompressedVideoPayload::Ptr &pkt, PixelFormat target,
                                                    int width, int height) {
        MediaConfig cfg;
        cfg.set(MediaConfig::OutputPixelFormat, target);
        cfg.set(MediaConfig::VideoSize, Size2Du32(width, height));
        auto decResult = VideoCodec(VideoCodec::JPEG).createDecoder(&cfg);
        if (error(decResult).isError()) return UncompressedVideoPayload::Ptr();
        VideoDecoder *dec = value(decResult);
        Error         err = dec->submitPayload(pkt);
        if (err.isError()) {
                delete dec;
                return UncompressedVideoPayload::Ptr();
        }
        UncompressedVideoPayload::Ptr out = dec->receiveVideoPayload();
        delete dec;
        return out;
}

// ---------------------------------------------------------------------------
// Encoder defaults & basic capabilities
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_Defaults") {
        JpegVideoEncoder enc;
        CHECK(enc.quality() == 85);
        CHECK(enc.subsampling() == JpegVideoEncoder::Subsampling422);
}

TEST_CASE("JpegVideoEncoder_QualityClamping") {
        // The spec on JpegQuality declares a 1-100 range, so a Strict
        // database refuses to store 0 or 200 in the first place.  This
        // test specifically exercises the encoder's defensive clamp,
        // which only fires when an out-of-range value still reaches it
        // (e.g. from a config built in Warn mode, or constructed by a
        // backend that doesn't share the spec).  Drop into Warn so the
        // out-of-range values land in the config and travel through to
        // configure().
        JpegVideoEncoder enc;
        MediaConfig      cfg;
        cfg.setValidation(SpecValidation::Warn);
        cfg.set(MediaConfig::JpegQuality, 0);
        enc.configure(cfg);
        CHECK(enc.quality() == 1);
        cfg.set(MediaConfig::JpegQuality, 200);
        enc.configure(cfg);
        CHECK(enc.quality() == 100);
}

// ---------------------------------------------------------------------------
// Codec registry — the typed VideoCodec::JPEG path resolves a session
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_RegisteredAndCanCreate") {
        VideoCodec vc(VideoCodec::JPEG);
        CHECK(vc.isValid());
        CHECK(vc.canEncode());
        CHECK(vc.canDecode());
        auto encResult = vc.createEncoder(nullptr);
        REQUIRE_FALSE(error(encResult).isError());
        delete value(encResult);
        auto decResult = vc.createDecoder(nullptr);
        REQUIRE_FALSE(error(decResult).isError());
        delete value(decResult);
}

// ---------------------------------------------------------------------------
// RGB / RGBA encode through the session
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_EncodeRGB8") {
        auto                        src = createTestImage(320, 240);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->plane(0).size() > 0);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_RGB8_sRGB);
        const uint8_t *d = static_cast<const uint8_t *>(pkt->plane(0).data());
        CHECK(d[0] == 0xFF);
        CHECK(d[1] == 0xD8);
        CHECK(pkt->isKeyframe());
}

TEST_CASE("JpegVideoEncoder_EncodeRGBA8") {
        auto                        src = createTestImage(160, 120, PixelFormat::RGBA8_sRGB);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_RGBA8_sRGB);
}

// ---------------------------------------------------------------------------
// Metadata propagates through encode (PTS / metadata stamped on the packet)
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_MetadataPreserved") {
        auto img = createTestImage(64, 64);
        img.modify()->desc().metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 1, 30, 15, 10));
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(img, MediaConfig());
        REQUIRE(pkt);
        Timecode tc = pkt->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 1);
        CHECK(tc.frame() == 10);
}

// ---------------------------------------------------------------------------
// Quality affects size
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_QualityAffectsSize") {
        auto        img = createTestImage(320, 240);
        MediaConfig lo;
        lo.set(MediaConfig::JpegQuality, 10);
        MediaConfig hi;
        hi.set(MediaConfig::JpegQuality, 95);
        CompressedVideoPayload::Ptr loPkt = encodeOneFrame(img, lo);
        CompressedVideoPayload::Ptr hiPkt = encodeOneFrame(img, hi);
        REQUIRE(loPkt);
        REQUIRE(hiPkt);
        CHECK(hiPkt->plane(0).size() > loPkt->plane(0).size());
}

// ---------------------------------------------------------------------------
// Subsampling modes via MediaConfig dispatch
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_SubsamplingModes") {
        auto        img = createTestImage(320, 240);
        MediaConfig c444;
        c444.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV444);
        MediaConfig c420;
        c420.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV420);
        CompressedVideoPayload::Ptr p444 = encodeOneFrame(img, c444);
        CompressedVideoPayload::Ptr p420 = encodeOneFrame(img, c420);
        REQUIRE(p444);
        REQUIRE(p420);
        CHECK(p444->plane(0).size() > p420->plane(0).size());
}

// ---------------------------------------------------------------------------
// Invalid input rejected with an error
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_InvalidInput") {
        auto encResult = VideoCodec(VideoCodec::JPEG).createEncoder(nullptr);
        REQUIRE_FALSE(error(encResult).isError());
        VideoEncoder *enc = value(encResult);
        CHECK(enc->submitPayload(UncompressedVideoPayload::Ptr()).isError());
        delete enc;

        auto decResult = VideoCodec(VideoCodec::JPEG).createDecoder(nullptr);
        REQUIRE_FALSE(error(decResult).isError());
        VideoDecoder *dec = value(decResult);
        CHECK(dec->submitPayload(CompressedVideoPayload::Ptr()).isError());
        delete dec;
}

// ---------------------------------------------------------------------------
// RGB round-trip
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_RoundTripRGB8") {
        auto        src = createTestImage(320, 240);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegQuality, 100);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::RGB8_sRGB), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::RGB8_sRGB);
}

TEST_CASE("JpegVideoCodec_DecodeToRGBA8") {
        auto                        src = createTestImage(160, 120);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::RGBA8_sRGB), 160, 120);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
}

// ---------------------------------------------------------------------------
// MediaConfig dispatch — make sure JpegQuality / JpegSubsampling flow
// through the session's configure() the same way they used to flow
// through ImageCodec::configure().
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoEncoder_ConfigureFromMediaConfig") {
        SUBCASE("Empty MediaConfig leaves session defaults intact") {
                JpegVideoEncoder configured;
                configured.configure(MediaConfig());
                JpegVideoEncoder baseline;
                CHECK(configured.quality() == baseline.quality());
                CHECK(configured.subsampling() == baseline.subsampling());
        }
        SUBCASE("Unknown subsampling string is ignored") {
                JpegVideoEncoder enc;
                MediaConfig      setup;
                setup.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV444);
                enc.configure(setup);
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegSubsampling, String("YUV777"));
                enc.configure(cfg);
                CHECK(enc.subsampling() == JpegVideoEncoder::Subsampling444);
        }
}

// ---------------------------------------------------------------------------
// 4:2:2 SOF0 marker check — pin the on-wire chroma sampling factors.
// ---------------------------------------------------------------------------

static size_t scanMarker(const uint8_t *data, size_t size, uint8_t marker, size_t startPos = 0) {
        for (size_t i = startPos; i + 1 < size; i++)
                if (data[i] == 0xFF && data[i + 1] == marker) return i;
        return size;
}

TEST_CASE("JpegVideoEncoder_422Structure") {
        auto        src = createTestImage(640, 480);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV422);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        const uint8_t *d = static_cast<const uint8_t *>(pkt->plane(0).data());
        size_t         sof0 = scanMarker(d, pkt->plane(0).size(), 0xC0, 2);
        REQUIRE(sof0 < pkt->plane(0).size());
        CHECK(d[sof0 + 11] == 0x21); // Y: H=2 V=1
        CHECK(d[sof0 + 14] == 0x11); // Cb
        CHECK(d[sof0 + 17] == 0x11); // Cr
}

// ---------------------------------------------------------------------------
// Interleaved UYVY / YUYV encode & round-trip
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodeUYVY") {
        auto                        src = createTestYCbCrImage(320, 240, PixelFormat::YUV8_422_UYVY_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegVideoCodec_EncodeYUYV") {
        auto                        src = createTestYCbCrImage(320, 240, PixelFormat::YUV8_422_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegVideoCodec_RoundTripUYVY") {
        auto        src = createTestYCbCrImage(320, 240, PixelFormat::YUV8_422_UYVY_Rec709);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegQuality, 100);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_422_UYVY_Rec709);
        const uint8_t *s = static_cast<const uint8_t *>(src->plane(0).data());
        const uint8_t *d = static_cast<const uint8_t *>(decoded->plane(0).data());
        CHECK(std::abs((int)s[1] - (int)d[1]) < 4);
}

TEST_CASE("JpegVideoCodec_RoundTripYUYV") {
        auto                        src = createTestYCbCrImage(320, 240, PixelFormat::YUV8_422_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_422_Rec709), 320, 240);
        CHECK(decoded.isValid());
}

// ---------------------------------------------------------------------------
// Cross-format interleaved <-> RGB
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodeUYVYDecodeRGB") {
        auto                        src = createTestYCbCrImage(320, 240, PixelFormat::YUV8_422_UYVY_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::RGB8_sRGB), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::RGB8_sRGB);
}

TEST_CASE("JpegVideoCodec_EncodeRGBDecodeUYVY") {
        auto                        src = createTestImage(320, 240);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), 320, 240);
        CHECK(decoded.isValid());
}

// ---------------------------------------------------------------------------
// Non-8-aligned height
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodeUYVYNonAlignedHeight") {
        auto                        src = createTestYCbCrImage(320, 244, PixelFormat::YUV8_422_UYVY_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709), 320, 244);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().height() == 244);
}

// ---------------------------------------------------------------------------
// Default decode format (no OutputPixelFormat in MediaConfig)
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_DecodeDefaultFormat") {
        auto                        src = createTestImage(160, 120);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);

        // Decode without OutputPixelFormat — the decoder picks the
        // first decodeTarget for the input JPEG sub-format.
        MediaConfig decCfg;
        decCfg.set(MediaConfig::VideoSize, Size2Du32(160, 120));
        auto decResult = VideoCodec(VideoCodec::JPEG).createDecoder(&decCfg);
        REQUIRE_FALSE(error(decResult).isError());
        VideoDecoder *dec = value(decResult);
        CHECK(dec->submitPayload(pkt) == Error::Ok);
        UncompressedVideoPayload::Ptr decoded = dec->receiveVideoPayload();
        delete dec;
        CHECK(decoded.isValid());
        CHECK(decoded->isValid());
}

// ---------------------------------------------------------------------------
// Planar 4:2:2 encode & round-trip
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodePlanar422") {
        auto                        src = createPlanarImage(320, 240, PixelFormat::YUV8_422_Planar_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_422_Rec709);
}

TEST_CASE("JpegVideoCodec_RoundTripPlanar422") {
        auto        src = createPlanarImage(320, 240, PixelFormat::YUV8_422_Planar_Rec709);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegQuality, 100);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_422_Planar_Rec709), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_422_Planar_Rec709);
        CHECK(decoded->desc().width() == 320);
        CHECK(decoded->desc().height() == 240);
}

// ---------------------------------------------------------------------------
// Planar 4:2:0 encode & round-trip
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodePlanar420") {
        auto                        src = createPlanarImage(320, 240, PixelFormat::YUV8_420_Planar_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_420_Rec709);
}

TEST_CASE("JpegVideoCodec_RoundTripPlanar420") {
        auto        src = createPlanarImage(320, 240, PixelFormat::YUV8_420_Planar_Rec709);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegQuality, 100);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_420_Planar_Rec709), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_420_Planar_Rec709);
}

// ---------------------------------------------------------------------------
// NV12 encode & round-trip
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodeNV12") {
        auto                        src = createPlanarImage(320, 240, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        CHECK(pkt->desc().pixelFormat().id() == PixelFormat::JPEG_YUV8_420_Rec709);
}

TEST_CASE("JpegVideoCodec_RoundTripNV12") {
        auto        src = createPlanarImage(320, 240, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegQuality, 100);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_420_SemiPlanar_Rec709), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_420_SemiPlanar_Rec709);
}

// ---------------------------------------------------------------------------
// Cross-format planar 4:2:0 <-> RGB
// ---------------------------------------------------------------------------

TEST_CASE("JpegVideoCodec_EncodePlanar420DecodeRGB") {
        auto                        src = createPlanarImage(320, 240, PixelFormat::YUV8_420_Planar_Rec709);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, MediaConfig());
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::RGB8_sRGB), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::RGB8_sRGB);
}

TEST_CASE("JpegVideoCodec_EncodeRGBDecodePlanar420") {
        auto        src = createTestImage(320, 240);
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegSubsampling, ChromaSubsampling::YUV420);
        CompressedVideoPayload::Ptr pkt = encodeOneFrame(src, cfg);
        REQUIRE(pkt);
        auto decoded = decodeOneFrame(pkt, PixelFormat(PixelFormat::YUV8_420_Planar_Rec709), 320, 240);
        REQUIRE(decoded.isValid());
        CHECK(decoded->desc().pixelFormat().id() == PixelFormat::YUV8_420_Planar_Rec709);
}
