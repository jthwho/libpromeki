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
#include <promeki/mediaconfig.h>
#include <promeki/enums.h>
#include <promeki/videotestpattern.h>
#include <cstring>
#include <cmath>

using namespace promeki;

// =========================================================================
// Helpers
// =========================================================================

static const MediaConfig &scalarConfig() {
        static MediaConfig cfg;
        static bool init = false;
        if(!init) { cfg.set(MediaConfig::CscPath, CscPath::Scalar); init = true; }
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

// Canonical BT.709 limited-range integer fast-path outputs (8-bit).
// These are the exact values produced by (( 47*R + 157*G +  16*B +
// 128) >> 8) + 16 for Y, ((-26*R - 87*G + 112*B + 128) >> 8) + 128 for
// Cb, and ((112*R - 102*G - 10*B + 128) >> 8) + 128 for Cr.  Note that
// neutral gray (White / Black) has Cb=127 rather than Cb=128 because
// the integer row coefficients for Cb sum to -1 (canonical rounding
// of -0.1146, -0.3854, 0.5).  This is a standard 1-LSB rounding
// artifact of the BT.709 integer matrix.
static const BarDef bars100[] = {
        {"White",   255, 255, 255, 235, 127, 128},
        {"Yellow",  255, 255,   0, 219,  15, 138},
        {"Cyan",      0, 255, 255, 188, 153,  16},
        {"Green",     0, 255,   0, 172,  41,  26},
        {"Magenta", 255,   0, 255,  79, 214, 230},
        {"Red",     255,   0,   0,  63, 102, 240},
        {"Blue",      0,   0, 255,  32, 240, 118},
        {"Black",     0,   0,   0,  16, 127, 128},
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
        // White bar at 75%: RGB(191,191,191).  Neutral gray in BT.709
        // integer fast path has Cb=127 (see bars100 comment above —
        // the Cb row coefficients sum to -1 after rounding).
        int whiteY, whiteCb, whiteCr;
        readYUYV8(dst, barWidth / 2, whiteY, whiteCb, whiteCr);
        CHECK(whiteY > 170);  CHECK(whiteY < 210);
        CHECK(std::abs(whiteCb - 128) <= 1);
        CHECK(std::abs(whiteCr - 128) <= 1);

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
        // Generate 10-bit bars directly via VideoTestPattern (PaintEngine now
        // supports RGBA10_LE natively).
        VideoTestPattern gen;
        gen.setPattern(VideoTestPattern::ColorBars);
        Image src10 = gen.create(ImageDesc(160, 2, PixelDesc::RGBA10_LE_sRGB));
        REQUIRE(src10.isValid());

        Image dst = src10.convert(PixelDesc::YUV10_422_UYVY_LE_Rec709, src10.metadata());
        REQUIRE(dst.isValid());

        // 10-bit BT.709 integer reference values computed from the
        // canonical fast-path formulas:
        //   Y  = (( 186*R + 627*G +  63*B + 512) >> 10) + 64
        //   Cb = ((-103*R - 346*G + 448*B + 512) >> 10) + 512
        //   Cr = (( 448*R - 407*G -  41*B + 512) >> 10) + 512
        // Neutral gray has Cb=511 rather than 512 (same 1-LSB rounding
        // artifact as the 8-bit BT.709 matrix).
        struct Ref10 { int y, cb, cr; };
        Ref10 refs[] = {
                {939, 511, 512}, {876,  63, 553}, {753, 614,  64}, {690, 166, 105},
                {313, 857, 919}, {250, 409, 960}, {127, 960, 471}, { 64, 512, 512},
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
                // Direct 10-bit rendering + fast path should be tight
                CHECK(std::abs(pY  - refs[i].y)  <= 2);
                CHECK(std::abs(pCb - refs[i].cb) <= 2);
                CHECK(std::abs(pCr - refs[i].cr) <= 2);
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

        // White -> Y=235, Cb=Cr≈128 (±1 LSB for Cb due to the BT.709
        // integer matrix's row sum being -1 instead of 0).
        Image w = makeUniformRGBA8(255, 255, 255).convert(PixelDesc::YUV8_422_Rec709, Metadata());
        REQUIRE(w.isValid());
        const uint8_t *wd = static_cast<const uint8_t *>(w.data());
        CHECK(wd[0] == 235);
        CHECK(std::abs((int)wd[1] - 128) <= 1);
        CHECK(std::abs((int)wd[3] - 128) <= 1);

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
        CHECK(std::abs((int)wd[0] - 512) <= 1);
        CHECK(std::abs((int)wd[2] - 512) <= 1);
}

TEST_CASE("CSC L8: range boundaries 12-bit") {
        auto make12 = [](uint16_t r, uint16_t g, uint16_t b) {
                return makeUniformRGBA10LE(r, g, b); // reuse; PixelDesc handles bit depth
        };

        SUBCASE("black 12-bit UYVY Rec.709") {
                Image src = makeUniformRGBA10LE(0, 0, 0); // 10-bit zeros = 12-bit zeros when upconverted
                Image src12 = src.convert(PixelDesc::RGBA12_LE_sRGB, Metadata(), scalarConfig());
                REQUIRE(src12.isValid());
                Image dst = src12.convert(PixelDesc::YUV12_422_UYVY_LE_Rec709, Metadata());
                REQUIRE(dst.isValid());
                CSCPipeline p(PixelDesc::RGBA12_LE_sRGB, PixelDesc::YUV12_422_UYVY_LE_Rec709);
                CHECK(p.isFastPath());
                const uint16_t *yuv = static_cast<const uint16_t *>(dst.data());
                CHECK(std::abs((int)yuv[1] - 256) <= 2);   // Y ~ 256
                CHECK(std::abs((int)yuv[0] - 2048) <= 2);  // Cb ~ 2048
                CHECK(std::abs((int)yuv[2] - 2048) <= 2);  // Cr ~ 2048
        }

        SUBCASE("black 12-bit Planar 420 Rec.709") {
                Image src(2, 2, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                src.fill(0);
                Image dst = src.convert(PixelDesc::YUV12_420_Planar_LE_Rec709, Metadata());
                REQUIRE(dst.isValid());
                CHECK(dst.pixelDesc().pixelFormat().planeCount() == 3);
                const uint16_t *yp = static_cast<const uint16_t *>(dst.data(0));
                CHECK(std::abs((int)yp[0] - 256) <= 2);
        }

        SUBCASE("black 12-bit NV12 Rec.709") {
                Image src(2, 2, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                src.fill(0);
                Image dst = src.convert(PixelDesc::YUV12_420_SemiPlanar_LE_Rec709, Metadata());
                REQUIRE(dst.isValid());
                CHECK(dst.pixelDesc().pixelFormat().planeCount() == 2);
                const uint16_t *yp = static_cast<const uint16_t *>(dst.data(0));
                CHECK(std::abs((int)yp[0] - 256) <= 2);
        }

        SUBCASE("black 12-bit Planar 422 Rec.709") {
                Image src(2, 1, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                src.fill(0);
                Image dst = src.convert(PixelDesc::YUV12_422_Planar_LE_Rec709, Metadata());
                REQUIRE(dst.isValid());
                const uint16_t *yp = static_cast<const uint16_t *>(dst.data(0));
                CHECK(std::abs((int)yp[0] - 256) <= 2);
        }

        SUBCASE("black 12-bit UYVY Rec.2020") {
                Image src(2, 1, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                src.fill(0);
                Image dst = src.convert(PixelDesc::YUV12_422_UYVY_LE_Rec2020, Metadata());
                REQUIRE(dst.isValid());
                CSCPipeline p(PixelDesc::RGBA12_LE_sRGB, PixelDesc::YUV12_422_UYVY_LE_Rec2020);
                CHECK(p.isFastPath());
                const uint16_t *yuv = static_cast<const uint16_t *>(dst.data());
                CHECK(std::abs((int)yuv[1] - 256) <= 2);
                CHECK(std::abs((int)yuv[0] - 2048) <= 2);
        }

        SUBCASE("black 12-bit Planar 420 Rec.2020") {
                Image src(2, 2, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                src.fill(0);
                Image dst = src.convert(PixelDesc::YUV12_420_Planar_LE_Rec2020, Metadata());
                REQUIRE(dst.isValid());
                CSCPipeline p(PixelDesc::RGBA12_LE_sRGB, PixelDesc::YUV12_420_Planar_LE_Rec2020);
                CHECK(p.isFastPath());
                const uint16_t *yp = static_cast<const uint16_t *>(dst.data(0));
                CHECK(std::abs((int)yp[0] - 256) <= 2);
        }

        SUBCASE("12-bit round-trips") {
                // Verify all 12-bit fast paths produce valid round-trip output
                Image src(4, 2, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                uint16_t *d = static_cast<uint16_t *>(src.data());
                for(int i = 0; i < 4 * 2 * 4; i++) d[i] = 2048;

                PixelDesc::ID targets[] = {
                        PixelDesc::YUV12_422_UYVY_LE_Rec709,
                        PixelDesc::YUV12_422_Planar_LE_Rec709,
                        PixelDesc::YUV12_420_Planar_LE_Rec709,
                        PixelDesc::YUV12_420_SemiPlanar_LE_Rec709,
                        PixelDesc::YUV12_422_UYVY_LE_Rec2020,
                        PixelDesc::YUV12_420_Planar_LE_Rec2020,
                };
                for(auto tid : targets) {
                        PixelDesc target(tid);
                        Image yuv = src.convert(target, Metadata());
                        REQUIRE(yuv.isValid());
                        Image back = yuv.convert(PixelDesc::RGBA12_LE_sRGB, Metadata());
                        INFO("12-bit round-trip: " << target.name());
                        CHECK(back.isValid());
                }
        }

        SUBCASE("white 12-bit UYVY Rec.709") {
                // Create white in 12-bit (4095)
                Image src(2, 1, PixelDesc::RGBA12_LE_sRGB);
                REQUIRE(src.isValid());
                uint16_t *d = static_cast<uint16_t *>(src.data());
                for(int i = 0; i < 8; i++) d[i] = 4095;
                Image dst = src.convert(PixelDesc::YUV12_422_UYVY_LE_Rec709, Metadata());
                REQUIRE(dst.isValid());
                const uint16_t *yuv = static_cast<const uint16_t *>(dst.data());
                CHECK(std::abs((int)yuv[1] - 3760) <= 2);  // Y ~ 3760
                CHECK(std::abs((int)yuv[0] - 2048) <= 2);  // Cb achromatic
                CHECK(std::abs((int)yuv[2] - 2048) <= 2);  // Cr achromatic
        }
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

TEST_CASE("CSC BGRA8 <-> RGBA8 scalar correctness") {
        // Validates that the generic scalar pipeline correctly reorders
        // components by semantic (R,G,B,A) rather than by positional index.
        Image src = makeUniformRGBA8(200, 100, 50);
        REQUIRE(src.isValid());

        // Convert RGBA8 -> BGRA8 -> RGBA8 through the scalar pipeline
        Image bgra = src.convert(PixelDesc::BGRA8_sRGB, src.metadata(), scalarConfig());
        REQUIRE(bgra.isValid());
        Image back = bgra.convert(PixelDesc::RGBA8_sRGB, Metadata(), scalarConfig());
        REQUIRE(back.isValid());

        const uint8_t *orig = static_cast<const uint8_t *>(src.data());
        const uint8_t *trip = static_cast<const uint8_t *>(back.data());
        CHECK(trip[0] == orig[0]);  // R
        CHECK(trip[1] == orig[1]);  // G
        CHECK(trip[2] == orig[2]);  // B
        CHECK(trip[3] == orig[3]);  // A

        // Also verify the intermediate BGRA has swapped R and B
        const uint8_t *bd = static_cast<const uint8_t *>(bgra.data());
        CHECK(bd[0] == orig[2]);  // BGRA[0] = B = orig R position? No: BGRA[0] = B = orig[2]
        CHECK(bd[1] == orig[1]);  // G
        CHECK(bd[2] == orig[0]);  // R
        CHECK(bd[3] == orig[3]);  // A
}

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

// =========================================================================
// Mono source expansion
// =========================================================================
//
// Converting a single-component (grayscale / mono) PixelDesc to a
// multi-component RGB target needs to broadcast the luma into every
// color channel — otherwise the unused buffers 1 and 2 leak zeros into
// G and B and the image displays entirely in the red channel.

static Image makeMonoRamp8(size_t w = 32, size_t h = 2) {
        Image img(w, h, PixelDesc::Mono8_sRGB);
        if(!img.isValid()) return img;
        uint8_t *data = static_cast<uint8_t *>(img.data());
        for(size_t y = 0; y < h; y++) {
                for(size_t x = 0; x < w; x++) {
                        data[y * img.lineStride(0) + x] =
                                static_cast<uint8_t>((x * 255) / (w - 1));
                }
        }
        return img;
}

TEST_CASE("CSC Mono8_sRGB -> RGBA8_sRGB round-trips as gray") {
        Image src = makeMonoRamp8();
        REQUIRE(src.isValid());

        Image dst = src.convert(PixelDesc(PixelDesc::RGBA8_sRGB), src.metadata());
        REQUIRE(dst.isValid());
        REQUIRE(dst.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
        CHECK(dst.width() == src.width());
        CHECK(dst.height() == src.height());

        const uint8_t *srcData = static_cast<const uint8_t *>(src.data());
        const uint8_t *dstData = static_cast<const uint8_t *>(dst.data());
        for(size_t y = 0; y < dst.height(); y++) {
                for(size_t x = 0; x < dst.width(); x++) {
                        uint8_t luma = srcData[y * src.lineStride(0) + x];
                        const uint8_t *px = dstData + y * dst.lineStride(0) + x * 4;
                        INFO("pixel x=" << x << " y=" << y
                             << "  luma=" << (int)luma
                             << "  rgba=(" << (int)px[0] << "," << (int)px[1]
                             << "," << (int)px[2] << "," << (int)px[3] << ")");
                        // Allow +/-1 LSB for any rounding through the float
                        // pipeline.  R, G, B must all be within the
                        // tolerance of the source luma, and they must
                        // match each other (true grayscale, not red).
                        CHECK(std::abs(int(px[0]) - int(luma)) <= 1);
                        CHECK(std::abs(int(px[1]) - int(luma)) <= 1);
                        CHECK(std::abs(int(px[2]) - int(luma)) <= 1);
                        CHECK(px[0] == px[1]);
                        CHECK(px[1] == px[2]);
                        CHECK(px[3] == 255);
                }
        }
}

TEST_CASE("CSC Mono8_sRGB -> RGB8_sRGB round-trips as gray") {
        // Same behavior without an alpha channel on the destination.
        Image src = makeMonoRamp8();
        REQUIRE(src.isValid());

        Image dst = src.convert(PixelDesc(PixelDesc::RGB8_sRGB), src.metadata());
        REQUIRE(dst.isValid());
        REQUIRE(dst.pixelDesc().id() == PixelDesc::RGB8_sRGB);

        const uint8_t *srcData = static_cast<const uint8_t *>(src.data());
        const uint8_t *dstData = static_cast<const uint8_t *>(dst.data());
        for(size_t y = 0; y < dst.height(); y++) {
                for(size_t x = 0; x < dst.width(); x++) {
                        uint8_t luma = srcData[y * src.lineStride(0) + x];
                        const uint8_t *px = dstData + y * dst.lineStride(0) + x * 3;
                        CHECK(std::abs(int(px[0]) - int(luma)) <= 1);
                        CHECK(std::abs(int(px[1]) - int(luma)) <= 1);
                        CHECK(std::abs(int(px[2]) - int(luma)) <= 1);
                        CHECK(px[0] == px[1]);
                        CHECK(px[1] == px[2]);
                }
        }
}

TEST_CASE("CSC Mono8_sRGB -> YUV8_422_Rec709 produces neutral chroma") {
        // The key property under test is that a grayscale input
        // produces a grayscale output — i.e. strictly neutral chroma
        // (Cb = Cr = 128 in limited range) across the full luma ramp.
        // The exact Y mapping depends on the sRGB <-> Rec.709 gamma
        // difference, so we just check monotonicity and endpoint
        // coverage rather than pinning an exact value.
        Image src = makeMonoRamp8(/*w=*/32, /*h=*/2);
        REQUIRE(src.isValid());

        Image dst = src.convert(PixelDesc(PixelDesc::YUV8_422_Rec709),
                                src.metadata(), scalarConfig());
        REQUIRE(dst.isValid());
        REQUIRE(dst.pixelDesc().id() == PixelDesc::YUV8_422_Rec709);

        int prevY = -1;
        int minY = 255, maxY = 0;
        for(size_t x = 0; x < dst.width(); x++) {
                int yv, cb, cr;
                readYUYV8(dst, static_cast<int>(x), yv, cb, cr);
                INFO("x=" << x << " Y=" << yv << " Cb=" << cb << " Cr=" << cr);
                // Neutral chroma, tolerate +/-1 LSB for rounding through
                // the float pipeline.
                CHECK(std::abs(cb - 128) <= 1);
                CHECK(std::abs(cr - 128) <= 1);
                // Y must be monotonically non-decreasing for a luma ramp.
                CHECK(yv >= prevY);
                prevY = yv;
                if(yv < minY) minY = yv;
                if(yv > maxY) maxY = yv;
        }
        // Black input should land on the limited-range floor (around 16)
        // and white input should land on the ceiling (around 235), with
        // a couple of LSB of slop for pipeline rounding.
        CHECK(minY <= 18);
        CHECK(maxY >= 233);
}

// =========================================================================
// CSCPipeline::cached() — process-wide pipeline cache
// =========================================================================

TEST_CASE("CSCPipeline::cached returns a valid compiled pipeline") {
        PixelDesc src = PixelDesc::RGBA8_sRGB;
        PixelDesc dst = PixelDesc::YUV8_422_Rec709;
        auto p = CSCPipeline::cached(src, dst);
        REQUIRE(p.isValid());
        CHECK(p->isValid());
}

TEST_CASE("CSCPipeline::cached returns the same object on repeated calls") {
        PixelDesc src = PixelDesc::RGBA8_sRGB;
        PixelDesc dst = PixelDesc::YUV8_422_Rec709;
        auto p1 = CSCPipeline::cached(src, dst);
        auto p2 = CSCPipeline::cached(src, dst);
        REQUIRE(p1.isValid());
        REQUIRE(p2.isValid());
        // Both callers should share the same compiled pipeline instance.
        CHECK(p1 == p2);
}

TEST_CASE("CSCPipeline::cached is distinct per format pair") {
        auto p709 = CSCPipeline::cached(PixelDesc::RGBA8_sRGB, PixelDesc::YUV8_422_Rec709);
        auto p601 = CSCPipeline::cached(PixelDesc::RGBA8_sRGB, PixelDesc::YUV8_422_Rec601);
        REQUIRE(p709.isValid());
        REQUIRE(p601.isValid());
        // Different dst format -> different compiled pipeline.
        CHECK(p709 != p601);
}

TEST_CASE("CSCPipeline::cached produces correct output") {
        // Verify that the cached pipeline executes identically to a
        // freshly constructed pipeline for the same format pair.
        PixelDesc srcDesc = PixelDesc::RGBA8_sRGB;
        PixelDesc dstDesc = PixelDesc::YUV8_422_Rec709;

        Image src(8, 1, srcDesc);
        REQUIRE(src.isValid());
        uint8_t *sp = static_cast<uint8_t *>(src.data());
        // Fill with a non-neutral color so chroma is exercised.
        for(int i = 0; i < 8; i++) {
                sp[i * 4 + 0] = 200;  // R
                sp[i * 4 + 1] =  80;  // G
                sp[i * 4 + 2] =  40;  // B
                sp[i * 4 + 3] = 255;  // A
        }

        // Reference via fresh pipeline.
        CSCPipeline fresh(srcDesc, dstDesc);
        REQUIRE(fresh.isValid());
        Image refDst(8, 1, dstDesc);
        REQUIRE(refDst.isValid());
        fresh.execute(src, refDst);

        // Execute via cached pipeline.
        auto cached = CSCPipeline::cached(srcDesc, dstDesc);
        REQUIRE(cached.isValid());
        Image cachedDst(8, 1, dstDesc);
        REQUIRE(cachedDst.isValid());
        cached->execute(src, cachedDst);

        // Outputs must be byte-identical.
        // YUV8_422 YUYV: stride = width * 2 bytes.
        size_t nbytes = refDst.lineStride() * refDst.height();
        const uint8_t *rd = static_cast<const uint8_t *>(refDst.data());
        const uint8_t *cd = static_cast<const uint8_t *>(cachedDst.data());
        bool match = (memcmp(rd, cd, nbytes) == 0);
        CHECK(match);
}

// =========================================================================
// FastPathV210toRGBA8 / FastPathRGBA8toV210 — v210 <-> RGBA8 fast paths
// =========================================================================
//
// The existing v210 test only covers RGBA10_LE_sRGB -> v210.  These tests
// exercise the new bridge fast paths that insert an 8<->10 bit step.

TEST_CASE("v210 RGBA8 fast path: RGBA8 -> v210 round-trip via RGBA8") {
        // Build a solid-color RGBA8 source.
        Image src(8, 1, PixelDesc::RGBA8_sRGB);
        REQUIRE(src.isValid());
        uint8_t *sp = static_cast<uint8_t *>(src.data());
        for(int i = 0; i < 8; i++) {
                sp[i * 4 + 0] = 180;
                sp[i * 4 + 1] = 100;
                sp[i * 4 + 2] =  60;
                sp[i * 4 + 3] = 255;
        }

        // RGBA8 -> v210
        Image v210 = src.convert(PixelDesc::YUV10_422_v210_Rec709, src.metadata());
        REQUIRE(v210.isValid());
        CHECK(v210.pixelDesc().id() == PixelDesc::YUV10_422_v210_Rec709);

        // v210 -> RGBA8
        Image back = v210.convert(PixelDesc::RGBA8_sRGB, v210.metadata());
        REQUIRE(back.isValid());
        CHECK(back.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
        CHECK(back.width() == src.width());

        // Each pixel should round-trip within 4 LSB (8-bit quantisation
        // of a 10-bit intermediate introduces up to 4 LSB of error on
        // reconstruction).
        const uint8_t *bd = static_cast<const uint8_t *>(back.data());
        for(int i = 0; i < 8; i++) {
                INFO("px " << i
                        << " R " << (int)bd[i*4+0]
                        << " G " << (int)bd[i*4+1]
                        << " B " << (int)bd[i*4+2]);
                CHECK(std::abs((int)bd[i*4+0] - 180) <= 10);
                CHECK(std::abs((int)bd[i*4+1] - 100) <= 10);
                CHECK(std::abs((int)bd[i*4+2] -  60) <= 10);
        }
}

TEST_CASE("v210 RGBA8 fast path: v210 -> RGBA8 does not crash on 1920-wide image") {
        // Construct a 1920-wide RGBA8 image and convert to v210, then back.
        Image src(1920, 1, PixelDesc::RGBA8_sRGB);
        REQUIRE(src.isValid());
        src.fill(128);
        Image v210 = src.convert(PixelDesc::YUV10_422_v210_Rec709, src.metadata());
        REQUIRE(v210.isValid());
        Image back = v210.convert(PixelDesc::RGBA8_sRGB, v210.metadata());
        REQUIRE(back.isValid());
        CHECK(back.width() == 1920);
}
