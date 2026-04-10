/**
 * @file      tests/jpegxsimagecodec.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/jpegxsimagecodec.h>
#include <promeki/codec.h>
#include <promeki/mediaconfig.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/metadata.h>
#include <promeki/timecode.h>

using namespace promeki;

// Fill a planar YUV Image with a deterministic pattern.  Works for
// 8-bit (uint8_t samples) and 10/12-bit LE (uint16_t samples) so the
// same helper covers all round-trip cases.  Chroma planes are filled
// with a flat mid-grey, luma is a horizontal ramp — the specifics
// don't matter beyond "reproducible and non-trivial so JPEG XS
// actually has something to compress".
static Image makePlanarYUV(int width, int height, PixelDesc::ID pd, int bitDepth, bool is420) {
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
        // Chroma planes — flat midpoint.  Dimensions come from the
        // subsampling: 4:2:2 halves width, 4:2:0 halves both.
        const int chromaW = width / 2;
        const int chromaH = is420 ? (height / 2) : height;
        for(size_t p = 1; p < img.pixelDesc().planeCount(); p++) {
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

// ============================================================================
// Defaults
// ============================================================================

TEST_CASE("JpegXsImageCodec_Defaults") {
        JpegXsImageCodec codec;
        CHECK(codec.canEncode());
        CHECK(codec.canDecode());
        CHECK(codec.bpp() == JpegXsImageCodec::DefaultBpp);
        CHECK(codec.decomposition() == JpegXsImageCodec::DefaultDecomposition);
}

// ============================================================================
// Setter clamping
// ============================================================================

TEST_CASE("JpegXsImageCodec_Clamping") {
        JpegXsImageCodec codec;
        codec.setBpp(0);   CHECK(codec.bpp() == JpegXsImageCodec::DefaultBpp);
        codec.setBpp(-5);  CHECK(codec.bpp() == JpegXsImageCodec::DefaultBpp);
        codec.setBpp(6);   CHECK(codec.bpp() == 6);
        codec.setDecomposition(-1); CHECK(codec.decomposition() == 0);
        codec.setDecomposition(99); CHECK(codec.decomposition() == 5);
        codec.setDecomposition(3);  CHECK(codec.decomposition() == 3);
}

// ============================================================================
// Registry
// ============================================================================

TEST_CASE("JpegXsImageCodec_Registry") {
        CHECK(ImageCodec::registeredCodecs().contains("jpegxs"));
        ImageCodec *codec = ImageCodec::createCodec("jpegxs");
        REQUIRE(codec != nullptr);
        CHECK(codec->canEncode());
        CHECK(codec->canDecode());
        delete codec;
}

// ============================================================================
// Invalid input
// ============================================================================

TEST_CASE("JpegXsImageCodec_InvalidInput") {
        JpegXsImageCodec codec;
        CHECK_FALSE(codec.encode(Image()).isValid());
        CHECK_FALSE(codec.decode(Image()).isValid());
}

TEST_CASE("JpegXsImageCodec_RejectsUnsupportedPixelFormat") {
        // RGB8 is not in the initial encoder scope.
        JpegXsImageCodec codec;
        Image img(64, 64, PixelDesc::RGB8_sRGB);
        CHECK_FALSE(codec.encode(img).isValid());
}

// ============================================================================
// Encode tags output with the correct compressed PixelDesc
// ============================================================================

TEST_CASE("JpegXsImageCodec_EncodeTagsCompressedPd") {
        struct Case { PixelDesc::ID src; PixelDesc::ID compressed; int bitDepth; bool is420; };
        const Case cases[] = {
                { PixelDesc::YUV8_422_Planar_Rec709,     PixelDesc::JPEG_XS_YUV8_422_Rec709,  8,  false },
                { PixelDesc::YUV10_422_Planar_LE_Rec709, PixelDesc::JPEG_XS_YUV10_422_Rec709, 10, false },
                { PixelDesc::YUV12_422_Planar_LE_Rec709, PixelDesc::JPEG_XS_YUV12_422_Rec709, 12, false },
                { PixelDesc::YUV8_420_Planar_Rec709,     PixelDesc::JPEG_XS_YUV8_420_Rec709,  8,  true  },
                { PixelDesc::YUV10_420_Planar_LE_Rec709, PixelDesc::JPEG_XS_YUV10_420_Rec709, 10, true  },
                { PixelDesc::YUV12_420_Planar_LE_Rec709, PixelDesc::JPEG_XS_YUV12_420_Rec709, 12, true  },
        };
        for(const auto &c : cases) {
                JpegXsImageCodec codec;
                Image src = makePlanarYUV(320, 240, c.src, c.bitDepth, c.is420);
                REQUIRE(src.isValid());
                Image enc = codec.encode(src);
                REQUIRE_MESSAGE(enc.isValid(), "encode failed for ", (int)c.src);
                CHECK(enc.isCompressed());
                CHECK(enc.pixelDesc().id() == c.compressed);
                CHECK(enc.compressedSize() > 0);
        }
}

// ============================================================================
// Round trip (planar YUV 4:2:2 / 4:2:0 × 8/10/12-bit)
// ============================================================================

TEST_CASE("JpegXsImageCodec_RoundTripPlanar") {
        struct Case { PixelDesc::ID src; int bitDepth; bool is420; };
        const Case cases[] = {
                { PixelDesc::YUV8_422_Planar_Rec709,     8,  false },
                { PixelDesc::YUV10_422_Planar_LE_Rec709, 10, false },
                { PixelDesc::YUV12_422_Planar_LE_Rec709, 12, false },
                { PixelDesc::YUV8_420_Planar_Rec709,     8,  true  },
                { PixelDesc::YUV10_420_Planar_LE_Rec709, 10, true  },
                { PixelDesc::YUV12_420_Planar_LE_Rec709, 12, true  },
        };
        for(const auto &c : cases) {
                JpegXsImageCodec codec;
                codec.setBpp(8);  // near-lossless for the round-trip
                Image src = makePlanarYUV(320, 240, c.src, c.bitDepth, c.is420);
                REQUIRE(src.isValid());
                Image enc = codec.encode(src);
                REQUIRE_MESSAGE(enc.isValid(), "encode failed for ", (int)c.src);
                Image dec = codec.decode(enc, c.src);
                REQUIRE_MESSAGE(dec.isValid(), "decode failed for ", (int)c.src);
                CHECK(dec.width() == 320);
                CHECK(dec.height() == 240);
                CHECK(dec.pixelDesc().id() == c.src);
        }
}

// ============================================================================
// Default decode target (no outputFormat argument)
// ============================================================================

TEST_CASE("JpegXsImageCodec_DefaultDecodeTarget") {
        JpegXsImageCodec codec;
        Image src = makePlanarYUV(160, 120, PixelDesc::YUV10_422_Planar_LE_Rec709, 10, false);
        REQUIRE(src.isValid());
        Image enc = codec.encode(src);
        REQUIRE(enc.isValid());
        Image dec = codec.decode(enc);  // no outputFormat
        REQUIRE(dec.isValid());
        CHECK(dec.pixelDesc().id() == PixelDesc::YUV10_422_Planar_LE_Rec709);
}

// ============================================================================
// Decode target mismatch is rejected, not silently corrupted
// ============================================================================

TEST_CASE("JpegXsImageCodec_DecodeTargetMismatch") {
        JpegXsImageCodec codec;
        // Encode as 4:2:2 and try to decode as 4:2:0 — the subsampling
        // mismatch must be rejected since the decoder can't reshape
        // the plane grid.
        Image src = makePlanarYUV(160, 120, PixelDesc::YUV8_422_Planar_Rec709, 8, false);
        REQUIRE(src.isValid());
        Image enc = codec.encode(src);
        REQUIRE(enc.isValid());
        Image dec = codec.decode(enc, PixelDesc::YUV8_420_Planar_Rec709);
        CHECK_FALSE(dec.isValid());
}

// ============================================================================
// Image::convert dispatch path — JPEG XS → RGBA8_sRGB
// ============================================================================
//
// SDLPlayerTask relies on Image::convert() to decode compressed
// frames into RGBA8_sRGB before handing them to the widget.  The
// dispatch chain has two hops: the codec layer decodes JPEG XS to
// its native YUV target, then the CSC pipeline lifts that to
// RGBA8_sRGB.  This test exercises both hops in one call so any
// regression in either layer surfaces here.
TEST_CASE("JpegXsImageCodec_ImageConvertToRgba8") {
        // Build a real JPEG XS bitstream by encoding a synthetic
        // planar YUV source — we cannot use Image::fromCompressedData
        // with random bytes here because the SVT decoder will
        // reject anything that isn't a valid codestream.
        JpegXsImageCodec codec;
        codec.setBpp(8);
        Image src = makePlanarYUV(64, 48,
                PixelDesc::YUV10_422_Planar_LE_Rec709, 10, false);
        REQUIRE(src.isValid());
        Image jxs = codec.encode(src);
        REQUIRE(jxs.isValid());
        REQUIRE(jxs.isCompressed());

        // Now exercise the dispatch path the SDL task uses: ask
        // Image::convert() to land on RGBA8_sRGB.  This should
        // decode JPEG XS → YUV10_422_Planar_LE → RGBA8_sRGB
        // automatically via the codec registry and CSC pipeline.
        Image rgba = jxs.convert(PixelDesc(PixelDesc::RGBA8_sRGB),
                                  jxs.metadata());
        REQUIRE(rgba.isValid());
        CHECK_FALSE(rgba.isCompressed());
        CHECK(rgba.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
        CHECK(rgba.width()  == 64);
        CHECK(rgba.height() == 48);
}

// ============================================================================
// Metadata survives encode
// ============================================================================

TEST_CASE("JpegXsImageCodec_MetadataPreserved") {
        JpegXsImageCodec codec;
        Image src = makePlanarYUV(128, 96, PixelDesc::YUV8_422_Planar_Rec709, 8, false);
        src.metadata().set(Metadata::Timecode, Timecode(Timecode::NDF24, 2, 15, 30, 12));
        Image enc = codec.encode(src);
        REQUIRE(enc.isValid());
        Timecode tc = enc.metadata().get(Metadata::Timecode).get<Timecode>();
        CHECK(tc.hour() == 2);
        CHECK(tc.min() == 15);
        CHECK(tc.frame() == 12);
}

// ============================================================================
// configure() from MediaConfig
// ============================================================================
//
// Matches the JpegImageCodec test contract: values from a MediaConfig
// must flow through configure() and influence subsequent encodes.

TEST_CASE("JpegXsImageCodec_ConfigureFromMediaConfig") {
        SUBCASE("JpegXsBpp flows through configure()") {
                ImageCodec *lo = ImageCodec::createCodec("jpegxs");
                ImageCodec *hi = ImageCodec::createCodec("jpegxs");
                REQUIRE(lo != nullptr);
                REQUIRE(hi != nullptr);

                MediaConfig loCfg;
                loCfg.set(MediaConfig::JpegXsBpp, 2);
                lo->configure(loCfg);

                MediaConfig hiCfg;
                hiCfg.set(MediaConfig::JpegXsBpp, 8);
                hi->configure(hiCfg);

                Image src = makePlanarYUV(256, 192, PixelDesc::YUV8_422_Planar_Rec709, 8, false);
                REQUIRE(src.isValid());
                Image encLo = lo->encode(src);
                Image encHi = hi->encode(src);
                REQUIRE(encLo.isValid());
                REQUIRE(encHi.isValid());
                CHECK(encHi.compressedSize() > encLo.compressedSize());

                delete lo;
                delete hi;
        }

        SUBCASE("JpegXsDecomposition flows through configure()") {
                JpegXsImageCodec codec;
                MediaConfig cfg;
                cfg.set(MediaConfig::JpegXsDecomposition, 4);
                codec.configure(cfg);
                CHECK(codec.decomposition() == 4);
        }

        SUBCASE("Empty MediaConfig leaves codec defaults intact") {
                JpegXsImageCodec configured;
                configured.configure(MediaConfig());
                JpegXsImageCodec baseline;
                CHECK(configured.bpp() == baseline.bpp());
                CHECK(configured.decomposition() == baseline.decomposition());
        }
}

// ============================================================================
// Planar RGB encode / decode path
// ============================================================================
//
// The codec accepts RGB8_Planar_sRGB (3 equal-sized planar planes)
// via COLOUR_FORMAT_PLANAR_YUV444_OR_RGB.  Interleaved RGB reaches
// the codec through the CSC fast path (RGB8_sRGB → RGB8_Planar_sRGB)
// and the reverse on decode.

static Image makePlanarRGB8(int w, int h) {
        Image img(w, h, PixelDesc(PixelDesc::RGB8_Planar_sRGB));
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

TEST_CASE("JpegXsImageCodec_PlanarRGB8_Encode") {
        JpegXsImageCodec codec;
        codec.setBpp(8);
        Image src = makePlanarRGB8(320, 240);
        REQUIRE(src.isValid());

        Image enc = codec.encode(src);
        REQUIRE(enc.isValid());
        CHECK(enc.isCompressed());
        CHECK(enc.pixelDesc().id() == PixelDesc::JPEG_XS_RGB8_sRGB);
        CHECK(enc.compressedSize() > 0);
        CHECK(enc.width() == 320);
        CHECK(enc.height() == 240);
}

TEST_CASE("JpegXsImageCodec_PlanarRGB8_RoundTrip") {
        JpegXsImageCodec codec;
        codec.setBpp(8);
        Image src = makePlanarRGB8(256, 192);
        REQUIRE(src.isValid());

        Image enc = codec.encode(src);
        REQUIRE(enc.isValid());
        REQUIRE(enc.isCompressed());

        // Decode to planar RGB (native target).
        Image dec = codec.decode(enc);
        REQUIRE(dec.isValid());
        CHECK(!dec.isCompressed());
        CHECK(dec.pixelDesc().id() == PixelDesc::RGB8_Planar_sRGB);
        CHECK(dec.width() == 256);
        CHECK(dec.height() == 192);

        // Convert planar → interleaved via CSC fast path.
        Image rgb = dec.convert(PixelDesc(PixelDesc::RGB8_sRGB), dec.metadata());
        REQUIRE(rgb.isValid());
        CHECK(rgb.pixelDesc().id() == PixelDesc::RGB8_sRGB);
}

TEST_CASE("JpegXsImageCodec_RGB8_ImageConvertRoundTrip") {
        // Full chain via Image::convert():
        // RGB8 → [CSC] → RGB8_Planar → [codec] → JPEG_XS_RGB8
        // JPEG_XS_RGB8 → [codec] → RGB8_Planar → [CSC] → RGB8
        Image src(128, 96, PixelDesc(PixelDesc::RGB8_sRGB));
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

        Image jxs = src.convert(PixelDesc(PixelDesc::JPEG_XS_RGB8_sRGB),
                                src.metadata());
        REQUIRE(jxs.isValid());
        CHECK(jxs.isCompressed());
        CHECK(jxs.pixelDesc().id() == PixelDesc::JPEG_XS_RGB8_sRGB);

        Image decoded = jxs.convert(PixelDesc(PixelDesc::RGB8_sRGB),
                                    jxs.metadata());
        REQUIRE(decoded.isValid());
        CHECK(!decoded.isCompressed());
        CHECK(decoded.pixelDesc().id() == PixelDesc::RGB8_sRGB);
        CHECK(decoded.width() == 128);
        CHECK(decoded.height() == 96);
}
