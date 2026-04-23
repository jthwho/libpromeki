/**
 * @file      tests/jpegxsvideocodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * End-to-end coverage for JpegXsVideoEncoder / JpegXsVideoDecoder via
 * the typed VideoCodec session API.  The pre-Phase-4 JpegXsImageCodec
 * base was retired with the ImageCodec abstraction; the SVT-JPEG-XS
 * plumbing now lives directly on the encoder / decoder session classes.
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include "codectesthelpers.h"

#include <promeki/jpegxsvideocodec.h>
#include <promeki/videocodec.h>
#include <promeki/videoencoder.h>
#include <promeki/videodecoder.h>
#include <promeki/mediaconfig.h>
#include <promeki/mediapacket.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

// Fill a planar YUV Image with a deterministic pattern.  Works for
// 8-bit (uint8_t samples) and 10/12-bit LE (uint16_t samples).
static Image makePlanarYUV(int width, int height, PixelFormat::ID pd, int bitDepth, bool is420) {
        Image img(width, height, pd);
        if(!img.isValid()) return img;
        const int chromaMid = (bitDepth == 8) ? 128 : (bitDepth == 10) ? 512 : 2048;
        const int lumaMax   = (bitDepth == 8) ? 219 : (bitDepth == 10) ? 876 : 3504;
        const size_t pixSize = (bitDepth > 8) ? 2 : 1;
        auto store = [&](uint8_t *row, int x, int val) {
                if(pixSize == 1) {
                        row[x] = (uint8_t)val;
                } else {
                        row[x * 2 + 0] = (uint8_t)(val & 0xFF);
                        row[x * 2 + 1] = (uint8_t)((val >> 8) & 0xFF);
                }
        };

        // Luma plane — horizontal ramp.
        {
                uint8_t *plane = static_cast<uint8_t *>(img.data(0));
                const size_t stride = img.lineStride(0);
                for(int y = 0; y < height; y++) {
                        uint8_t *row = plane + y * stride;
                        for(int x = 0; x < width; x++) {
                                store(row, x, ((x * lumaMax) / width) + 16);
                        }
                }
        }
        // Chroma planes — flat midpoint.
        const int chromaW = width / 2;
        const int chromaH = is420 ? (height / 2) : height;
        for(size_t p = 1; p < img.pixelFormat().planeCount(); p++) {
                uint8_t *plane = static_cast<uint8_t *>(img.data(p));
                const size_t stride = img.lineStride(p);
                for(int y = 0; y < chromaH; y++) {
                        uint8_t *row = plane + y * stride;
                        for(int x = 0; x < chromaW; x++) {
                                store(row, x, chromaMid);
                        }
                }
        }
        return img;
}

static Image makePlanarRGB8(int w, int h) {
        Image img(w, h, PixelFormat(PixelFormat::RGB8_Planar_sRGB));
        if(!img.isValid()) return img;
        for(int p = 0; p < 3; p++) {
                uint8_t *plane = static_cast<uint8_t *>(img.data(p));
                const size_t stride = img.lineStride(p);
                for(int y = 0; y < h; y++) {
                        uint8_t *row = plane + y * stride;
                        for(int x = 0; x < w; x++) {
                                if(p == 0) row[x] = static_cast<uint8_t>(x * 255 / w);
                                else if(p == 1) row[x] = static_cast<uint8_t>(y * 255 / h);
                                else row[x] = 128;
                        }
                }
        }
        return img;
}

// ---------------------------------------------------------------------------
// One-shot helpers — push a single frame through a VideoCodec::JPEG_XS
// session.  Equivalent to the codectesthelpers.h utilities, exposed
// inline so the tests can pin a specific bpp / decomposition before
// submit.
// ---------------------------------------------------------------------------

static VideoPacket::Ptr encodeOneJxsFrame(const Image &src, const MediaConfig &cfg) {
        MediaConfig sessionCfg = cfg;
        // Pick a sensible default OutputPixelFormat — every JPEG XS
        // sub-format works, the encoder just needs a non-empty value
        // to stamp on the packet.
        if(!sessionCfg.contains(MediaConfig::OutputPixelFormat)) {
                sessionCfg.set(MediaConfig::OutputPixelFormat,
                               PixelFormat(PixelFormat::JPEG_XS_YUV8_422_Rec709));
        }
        auto encResult = VideoCodec(VideoCodec::JPEG_XS).createEncoder(&sessionCfg);
        if(error(encResult).isError()) return VideoPacket::Ptr();
        VideoEncoder *enc = value(encResult);
        Error err = enc->submitFrame(Image::Ptr::create(src));
        if(err.isError()) { delete enc; return VideoPacket::Ptr(); }
        VideoPacket::Ptr pkt = enc->receivePacket();
        delete enc;
        return pkt;
}

static Image decodeOneJxsFrame(const VideoPacket::Ptr &pkt, PixelFormat target,
                               int width, int height) {
        MediaConfig cfg;
        if(target.isValid()) cfg.set(MediaConfig::OutputPixelFormat, target);
        cfg.set(MediaConfig::VideoSize, Size2Du32(width, height));
        auto decResult = VideoCodec(VideoCodec::JPEG_XS).createDecoder(&cfg);
        if(error(decResult).isError()) return Image();
        VideoDecoder *dec = value(decResult);
        Error err = dec->submitPacket(pkt);
        if(err.isError()) { delete dec; return Image(); }
        Image::Ptr out = dec->receiveFrame();
        delete dec;
        return out.isValid() ? *out : Image();
}

// ---------------------------------------------------------------------------
// Encoder defaults & basic capabilities
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoEncoder_Defaults") {
        JpegXsVideoEncoder enc;
        CHECK(enc.bpp() == JpegXsVideoEncoder::DefaultBpp);
        CHECK(enc.decomposition() == JpegXsVideoEncoder::DefaultDecomposition);
}

TEST_CASE("JpegXsVideoEncoder_Clamping") {
        JpegXsVideoEncoder enc;
        auto setBpp = [&](int v) {
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegXsBpp, v);
                enc.configure(cfg);
        };
        auto setDecomposition = [&](int v) {
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegXsDecomposition, v);
                enc.configure(cfg);
        };
        setBpp(0);   CHECK(enc.bpp() == JpegXsVideoEncoder::DefaultBpp);
        setBpp(-5);  CHECK(enc.bpp() == JpegXsVideoEncoder::DefaultBpp);
        setBpp(6);   CHECK(enc.bpp() == 6);
        setDecomposition(-1); CHECK(enc.decomposition() == 0);
        setDecomposition(99); CHECK(enc.decomposition() == 5);
        setDecomposition(3);  CHECK(enc.decomposition() == 3);
}

// ---------------------------------------------------------------------------
// Codec registry — the typed VideoCodec::JPEG_XS path resolves a session
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_RegisteredAndCanCreate") {
        VideoCodec vc(VideoCodec::JPEG_XS);
        CHECK(vc.isValid());
        CHECK(vc.canEncode());
        CHECK(vc.canDecode());
}

// ---------------------------------------------------------------------------
// Invalid input
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoEncoder_InvalidInput") {
        auto encResult = VideoCodec(VideoCodec::JPEG_XS).createEncoder(nullptr);
        REQUIRE_FALSE(error(encResult).isError());
        VideoEncoder *enc = value(encResult);
        CHECK(enc->submitFrame(Image::Ptr()).isError());
        delete enc;
}

TEST_CASE("JpegXsVideoEncoder_RejectsUnsupportedPixelFormat") {
        // Interleaved RGB8 is not directly accepted by the encoder
        // (the CSC fast path lifts it to RGB8_Planar_sRGB before
        // submit).  When called with the raw interleaved input
        // through the session API directly, the underlying
        // SVT-JPEG-XS path rejects it.
        auto encResult = VideoCodec(VideoCodec::JPEG_XS).createEncoder(nullptr);
        REQUIRE_FALSE(error(encResult).isError());
        VideoEncoder *enc = value(encResult);
        // Force a configuration with a non-default OutputPixelFormat
        // for completeness; encoder still rejects the unsupported
        // input format on submitFrame.
        Image::Ptr img = Image::Ptr::create(64, 64, PixelFormat::RGB8_sRGB);
        CHECK(enc->submitFrame(img).isError());
        delete enc;
}

// ---------------------------------------------------------------------------
// Encode tags output with the correct compressed PixelFormat
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_EncodeTagsCompressedPd") {
        struct Case { PixelFormat::ID src; PixelFormat::ID compressed; int bitDepth; bool is420; };
        const Case cases[] = {
                { PixelFormat::YUV8_422_Planar_Rec709,     PixelFormat::JPEG_XS_YUV8_422_Rec709,  8,  false },
                { PixelFormat::YUV10_422_Planar_LE_Rec709, PixelFormat::JPEG_XS_YUV10_422_Rec709, 10, false },
                { PixelFormat::YUV12_422_Planar_LE_Rec709, PixelFormat::JPEG_XS_YUV12_422_Rec709, 12, false },
                { PixelFormat::YUV8_420_Planar_Rec709,     PixelFormat::JPEG_XS_YUV8_420_Rec709,  8,  true  },
                { PixelFormat::YUV10_420_Planar_LE_Rec709, PixelFormat::JPEG_XS_YUV10_420_Rec709, 10, true  },
                { PixelFormat::YUV12_420_Planar_LE_Rec709, PixelFormat::JPEG_XS_YUV12_420_Rec709, 12, true  },
        };
        for(const auto &c : cases) {
                Image src = makePlanarYUV(320, 240, c.src, c.bitDepth, c.is420);
                REQUIRE(src.isValid());
                MediaConfig cfg;
                cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(c.compressed));
                VideoPacket::Ptr pkt = encodeOneJxsFrame(src, cfg);
                REQUIRE_MESSAGE(pkt, "encode failed for ", (int)c.src);
                CHECK(pkt->pixelFormat().id() == c.compressed);
                CHECK(pkt->size() > 0);
                CHECK(pkt->hasFlag(VideoPacket::Keyframe));
        }
}

// ---------------------------------------------------------------------------
// Round trip (planar YUV 4:2:2 / 4:2:0 × 8/10/12-bit)
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_RoundTripPlanar") {
        struct Case { PixelFormat::ID src; int bitDepth; bool is420; };
        const Case cases[] = {
                { PixelFormat::YUV8_422_Planar_Rec709,     8,  false },
                { PixelFormat::YUV10_422_Planar_LE_Rec709, 10, false },
                { PixelFormat::YUV12_422_Planar_LE_Rec709, 12, false },
                { PixelFormat::YUV8_420_Planar_Rec709,     8,  true  },
                { PixelFormat::YUV10_420_Planar_LE_Rec709, 10, true  },
                { PixelFormat::YUV12_420_Planar_LE_Rec709, 12, true  },
        };
        for(const auto &c : cases) {
                Image src = makePlanarYUV(320, 240, c.src, c.bitDepth, c.is420);
                REQUIRE(src.isValid());
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegXsBpp, 8);  // near-lossless
                VideoPacket::Ptr pkt = encodeOneJxsFrame(src, cfg);
                REQUIRE_MESSAGE(pkt, "encode failed for ", (int)c.src);
                Image dec = decodeOneJxsFrame(pkt, PixelFormat(c.src), 320, 240);
                REQUIRE_MESSAGE(dec.isValid(), "decode failed for ", (int)c.src);
                CHECK(dec.width() == 320);
                CHECK(dec.height() == 240);
                CHECK(dec.pixelFormat().id() == c.src);
        }
}

// ---------------------------------------------------------------------------
// Default decode target (no OutputPixelFormat in MediaConfig)
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_DefaultDecodeTarget") {
        Image src = makePlanarYUV(160, 120, PixelFormat::YUV10_422_Planar_LE_Rec709, 10, false);
        REQUIRE(src.isValid());
        MediaConfig encCfg;
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, encCfg);
        REQUIRE(pkt);

        // Decode without OutputPixelFormat — the decoder picks the
        // natural target for the input bitstream.
        Image dec = decodeOneJxsFrame(pkt, PixelFormat(), 160, 120);
        REQUIRE(dec.isValid());
        CHECK(dec.pixelFormat().id() == PixelFormat::YUV10_422_Planar_LE_Rec709);
}

// ---------------------------------------------------------------------------
// Decode target mismatch is rejected, not silently corrupted
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_DecodeTargetMismatch") {
        Image src = makePlanarYUV(160, 120, PixelFormat::YUV8_422_Planar_Rec709, 8, false);
        REQUIRE(src.isValid());
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, MediaConfig());
        REQUIRE(pkt);

        // Encode is 4:2:2 and we ask the decoder to produce 4:2:0 —
        // the subsampling mismatch must be rejected.
        Image dec = decodeOneJxsFrame(pkt,
                PixelFormat(PixelFormat::YUV8_420_Planar_Rec709), 160, 120);
        CHECK_FALSE(dec.isValid());
}

// ---------------------------------------------------------------------------
// Image::convert dispatch path — JPEG XS → RGBA8_sRGB
// ---------------------------------------------------------------------------
//
// SDLPlayerTask relies on the codec layer to decode compressed
// frames into RGBA8_sRGB before handing them to the widget.

TEST_CASE("JpegXsVideoCodec_ImageConvertToRgba8") {
        Image src = makePlanarYUV(64, 48, PixelFormat::YUV10_422_Planar_LE_Rec709, 10, false);
        REQUIRE(src.isValid());
        MediaConfig encCfg;
        encCfg.set(MediaConfig::JpegXsBpp, 8);
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_XS_YUV10_422_Rec709));
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, encCfg);
        REQUIRE(pkt);

        // Wrap the encoded packet bytes back into a compressed Image
        // so the codectesthelpers helper can drive the decode + CSC.
        Image jxs = Image::fromCompressedData(
                pkt->view().data(), pkt->size(),
                64, 48, pkt->pixelFormat());
        REQUIRE(jxs.isValid());
        REQUIRE(jxs.isCompressed());

        Image rgba = promeki::tests::decodeCompressedToImage(
                jxs, PixelFormat(PixelFormat::RGBA8_sRGB));
        REQUIRE(rgba.isValid());
        CHECK_FALSE(rgba.isCompressed());
        CHECK(rgba.pixelFormat().id() == PixelFormat::RGBA8_sRGB);
        CHECK(rgba.width()  == 64);
        CHECK(rgba.height() == 48);
}

// ---------------------------------------------------------------------------
// Metadata propagates through encode (PTS / metadata stamped on packet)
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_MetadataPreserved") {
        Image src = makePlanarYUV(128, 96, PixelFormat::YUV8_422_Planar_Rec709, 8, false);
        src.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 2, 15, 30, 12));
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, MediaConfig());
        REQUIRE(pkt);
        Timecode tc = pkt->metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 2);
        CHECK(tc.min() == 15);
        CHECK(tc.frame() == 12);
}

// ---------------------------------------------------------------------------
// configure() from MediaConfig
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoEncoder_ConfigureFromMediaConfig") {
        SUBCASE("JpegXsBpp flows through configure()") {
                Image src = makePlanarYUV(256, 192, PixelFormat::YUV8_422_Planar_Rec709, 8, false);
                REQUIRE(src.isValid());
                MediaConfig loCfg; loCfg.set(MediaConfig::JpegXsBpp, 2);
                MediaConfig hiCfg; hiCfg.set(MediaConfig::JpegXsBpp, 8);
                VideoPacket::Ptr loPkt = encodeOneJxsFrame(src, loCfg);
                VideoPacket::Ptr hiPkt = encodeOneJxsFrame(src, hiCfg);
                REQUIRE(loPkt);
                REQUIRE(hiPkt);
                CHECK(hiPkt->size() > loPkt->size());
        }

        SUBCASE("JpegXsDecomposition flows through configure()") {
                JpegXsVideoEncoder enc;
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegXsDecomposition, 4);
                enc.configure(cfg);
                CHECK(enc.decomposition() == 4);
        }

        SUBCASE("Empty MediaConfig leaves session defaults intact") {
                JpegXsVideoEncoder configured;
                configured.configure(MediaConfig());
                JpegXsVideoEncoder baseline;
                CHECK(configured.bpp() == baseline.bpp());
                CHECK(configured.decomposition() == baseline.decomposition());
        }
}

// ---------------------------------------------------------------------------
// Planar RGB encode / decode path
// ---------------------------------------------------------------------------

TEST_CASE("JpegXsVideoCodec_PlanarRGB8_Encode") {
        Image src = makePlanarRGB8(320, 240);
        REQUIRE(src.isValid());
        MediaConfig cfg;
        cfg.set(MediaConfig::JpegXsBpp, 8);
        cfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_XS_RGB8_sRGB));
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, cfg);
        REQUIRE(pkt);
        CHECK(pkt->pixelFormat().id() == PixelFormat::JPEG_XS_RGB8_sRGB);
        CHECK(pkt->size() > 0);
}

TEST_CASE("JpegXsVideoCodec_PlanarRGB8_RoundTrip") {
        Image src = makePlanarRGB8(256, 192);
        REQUIRE(src.isValid());
        MediaConfig encCfg;
        encCfg.set(MediaConfig::JpegXsBpp, 8);
        encCfg.set(MediaConfig::OutputPixelFormat, PixelFormat(PixelFormat::JPEG_XS_RGB8_sRGB));
        VideoPacket::Ptr pkt = encodeOneJxsFrame(src, encCfg);
        REQUIRE(pkt);

        // Decode to planar RGB (natural target).
        Image dec = decodeOneJxsFrame(pkt, PixelFormat(), 256, 192);
        REQUIRE(dec.isValid());
        CHECK(dec.pixelFormat().id() == PixelFormat::RGB8_Planar_sRGB);
        CHECK(dec.width() == 256);
        CHECK(dec.height() == 192);

        // Convert planar → interleaved via CSC fast path.
        Image rgb = dec.convert(PixelFormat(PixelFormat::RGB8_sRGB), dec.metadata());
        REQUIRE(rgb.isValid());
        CHECK(rgb.pixelFormat().id() == PixelFormat::RGB8_sRGB);
}

TEST_CASE("JpegXsVideoCodec_RGB8_ImageConvertRoundTrip") {
        Image src(128, 96, PixelFormat(PixelFormat::RGB8_sRGB));
        REQUIRE(src.isValid());
        uint8_t *data = static_cast<uint8_t *>(src.data(0));
        const size_t stride = src.lineStride(0);
        for(int y = 0; y < 96; y++) {
                uint8_t *row = data + y * stride;
                for(int x = 0; x < 128; x++) {
                        row[x * 3 + 0] = static_cast<uint8_t>(x * 2);
                        row[x * 3 + 1] = static_cast<uint8_t>(y * 2);
                        row[x * 3 + 2] = 128;
                }
        }

        Image jxs = promeki::tests::encodeImageToCompressed(
                src, PixelFormat(PixelFormat::JPEG_XS_RGB8_sRGB));
        REQUIRE(jxs.isValid());
        CHECK(jxs.isCompressed());
        CHECK(jxs.pixelFormat().id() == PixelFormat::JPEG_XS_RGB8_sRGB);

        Image decoded = promeki::tests::decodeCompressedToImage(
                jxs, PixelFormat(PixelFormat::RGB8_sRGB));
        REQUIRE(decoded.isValid());
        CHECK_FALSE(decoded.isCompressed());
        CHECK(decoded.pixelFormat().id() == PixelFormat::RGB8_sRGB);
        CHECK(decoded.width() == 128);
        CHECK(decoded.height() == 96);
}
