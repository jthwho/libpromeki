/**
 * @file      tests/videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videotestpattern.h>
#include <promeki/enums.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>

using namespace promeki;

static ImageDesc testDesc(int w = 320, int h = 240, PixelFormat::ID fmt = PixelFormat::RGB8_sRGB) {
        return ImageDesc(w, h, fmt);
}

static size_t stride0(const UncompressedVideoPayload &p) {
        return p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
}

// ============================================================================
// Construction and defaults
// ============================================================================

TEST_CASE("VideoTestPattern_Defaults") {
        VideoTestPattern gen;
        CHECK(gen.pattern() == VideoPattern::ColorBars);
        CHECK_FALSE(gen.solidColor().isValid());
}

// ============================================================================
// create() produces valid payloads for all patterns
// ============================================================================

TEST_CASE("VideoTestPattern_CreateAllPatterns") {
        VideoTestPattern gen;
        ImageDesc desc = testDesc();

        VideoPattern patterns[] = {
                VideoPattern::ColorBars,
                VideoPattern::ColorBars75,
                VideoPattern::Ramp,
                VideoPattern::Grid,
                VideoPattern::Crosshatch,
                VideoPattern::Checkerboard,
                VideoPattern::SolidColor,
                VideoPattern::White,
                VideoPattern::Black,
                VideoPattern::Noise,
                VideoPattern::ZonePlate,
                VideoPattern::ColorChecker,
                VideoPattern::SMPTE219,
                VideoPattern::AvSync,
                VideoPattern::MultiBurst,
                VideoPattern::LimitRange,
                VideoPattern::CircularZone,
                VideoPattern::Alignment,
                VideoPattern::SDIPathEQ,
                VideoPattern::SDIPathPLL
        };

        for(auto pat : patterns) {
                gen.setPattern(pat);
                auto img = gen.createPayload(desc, 0.0);
                REQUIRE(img.isValid());
                CHECK(img->desc().width() == 320);
                CHECK(img->desc().height() == 240);
        }
}

// ============================================================================
// render() into existing image
// ============================================================================

TEST_CASE("VideoTestPattern_RenderIntoExisting") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::White);

        ImageDesc desc = testDesc(64, 64);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *data = img->plane(0).data();
        CHECK(data[0] == 255);
        CHECK(data[1] == 255);
        CHECK(data[2] == 255);
}

// ============================================================================
// Motion offset
// ============================================================================

TEST_CASE("VideoTestPattern_MotionOffset") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::ColorBars);

        ImageDesc desc = testDesc(64, 64);
        auto img1 = gen.createPayload(desc, 0.0);
        auto img2 = gen.createPayload(desc, 10.0);

        REQUIRE(img1.isValid());
        REQUIRE(img2.isValid());

        const uint8_t *d1 = img1->plane(0).data();
        const uint8_t *d2 = img2->plane(0).data();
        bool differ = false;
        for(size_t i = 0; i < 64 * 3; i++) {
                if(d1[i] != d2[i]) { differ = true; break; }
        }
        CHECK(differ);
}

// ============================================================================
// SolidColor uses configured color
// ============================================================================

TEST_CASE("VideoTestPattern_SolidColor") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SolidColor);
        gen.setSolidColor(Color::Red);

        ImageDesc desc = testDesc(8, 8);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *data = img->plane(0).data();
        CHECK(data[0] == 255);
        CHECK(data[1] == 0);
        CHECK(data[2] == 0);
}

// ============================================================================
// RGBA8 pixel format
// ============================================================================

TEST_CASE("VideoTestPattern_RGBA8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::White);

        ImageDesc desc = testDesc(16, 16, PixelFormat::RGBA8_sRGB);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());
        CHECK(img->desc().pixelFormat().id() == PixelFormat::RGBA8_sRGB);
}

// ============================================================================
// VideoPattern name round-trip via the TypedEnum machinery
// ============================================================================

TEST_CASE("VideoTestPattern_StringRoundTrip") {
        VideoPattern patterns[] = {
                VideoPattern::ColorBars,
                VideoPattern::ColorBars75,
                VideoPattern::Ramp,
                VideoPattern::Grid,
                VideoPattern::Crosshatch,
                VideoPattern::Checkerboard,
                VideoPattern::SolidColor,
                VideoPattern::White,
                VideoPattern::Black,
                VideoPattern::Noise,
                VideoPattern::ZonePlate,
                VideoPattern::ColorChecker,
                VideoPattern::SMPTE219,
                VideoPattern::AvSync,
                VideoPattern::MultiBurst,
                VideoPattern::LimitRange,
                VideoPattern::CircularZone,
                VideoPattern::Alignment,
                VideoPattern::SDIPathEQ,
                VideoPattern::SDIPathPLL
        };

        for(auto pat : patterns) {
                String name = Enum::nameOf(VideoPattern::Type, pat.value());
                CHECK_FALSE(name.isEmpty());
                VideoPattern parsed(name);
                CHECK(parsed.hasListedValue());
                CHECK(parsed == pat);
        }
}

TEST_CASE("VideoTestPattern_FromStringInvalid") {
        VideoPattern bogus("bogus");
        CHECK_FALSE(bogus.hasListedValue());
}

// ============================================================================
// BurnPosition name round-trip
// ============================================================================

TEST_CASE("VideoTestPattern_BurnPositionRoundTrip") {
        struct Case { BurnPosition pos; const char *name; };
        Case cases[] = {
                { BurnPosition::TopLeft,      "TopLeft"      },
                { BurnPosition::TopCenter,    "TopCenter"    },
                { BurnPosition::TopRight,     "TopRight"     },
                { BurnPosition::BottomLeft,   "BottomLeft"   },
                { BurnPosition::BottomCenter, "BottomCenter" },
                { BurnPosition::BottomRight,  "BottomRight"  },
                { BurnPosition::Center,       "Center"       },
        };

        for(auto &c : cases) {
                String name = Enum::nameOf(BurnPosition::Type, c.pos.value());
                CHECK(name == String(c.name));

                BurnPosition parsed(name);
                CHECK(parsed.hasListedValue());
                CHECK(parsed == c.pos);
        }
}

TEST_CASE("VideoTestPattern_BurnPositionFromStringInvalid") {
        BurnPosition bogus("bogus");
        CHECK_FALSE(bogus.hasListedValue());
}

// ============================================================================
// BurnCenter enum value is stable at 6
// ============================================================================

TEST_CASE("VideoTestPattern_BurnCenterValue") {
        CHECK(BurnPosition::Center.value() == 6);
}

#include <promeki/timecode.h>

static bool payloadPixelMatches(const UncompressedVideoPayload &img, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t *data = img.plane(0).data();
        return data[0] == r && data[1] == g && data[2] == b;
}

TEST_CASE("VideoTestPattern_AvSyncCachedWhiteAndBlack") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::AvSync);
        ImageDesc desc(64, 32, PixelFormat::RGB8_sRGB);

        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);
        REQUIRE(marker.isValid());
        REQUIRE(marker.frame() == 0);
        auto white = gen.createPayload(desc, 0.0, marker);
        REQUIRE(white.isValid());
        CHECK(payloadPixelMatches(*white, 255, 255, 255));

        Timecode nonMarker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 5);
        REQUIRE(nonMarker.isValid());
        REQUIRE(nonMarker.frame() == 5);
        auto black = gen.createPayload(desc, 0.0, nonMarker);
        REQUIRE(black.isValid());
        CHECK(payloadPixelMatches(*black, 0, 0, 0));

        // Repeated calls reuse the cached planes.
        auto white2 = gen.createPayload(desc, 0.0, marker);
        REQUIRE(white2.isValid());
        CHECK(white2->plane(0).data() == white->plane(0).data());

        auto black2 = gen.createPayload(desc, 0.0, nonMarker);
        REQUIRE(black2.isValid());
        CHECK(black2->plane(0).data() == black->plane(0).data());

        auto fallback = gen.createPayload(desc, 0.0, Timecode());
        REQUIRE(fallback.isValid());
        CHECK(payloadPixelMatches(*fallback, 0, 0, 0));
}

// ============================================================================
// SDI pathological patterns — exact byte verification
// ============================================================================

TEST_CASE("VideoTestPattern_SDIPathEQ_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0xC0);
        CHECK(row0[1] == 0x66);
        CHECK(row0[2] == 0xC0);
        CHECK(row0[3] == 0x66);
        CHECK(row0[124] == 0xC0);
        CHECK(row0[125] == 0x66);

        size_t stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0x66);
        CHECK(row1[1] == 0xC0);
        CHECK(row1[2] == 0x66);
        CHECK(row1[3] == 0xC0);
}

TEST_CASE("VideoTestPattern_SDIPathPLL_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathPLL);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
        CHECK(row0[2] == 0x80);
        CHECK(row0[3] == 0x44);

        size_t stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0x44);
        CHECK(row1[1] == 0x80);
        CHECK(row1[2] == 0x44);
        CHECK(row1[3] == 0x80);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_UYVY10LE") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV10_422_UYVY_LE_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint16_t *row0 = reinterpret_cast<const uint16_t *>(img->plane(0).data());
        CHECK(row0[0] == 0x0300);
        CHECK(row0[1] == 0x0198);
        CHECK(row0[2] == 0x0300);
        CHECK(row0[3] == 0x0198);

        size_t stride = stride0(*img);
        const uint16_t *row1 = reinterpret_cast<const uint16_t *>(
                img->plane(0).data() + stride);
        CHECK(row1[0] == 0x0198);
        CHECK(row1[1] == 0x0300);
        CHECK(row1[2] == 0x0198);
        CHECK(row1[3] == 0x0300);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_YUYV8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        CHECK(row0[0] == 0x66);
        CHECK(row0[1] == 0xC0);
        CHECK(row0[2] == 0x66);
        CHECK(row0[3] == 0xC0);

        size_t stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        CHECK(row1[0] == 0xC0);
        CHECK(row1[1] == 0x66);
        CHECK(row1[2] == 0xC0);
        CHECK(row1[3] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_v210") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV10_422_v210_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint32_t *row0 = reinterpret_cast<const uint32_t *>(img->plane(0).data());
        CHECK((row0[0] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[1] & 0x3FFFFFFFu) == 0x198C0198u);
        CHECK((row0[2] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[3] & 0x3FFFFFFFu) == 0x198C0198u);

        size_t stride = stride0(*img);
        const uint32_t *row1 = reinterpret_cast<const uint32_t *>(
                img->plane(0).data() + stride);
        CHECK((row1[0] & 0x3FFFFFFFu) == 0x198C0198u);
        CHECK((row1[1] & 0x3FFFFFFFu) == 0x30066300u);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_Planar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Planar_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *yRow0 = img->plane(0).data();
        CHECK(yRow0[0] == 0x66);
        CHECK(yRow0[63] == 0x66);
        size_t yStride = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        CHECK(yRow0[yStride] == 0xC0);

        const uint8_t *cbRow0 = img->plane(1).data();
        CHECK(cbRow0[0] == 0xC0);
        CHECK(cbRow0[31] == 0xC0);
        size_t cbStride = img->desc().pixelFormat().memLayout().lineStride(1, img->desc().width());
        CHECK(cbRow0[cbStride] == 0x66);

        const uint8_t *crRow0 = img->plane(2).data();
        CHECK(crRow0[0] == 0xC0);
        CHECK(crRow0[cbStride] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_SemiPlanar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_SemiPlanar_Rec709);
        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *yRow0 = img->plane(0).data();
        CHECK(yRow0[0] == 0x66);
        size_t yStride = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        CHECK(yRow0[yStride] == 0xC0);

        const uint8_t *cRow0 = img->plane(1).data();
        CHECK(cRow0[0] == 0xC0);
        CHECK(cRow0[1] == 0xC0);
        size_t cStride = img->desc().pixelFormat().memLayout().lineStride(1, img->desc().width());
        CHECK(cRow0[cStride] == 0x66);
        CHECK(cRow0[cStride + 1] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPath_CacheBehavior") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);

        auto a = gen.createPayload(desc, 0.0);
        auto b = gen.createPayload(desc, 0.0);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        CHECK(a->plane(0).data() == b->plane(0).data());

        gen.setPattern(VideoPattern::SDIPathPLL);
        auto c = gen.createPayload(desc, 0.0);
        REQUIRE(c.isValid());
        CHECK(c->plane(0).data() != a->plane(0).data());

        const uint8_t *row0 = c->plane(0).data();
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
}

TEST_CASE("VideoTestPattern_SDIPath_RGBFallback") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::RGB8_sRGB);

        auto img = gen.createPayload(desc, 0.0);
        REQUIRE(img.isValid());

        const uint8_t *row0 = img->plane(0).data();
        size_t stride = stride0(*img);
        const uint8_t *row1 = row0 + stride;
        bool linesDiffer = false;
        for(int i = 0; i < 3; i++) {
                if(row0[i] != row1[i]) { linesDiffer = true; break; }
        }
        CHECK(linesDiffer);
}

TEST_CASE("VideoTestPattern_AvSyncCacheRebuildsOnDescChange") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::AvSync);
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);

        auto a = gen.createPayload(ImageDesc(32, 16, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(a.isValid());
        CHECK(a->desc().width() == 32);
        CHECK(a->desc().height() == 16);
        CHECK(payloadPixelMatches(*a, 255, 255, 255));

        auto b = gen.createPayload(ImageDesc(64, 32, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(b.isValid());
        CHECK(b->desc().width() == 64);
        CHECK(b->desc().height() == 32);
        CHECK(payloadPixelMatches(*b, 255, 255, 255));
        CHECK(b->plane(0).data() != a->plane(0).data());
}
