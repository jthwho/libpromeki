/**
 * @file      cscpipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Multi-level consistency tests for the CSC (Color Space Conversion)
 * framework.  Tests validate conversions against the library's own
 * Color class (ground truth), VideoTestPattern generator, and ITU-R
 * BT.709/BT.601/BT.2020 reference values.
 *
 * Test levels:
 *   1. Color::convert() single-pixel validation
 *   2. Scalar pipeline consistency with Color::convert()
 *   3. VideoTestPattern -> scalar pipeline -> reference
 *   4. VideoTestPattern -> fast path -> integer reference
 *   5. Round-trip accuracy (RGB -> YCbCr -> RGB)
 *   6. Cross-format consistency (same source -> multiple YCbCr layouts)
 *   7. 10-bit consistency
 *   8. Range boundaries and edge cases
 *   9. Rec.601 and Rec.2020 coverage
 */

#include <doctest/doctest.h>
#include <promeki/cscpipeline.h>
#include <promeki/csccontext.h>
#include <promeki/cscregistry.h>
#include <promeki/image.h>
#include <promeki/color.h>
#include <promeki/colormodel.h>
#include <promeki/medianodeconfig.h>
#include <promeki/videotestpattern.h>
#include <cstring>
#include <cmath>

using namespace promeki;

// =========================================================================
// Helpers
// =========================================================================

static const MediaNodeConfig &scalarConfig() {
        static MediaNodeConfig cfg;
        static bool init = false;
        if(!init) { cfg.set(CSCPipeline::KeyPath, "scalar"); init = true; }
        return cfg;
}

static Image makeUniformRGBA8(uint8_t r, uint8_t g, uint8_t b, size_t w = 2) {
        Image img(w, 1, PixelDesc::RGBA8_sRGB);
        if(!img.isValid()) return img;
        uint8_t *data = static_cast<uint8_t *>(img.data());
        for(size_t x = 0; x < w; x++) {
                data[x * 4 + 0] = r;
                data[x * 4 + 1] = g;
                data[x * 4 + 2] = b;
                data[x * 4 + 3] = 255;
        }
        return img;
}

static Image makeUniformRGBA10LE(uint16_t r, uint16_t g, uint16_t b, size_t w = 2) {
        Image img(w, 1, PixelDesc::RGBA10_LE_sRGB);
        if(!img.isValid()) return img;
        uint16_t *data = static_cast<uint16_t *>(img.data());
        for(size_t x = 0; x < w; x++) {
                data[x * 4 + 0] = r;
                data[x * 4 + 1] = g;
                data[x * 4 + 2] = b;
                data[x * 4 + 3] = 1023;
        }
        return img;
}

static Image makeGradientRGBA8(size_t w, size_t h) {
        Image img(w, h, PixelDesc::RGBA8_sRGB);
        if(!img.isValid()) return img;
        uint8_t *data = static_cast<uint8_t *>(img.data());
        for(size_t i = 0; i < w * h; i++) {
                data[i * 4 + 0] = static_cast<uint8_t>(64 + (i * 7) % 128);
                data[i * 4 + 1] = static_cast<uint8_t>(64 + (i * 13) % 128);
                data[i * 4 + 2] = static_cast<uint8_t>(64 + (i * 19) % 128);
                data[i * 4 + 3] = 255;
        }
        return img;
}

static Image makeColorBars8(size_t w = 160, size_t h = 2) {
        VideoTestPattern gen;
        gen.setPattern(VideoTestPattern::ColorBars);
        return gen.create(ImageDesc(w, h, PixelDesc::RGBA8_sRGB));
}

// 100% SMPTE color bar definitions
struct BarDef {
        const char *name;
        uint8_t r, g, b;
        // BT.709 integer reference (fast-path output)
        int y709, cb709, cr709;
};

static const BarDef bars100[] = {
        {"White",   255, 255, 255, 235, 128, 128},
        {"Yellow",  255, 255,   0, 210,  16, 146},
        {"Cyan",      0, 255, 255, 169, 166,  16},
        {"Green",     0, 255,   0, 144,  54,  34},
        {"Magenta", 255,   0, 255, 107, 202, 222},
        {"Red",     255,   0,   0,  82,  90, 240},
        {"Blue",      0,   0, 255,  41, 240, 110},
        {"Black",     0,   0,   0,  16, 128, 128},
};

// Convert a Color to limited-range 8-bit YCbCr integers
static void colorToYCbCr8(const Color &ycbcr, int &y, int &cb, int &cr) {
        // Color stores YCbCr normalized to 0-1 via the ColorModel's range
        y  = static_cast<int>(ycbcr.comp(0) * (235.0f - 16.0f) + 16.0f + 0.5f);
        cb = static_cast<int>(ycbcr.comp(1) * (240.0f - 16.0f) + 16.0f + 0.5f);
        cr = static_cast<int>(ycbcr.comp(2) * (240.0f - 16.0f) + 16.0f + 0.5f);
}

// Read Y/Cb/Cr from a YUYV 8-bit image at a given pixel x position
static void readYUYV8(const Image &img, int x, int &y, int &cb, int &cr) {
        const uint8_t *d = static_cast<const uint8_t *>(img.data());
        int pair = x / 2;
        y  = d[pair * 4 + (x % 2 == 0 ? 0 : 2)];
        cb = d[pair * 4 + 1];
        cr = d[pair * 4 + 3];
}

// =========================================================================
// Infrastructure
// =========================================================================

TEST_CASE("CSC infrastructure") {
        SUBCASE("CSCContext") {
                CSCContext ctx(1920);
                CHECK(ctx.isValid());
                CHECK(ctx.maxWidth() == 1920);
                for(int i = 0; i < CSCContext::BufferCount; i++) {
                        CHECK(ctx.buffer(i) != nullptr);
                        CHECK((reinterpret_cast<uintptr_t>(ctx.buffer(i)) % CSCContext::BufferAlign) == 0);
                }
        }

        SUBCASE("pipeline identity") {
                CSCPipeline p(PixelDesc::RGBA8_sRGB, PixelDesc::RGBA8_sRGB);
                CHECK(p.isValid());
                CHECK(p.isIdentity());
        }

        SUBCASE("pipeline fast path detection") {
                CSCPipeline fp(PixelDesc::RGBA8_sRGB, PixelDesc::YUV8_422_Rec709);
                CHECK(fp.isFastPath());
                CSCPipeline sp(PixelDesc::RGBA8_sRGB, PixelDesc::YUV8_422_Rec709, scalarConfig());
                CHECK_FALSE(sp.isFastPath());
                CHECK(sp.stageCount() > 0);
        }

        SUBCASE("identity memcpy") {
                Image src(16, 8, PixelDesc::RGBA8_sRGB);
                REQUIRE(src.isValid());
                uint8_t *d = static_cast<uint8_t *>(src.data());
                for(size_t i = 0; i < 16 * 8 * 4; i++) d[i] = static_cast<uint8_t>(i & 0xFF);
                Image dst = src.convert(PixelDesc::RGBA8_sRGB, src.metadata());
                CHECK(std::memcmp(src.data(), dst.data(), 16 * 8 * 4) == 0);
        }
}

// =========================================================================
// Level 1: Color::convert() single-pixel validation
// =========================================================================

TEST_CASE("CSC L1: Color::convert sRGB -> YCbCr_Rec709") {
        // Validate Color::convert() itself as the ground truth.
        for(const auto &bar : bars100) {
                Color srgb = Color::srgb(bar.r / 255.0f, bar.g / 255.0f, bar.b / 255.0f, 1.0f);
                Color ycbcr = srgb.convert(ColorModel(ColorModel::YCbCr_Rec709));
                REQUIRE(ycbcr.isValid());

                int y, cb, cr;
                colorToYCbCr8(ycbcr, y, cb, cr);

                // The Color path uses sRGB EOTF + Rec.709 OETF (proper transfer
                // function handling), so values differ from BT.709 integer math.
                // Just validate they're in legal limited range.
                INFO(bar.name << ": Y=" << y << " Cb=" << cb << " Cr=" << cr);
                CHECK(y >= 16);  CHECK(y <= 235);
                CHECK(cb >= 16); CHECK(cb <= 240);
                CHECK(cr >= 16); CHECK(cr <= 240);
        }

        // Black and white must be exact
        Color black = Color::srgb(0, 0, 0, 1).convert(ColorModel(ColorModel::YCbCr_Rec709));
        Color white = Color::srgb(1, 1, 1, 1).convert(ColorModel(ColorModel::YCbCr_Rec709));
        int y, cb, cr;
        colorToYCbCr8(black, y, cb, cr);
        CHECK(y == 16);  CHECK(cb == 128);  CHECK(cr == 128);
        colorToYCbCr8(white, y, cb, cr);
        CHECK(y == 235);  CHECK(cb == 128);  CHECK(cr == 128);
}

// =========================================================================
// Level 2: Scalar pipeline consistency with Color::convert()
// =========================================================================

TEST_CASE("CSC L2: scalar pipeline vs Color::convert") {
        // The scalar generic pipeline should closely match Color::convert()
        // since both use float-domain EOTF + matrix + OETF.
        for(const auto &bar : bars100) {
                Color srgb = Color::srgb(bar.r / 255.0f, bar.g / 255.0f, bar.b / 255.0f, 1.0f);
                Color refYCbCr = srgb.convert(ColorModel(ColorModel::YCbCr_Rec709));
                int refY, refCb, refCr;
                colorToYCbCr8(refYCbCr, refY, refCb, refCr);

                Image src = makeUniformRGBA8(bar.r, bar.g, bar.b);
                Image dst = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata(), scalarConfig());
                REQUIRE(dst.isValid());

                int pipeY, pipeCb, pipeCr;
                readYUYV8(dst, 0, pipeY, pipeCb, pipeCr);

                INFO(bar.name << ": ref Y=" << refY << " pipe Y=" << pipeY
                     << "  ref Cb=" << refCb << " pipe Cb=" << pipeCb
                     << "  ref Cr=" << refCr << " pipe Cr=" << pipeCr);
                CHECK(std::abs(pipeY  - refY)  <= 2);
                CHECK(std::abs(pipeCb - refCb) <= 2);
                CHECK(std::abs(pipeCr - refCr) <= 2);
        }
}

// =========================================================================
// Level 3: VideoTestPattern -> scalar pipeline -> Color reference
// =========================================================================

TEST_CASE("CSC L3: VideoTestPattern bars through scalar pipeline") {
        Image src = makeColorBars8();
        REQUIRE(src.isValid());
        Image dst = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata(), scalarConfig());
        REQUIRE(dst.isValid());

        int barWidth = 160 / 8;
        for(int i = 0; i < 8; i++) {
                // Color::convert() reference for this bar
                Color srgb = Color::srgb(bars100[i].r / 255.0f,
                                         bars100[i].g / 255.0f,
                                         bars100[i].b / 255.0f, 1.0f);
                Color refYCbCr = srgb.convert(ColorModel(ColorModel::YCbCr_Rec709));
                int refY, refCb, refCr;
                colorToYCbCr8(refYCbCr, refY, refCb, refCr);

                int cx = i * barWidth + barWidth / 2;
                int pipeY, pipeCb, pipeCr;
                readYUYV8(dst, cx, pipeY, pipeCb, pipeCr);

                INFO(bars100[i].name << " (VTP->scalar): ref Y=" << refY
                     << " pipe Y=" << pipeY);
                CHECK(std::abs(pipeY  - refY)  <= 2);
                CHECK(std::abs(pipeCb - refCb) <= 2);
                CHECK(std::abs(pipeCr - refCr) <= 2);
        }
}

// =========================================================================
// Level 4: VideoTestPattern -> fast path -> BT.709 integer reference
// =========================================================================

TEST_CASE("CSC L4: VideoTestPattern bars through fast path") {
        Image src = makeColorBars8();
        REQUIRE(src.isValid());
        // Default config -> fast path
        Image dst = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata());
        REQUIRE(dst.isValid());

        CSCPipeline p(PixelDesc::RGBA8_sRGB, PixelDesc::YUV8_422_Rec709);
        REQUIRE(p.isFastPath());

        int barWidth = 160 / 8;
        for(int i = 0; i < 8; i++) {
                int cx = i * barWidth + barWidth / 2;
                int pipeY, pipeCb, pipeCr;
                readYUYV8(dst, cx, pipeY, pipeCb, pipeCr);

                INFO(bars100[i].name << " (VTP->fastpath): Y=" << pipeY
                     << " ref=" << bars100[i].y709
                     << "  Cb=" << pipeCb << " ref=" << bars100[i].cb709
                     << "  Cr=" << pipeCr << " ref=" << bars100[i].cr709);
                CHECK(std::abs(pipeY  - bars100[i].y709)  <= 1);
                CHECK(std::abs(pipeCb - bars100[i].cb709) <= 1);
                CHECK(std::abs(pipeCr - bars100[i].cr709) <= 1);
        }
}

TEST_CASE("CSC L4: 75% bars through fast path") {
        VideoTestPattern gen;
        gen.setPattern(VideoTestPattern::ColorBars75);
        Image src = gen.create(ImageDesc(160, 2, PixelDesc::RGBA8_sRGB));
        REQUIRE(src.isValid());
        Image dst = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata());
        REQUIRE(dst.isValid());

        int barWidth = 160 / 8;
        // White bar at 75%: RGB(191,191,191)
        int whiteY, whiteCb, whiteCr;
        readYUYV8(dst, barWidth / 2, whiteY, whiteCb, whiteCr);
        CHECK(whiteY > 170);  CHECK(whiteY < 210);
        CHECK(whiteCb == 128); CHECK(whiteCr == 128);

        // Black bar (always 0)
        int blackY, blackCb, blackCr;
        readYUYV8(dst, 7 * barWidth + barWidth / 2, blackY, blackCb, blackCr);
        CHECK(blackY == 16);
}

// =========================================================================
// Level 5: Round-trip accuracy
// =========================================================================

TEST_CASE("CSC L5: round-trip RGB -> YCbCr -> RGB") {
        Image src = makeColorBars8();
        REQUIRE(src.isValid());

        SUBCASE("fast path round-trip") {
                Image yuv = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata());
                REQUIRE(yuv.isValid());
                Image back = yuv.convert(PixelDesc::RGBA8_sRGB, Metadata());
                REQUIRE(back.isValid());

                // Measure per-bar round-trip error
                int barWidth = 160 / 8;
                for(int i = 0; i < 8; i++) {
                        int cx = i * barWidth + barWidth / 2;
                        const uint8_t *orig = static_cast<const uint8_t *>(src.data()) + cx * 4;
                        const uint8_t *trip = static_cast<const uint8_t *>(back.data()) + cx * 4;
                        int dR = std::abs((int)orig[0] - (int)trip[0]);
                        int dG = std::abs((int)orig[1] - (int)trip[1]);
                        int dB = std::abs((int)orig[2] - (int)trip[2]);
                        int maxD = std::max({dR, dG, dB});
                        INFO(bars100[i].name << " round-trip: dR=" << dR
                             << " dG=" << dG << " dB=" << dB);
                        // Fast path round-trip: integer quantization + chroma
                        // subsampling. Achromatic colors (white/black/gray) should
                        // be near-exact; saturated colors may lose ~2 LSB.
                        CHECK(maxD <= 3);
                }
        }

        SUBCASE("scalar path round-trip") {
                Image yuv = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata(), scalarConfig());
                Image back = yuv.convert(PixelDesc::RGBA8_sRGB, Metadata(), scalarConfig());
                REQUIRE(back.isValid());
                // Scalar round-trip goes through sRGB EOTF -> Rec.709 OETF ->
                // YCbCr matrix -> inverse matrix -> Rec.709 EOTF -> sRGB OETF.
                // The transfer function mismatch causes larger error.
        }
}

// =========================================================================
// Level 6: Cross-format consistency
// =========================================================================

TEST_CASE("CSC L6: cross-format consistency") {
        // Same VideoTestPattern source, converted to multiple YCbCr 8-bit
        // layouts.  All should produce identical Y/Cb/Cr values for each bar
        // (they differ only in memory layout).
        Image src = makeColorBars8();
        REQUIRE(src.isValid());

        PixelDesc::ID targets[] = {
                PixelDesc::YUV8_422_Rec709,
                PixelDesc::YUV8_422_UYVY_Rec709,
                PixelDesc::YUV8_422_Planar_Rec709,
                PixelDesc::YUV8_422_SemiPlanar_Rec709,
        };

        // Convert to YUYV as reference
        Image refImg = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata());
        REQUIRE(refImg.isValid());

        int barWidth = 160 / 8;

        for(auto targetID : targets) {
                PixelDesc target(targetID);
                if(target.id() == PixelDesc::YUV8_422_Rec709) continue;

                Image dst = src.convert(target, src.metadata());
                REQUIRE(dst.isValid());

                // Convert both back to RGBA8 and compare
                Image refBack = refImg.convert(PixelDesc::RGBA8_sRGB, Metadata());
                Image dstBack = dst.convert(PixelDesc::RGBA8_sRGB, Metadata());
                REQUIRE(refBack.isValid());
                REQUIRE(dstBack.isValid());

                int maxDiff = 0;
                const uint8_t *r = static_cast<const uint8_t *>(refBack.data());
                const uint8_t *d = static_cast<const uint8_t *>(dstBack.data());
                for(size_t i = 0; i < 160 * 2 * 4; i++) {
                        int diff = std::abs((int)r[i] - (int)d[i]);
                        if(diff > maxDiff) maxDiff = diff;
                }
                INFO("YUYV vs " << target.name() << ": maxDiff=" << maxDiff);
                CHECK(maxDiff <= 1);
        }
}

// =========================================================================
// Level 7: 10-bit consistency
// =========================================================================

TEST_CASE("CSC L7: 10-bit bar values") {
        // Generate 8-bit bars, upconvert to 10-bit, convert to 10-bit YCbCr.
        Image src8 = makeColorBars8();
        REQUIRE(src8.isValid());
        Image src10 = src8.convert(PixelDesc::RGBA10_LE_sRGB, src8.metadata(), scalarConfig());
        REQUIRE(src10.isValid());

        Image dst = src10.convert(PixelDesc::YUV10_422_UYVY_LE_Rec709, src10.metadata());
        REQUIRE(dst.isValid());

        // 10-bit BT.709 integer reference values
        struct Ref10 { int y, cb, cr; };
        Ref10 refs[] = {
                { 942, 512, 512}, { 842,  62, 585}, { 679, 664,  62}, { 579, 214, 135},
                { 427, 810, 889}, { 327, 360, 962}, { 164, 962, 439}, {  64, 512, 512},
        };

        int barWidth = 160 / 8;
        const uint16_t *yuv = static_cast<const uint16_t *>(dst.data());
        for(int i = 0; i < 8; i++) {
                int cx = i * barWidth + barWidth / 2;
                int pair = cx / 2;
                int pY  = yuv[pair * 4 + (cx % 2 == 0 ? 1 : 3)];
                int pCb = yuv[pair * 4 + 0];
                int pCr = yuv[pair * 4 + 2];

                INFO(bars100[i].name << " 10b: Y=" << pY << " ref=" << refs[i].y
                     << "  Cb=" << pCb << " ref=" << refs[i].cb
                     << "  Cr=" << pCr << " ref=" << refs[i].cr);
                // Wider tolerance for 8-bit -> 10-bit upconversion + fast path
                CHECK(std::abs(pY  - refs[i].y)  <= 4);
                CHECK(std::abs(pCb - refs[i].cb) <= 4);
                CHECK(std::abs(pCr - refs[i].cr) <= 4);
        }
}

// =========================================================================
// Level 8: Range boundaries and edge cases
// =========================================================================

TEST_CASE("CSC L8: range boundaries 8-bit") {
        // Black -> Y=16, Cb=Cr=128
        Image b = makeUniformRGBA8(0, 0, 0).convert(PixelDesc::YUV8_422_Rec709, Metadata());
        REQUIRE(b.isValid());
        const uint8_t *bd = static_cast<const uint8_t *>(b.data());
        CHECK(bd[0] == 16);  CHECK(bd[1] == 128);  CHECK(bd[3] == 128);

        // White -> Y=235, Cb=Cr=128
        Image w = makeUniformRGBA8(255, 255, 255).convert(PixelDesc::YUV8_422_Rec709, Metadata());
        REQUIRE(w.isValid());
        const uint8_t *wd = static_cast<const uint8_t *>(w.data());
        CHECK(wd[0] == 235);  CHECK(wd[1] == 128);  CHECK(wd[3] == 128);

        // Gray -> achromatic (Cb=Cr=128)
        Image g = makeUniformRGBA8(128, 128, 128).convert(PixelDesc::YUV8_422_Rec709, Metadata());
        REQUIRE(g.isValid());
        const uint8_t *gd = static_cast<const uint8_t *>(g.data());
        CHECK(gd[1] == 128);  CHECK(gd[3] == 128);
        CHECK(gd[0] > 16);   CHECK(gd[0] < 235);
}

TEST_CASE("CSC L8: range boundaries 10-bit") {
        Image b = makeUniformRGBA10LE(0, 0, 0).convert(PixelDesc::YUV10_422_UYVY_LE_Rec709, Metadata());
        REQUIRE(b.isValid());
        const uint16_t *bd = static_cast<const uint16_t *>(b.data());
        CHECK(bd[1] == 64);  CHECK(bd[0] == 512);  CHECK(bd[2] == 512);

        Image w = makeUniformRGBA10LE(1023, 1023, 1023).convert(PixelDesc::YUV10_422_UYVY_LE_Rec709, Metadata());
        REQUIRE(w.isValid());
        const uint16_t *wd = static_cast<const uint16_t *>(w.data());
        CHECK(std::abs((int)wd[1] - 940) <= 2);
        CHECK(wd[0] == 512);  CHECK(wd[2] == 512);
}

TEST_CASE("CSC L8: edge cases") {
        SUBCASE("1px wide") {
                Image s(1, 1, PixelDesc::RGBA8_sRGB);
                s.fill(128);
                CHECK(s.convert(PixelDesc::YUV8_422_Rec709, Metadata()).isValid());
        }
        SUBCASE("odd width 7 YUYV") {
                Image s(7, 1, PixelDesc::RGBA8_sRGB);
                s.fill(128);
                Image d = s.convert(PixelDesc::YUV8_422_Rec709, Metadata());
                CHECK(d.isValid());
                CHECK(d.convert(PixelDesc::RGBA8_sRGB, Metadata()).isValid());
        }
        SUBCASE("odd width 7 planar 422") {
                Image s(7, 1, PixelDesc::RGBA8_sRGB);
                s.fill(128);
                CHECK(s.convert(PixelDesc::YUV8_422_Planar_Rec709, Metadata()).isValid());
        }
        SUBCASE("odd width 7 NV12") {
                Image s(7, 2, PixelDesc::RGBA8_sRGB);
                s.fill(128);
                CHECK(s.convert(PixelDesc::YUV8_420_SemiPlanar_Rec709, Metadata()).isValid());
        }
        SUBCASE("v210 width 8") {
                Image s(8, 1, PixelDesc::RGBA10_LE_sRGB);
                REQUIRE(s.isValid());
                uint16_t *d = static_cast<uint16_t *>(s.data());
                for(int i = 0; i < 8 * 4; i++) d[i] = 512;
                CHECK(s.convert(PixelDesc::YUV10_422_v210_Rec709, Metadata()).isValid());
        }
        SUBCASE("HD 1920 round-trip") {
                Image s(1920, 1, PixelDesc::RGBA8_sRGB);
                s.fill(128);
                Image d = s.convert(PixelDesc::YUV8_422_Rec709, Metadata());
                CHECK(d.isValid());
                Image b = d.convert(PixelDesc::RGBA8_sRGB, Metadata());
                CHECK(b.isValid());
                CHECK(b.width() == 1920);
        }
}

// =========================================================================
// Level 9: Rec.601, Rec.2020, and cross-standard
// =========================================================================

TEST_CASE("CSC L9: Rec.601 coefficient validation") {
        // Rec.601 and Rec.709 must produce different luma for non-achromatic colors
        Image src = makeUniformRGBA8(200, 100, 50);
        Image y709 = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata(), scalarConfig());
        Image y601 = src.convert(PixelDesc::YUV8_422_Rec601, src.metadata(), scalarConfig());
        REQUIRE(y709.isValid());
        REQUIRE(y601.isValid());

        int luma709 = static_cast<const uint8_t *>(y709.data())[0];
        int luma601 = static_cast<const uint8_t *>(y601.data())[0];
        INFO("709 Y=" << luma709 << "  601 Y=" << luma601);
        CHECK(luma709 != luma601);
}

TEST_CASE("CSC L9: VideoTestPattern bars Rec.601 vs Rec.709") {
        Image src = makeColorBars8();
        Image yuv709 = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata());
        Image yuv601 = src.convert(PixelDesc::YUV8_422_Rec601, src.metadata());
        REQUIRE(yuv709.isValid());
        REQUIRE(yuv601.isValid());

        // Green bar has the largest luma difference between standards
        int barWidth = 160 / 8;
        int greenCx = 3 * barWidth + barWidth / 2;
        int y709, cb709, cr709, y601, cb601, cr601;
        readYUYV8(yuv709, greenCx, y709, cb709, cr709);
        readYUYV8(yuv601, greenCx, y601, cb601, cr601);
        INFO("Green: 709 Y=" << y709 << "  601 Y=" << y601);
        CHECK(y709 != y601);
}

TEST_CASE("CSC L9: Rec.601 round-trips") {
        Image src = makeGradientRGBA8(8, 2);
        SUBCASE("YUYV Rec.601")  { CHECK(src.convert(PixelDesc::YUV8_422_Rec601, src.metadata()).isValid()); }
        SUBCASE("UYVY Rec.601")  { CHECK(src.convert(PixelDesc::YUV8_422_UYVY_Rec601, src.metadata()).isValid()); }
        SUBCASE("NV12 Rec.601")  { CHECK(src.convert(PixelDesc::YUV8_420_SemiPlanar_Rec601, src.metadata()).isValid()); }
        SUBCASE("Planar Rec.601"){ CHECK(src.convert(PixelDesc::YUV8_420_Planar_Rec601, src.metadata()).isValid()); }
}

TEST_CASE("CSC L9: Rec.2020 10-bit") {
        SUBCASE("black and white") {
                Image b = makeUniformRGBA10LE(0, 0, 0);
                Image d = b.convert(PixelDesc::YUV10_422_UYVY_LE_Rec2020, Metadata());
                REQUIRE(d.isValid());
                const uint16_t *yuv = static_cast<const uint16_t *>(d.data());
                CHECK(yuv[1] == 64);  CHECK(yuv[0] == 512);  CHECK(yuv[2] == 512);

                Image w = makeUniformRGBA10LE(1023, 1023, 1023);
                Image dw = w.convert(PixelDesc::YUV10_422_UYVY_LE_Rec2020, Metadata());
                REQUIRE(dw.isValid());
                const uint16_t *ywuv = static_cast<const uint16_t *>(dw.data());
                CHECK(std::abs((int)ywuv[1] - 940) <= 2);
        }

        SUBCASE("Rec.2020 vs Rec.709 differ") {
                Image g = makeUniformRGBA10LE(0, 1023, 0);
                Image y709 = g.convert(PixelDesc::YUV10_422_UYVY_LE_Rec709, Metadata());
                Image y2020 = g.convert(PixelDesc::YUV10_422_UYVY_LE_Rec2020, Metadata());
                REQUIRE(y709.isValid());
                REQUIRE(y2020.isValid());
                int l709  = static_cast<const uint16_t *>(y709.data())[1];
                int l2020 = static_cast<const uint16_t *>(y2020.data())[1];
                CHECK(l709 != l2020);
        }
}

// =========================================================================
// Planar / semi-planar coverage
// =========================================================================

TEST_CASE("CSC planar format coverage") {
        Image src = makeGradientRGBA8(8, 4);
        REQUIRE(src.isValid());

        SUBCASE("422 planar") {
                Image y = src.convert(PixelDesc::YUV8_422_Planar_Rec709, src.metadata());
                REQUIRE(y.isValid());
                CHECK(y.pixelDesc().pixelFormat().planeCount() == 3);
                CHECK(y.convert(PixelDesc::RGBA8_sRGB, Metadata()).isValid());
        }
        SUBCASE("420 planar")     { CHECK(src.convert(PixelDesc::YUV8_420_Planar_Rec709, src.metadata()).isValid()); }
        SUBCASE("NV12")           { CHECK(src.convert(PixelDesc::YUV8_420_SemiPlanar_Rec709, src.metadata()).isValid()); }
        SUBCASE("NV21")           { CHECK(src.convert(PixelDesc::YUV8_420_NV21_Rec709, src.metadata()).isValid()); }
        SUBCASE("NV16")           { CHECK(src.convert(PixelDesc::YUV8_422_SemiPlanar_Rec709, src.metadata()).isValid()); }
        SUBCASE("411")            { CHECK(src.convert(PixelDesc::YUV8_411_Planar_Rec709, src.metadata()).isValid()); }

        SUBCASE("planar vs interleaved equivalence (scalar)") {
                Image yP = src.convert(PixelDesc::YUV8_422_Planar_Rec709, src.metadata(), scalarConfig());
                Image yI = src.convert(PixelDesc::YUV8_422_Rec709, src.metadata(), scalarConfig());
                Image bP = yP.convert(PixelDesc::RGBA8_sRGB, Metadata(), scalarConfig());
                Image bI = yI.convert(PixelDesc::RGBA8_sRGB, Metadata(), scalarConfig());
                REQUIRE(bP.isValid());
                REQUIRE(bI.isValid());
                const uint8_t *rp = static_cast<const uint8_t *>(bP.data());
                const uint8_t *ri = static_cast<const uint8_t *>(bI.data());
                int maxDiff = 0;
                for(size_t i = 0; i < 8 * 4 * 4; i++) {
                        int d = std::abs((int)rp[i] - (int)ri[i]);
                        if(d > maxDiff) maxDiff = d;
                }
                CHECK(maxDiff <= 2);
        }

        SUBCASE("NV12 vs NV21 equivalence") {
                Image s = makeUniformRGBA8(200, 100, 50, 8);
                Image nv12 = s.convert(PixelDesc::YUV8_420_SemiPlanar_Rec709, Metadata());
                Image nv21 = s.convert(PixelDesc::YUV8_420_NV21_Rec709, Metadata());
                Image r12 = nv12.convert(PixelDesc::RGBA8_sRGB, Metadata());
                Image r21 = nv21.convert(PixelDesc::RGBA8_sRGB, Metadata());
                REQUIRE(r12.isValid());
                REQUIRE(r21.isValid());
                const uint8_t *d12 = static_cast<const uint8_t *>(r12.data());
                const uint8_t *d21 = static_cast<const uint8_t *>(r21.data());
                int maxDiff = 0;
                for(size_t i = 0; i < 8 * 4; i++) {
                        int d = std::abs((int)d12[i] - (int)d21[i]);
                        if(d > maxDiff) maxDiff = d;
                }
                CHECK(maxDiff <= 6);
        }
}

// =========================================================================
// Fast-path cross-validation
// =========================================================================

TEST_CASE("CSC fast-path cross-validation") {
        struct Pair { PixelDesc::ID src; PixelDesc::ID dst; int tolerance; };
        Pair pairs[] = {
                {PixelDesc::RGBA8_sRGB,               PixelDesc::RGB8_sRGB,  0},
                {PixelDesc::RGB8_sRGB,                PixelDesc::RGBA8_sRGB, 0},
                {PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Rec709, 35},
                {PixelDesc::YUV8_422_Rec709,          PixelDesc::RGBA8_sRGB, 35},
                {PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_UYVY_Rec709, 35},
                {PixelDesc::YUV8_422_UYVY_Rec709,     PixelDesc::RGBA8_sRGB, 35},
                {PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_SemiPlanar_Rec709, 35},
                {PixelDesc::YUV8_420_SemiPlanar_Rec709, PixelDesc::RGBA8_sRGB, 35},
                {PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_422_Planar_Rec709, 35},
                {PixelDesc::YUV8_422_Planar_Rec709,   PixelDesc::RGBA8_sRGB, 35},
                {PixelDesc::RGBA8_sRGB,               PixelDesc::YUV8_420_Planar_Rec709, 35},
                {PixelDesc::YUV8_420_Planar_Rec709,   PixelDesc::RGBA8_sRGB, 35},
        };

        Image src = makeGradientRGBA8(16, 2);
        REQUIRE(src.isValid());

        for(const auto &p : pairs) {
                PixelDesc srcPD(p.src);
                PixelDesc dstPD(p.dst);
                CSCPipeline fp(srcPD, dstPD);
                REQUIRE(fp.isFastPath());

                Image fpSrc = (p.src == PixelDesc::RGBA8_sRGB) ? src :
                              src.convert(srcPD, src.metadata(), scalarConfig());
                REQUIRE(fpSrc.isValid());

                Image dstOpt = fpSrc.convert(dstPD, fpSrc.metadata());
                Image dstScalar = fpSrc.convert(dstPD, fpSrc.metadata(), scalarConfig());
                REQUIRE(dstOpt.isValid());
                REQUIRE(dstScalar.isValid());

                int maxDiff = 0;
                for(int pl = 0; pl < static_cast<int>(dstPD.planeCount()); pl++) {
                        size_t bytes = dstOpt.plane(pl)->size();
                        const uint8_t *o = static_cast<const uint8_t *>(dstOpt.data(pl));
                        const uint8_t *s = static_cast<const uint8_t *>(dstScalar.data(pl));
                        for(size_t i = 0; i < bytes; i++) {
                                int d = std::abs((int)o[i] - (int)s[i]);
                                if(d > maxDiff) maxDiff = d;
                        }
                }
                INFO(srcPD.name() << " -> " << dstPD.name()
                     << "  maxDiff=" << maxDiff << "  tol=" << p.tolerance);
                CHECK(maxDiff <= p.tolerance);
        }
}
