/**
 * @file      tests/videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videotestpattern.h>
#include <promeki/enums.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>

using namespace promeki;

static ImageDesc testDesc(int w = 320, int h = 240, PixelFormat::ID fmt = PixelFormat::RGB8_sRGB) {
        return ImageDesc(w, h, fmt);
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
// create() produces valid images for all patterns
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
                Image img = gen.create(desc);
                CHECK(img.isValid());
                CHECK(img.width() == 320);
                CHECK(img.height() == 240);
        }
}

// ============================================================================
// render() into existing image
// ============================================================================

TEST_CASE("VideoTestPattern_RenderIntoExisting") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::White);

        ImageDesc desc = testDesc(64, 64);
        Image img(desc);
        img.fill(0);

        gen.render(img);
        CHECK(img.isValid());

        // White pattern should produce non-zero pixels
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
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
        Image img1 = gen.create(desc, 0.0);
        Image img2 = gen.create(desc, 10.0);

        CHECK(img1.isValid());
        CHECK(img2.isValid());

        // Images should differ due to motion offset
        const uint8_t *d1 = static_cast<const uint8_t *>(img1.data());
        const uint8_t *d2 = static_cast<const uint8_t *>(img2.data());
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
        Image img = gen.create(desc);
        CHECK(img.isValid());

        // First pixel should be pure red in RGB8
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
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
        Image img = gen.create(desc);
        CHECK(img.isValid());
        CHECK(img.pixelFormat().id() == PixelFormat::RGBA8_sRGB);
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

static bool imagePixelMatches(const Image &img, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        return data[0] == r && data[1] == g && data[2] == b;
}

TEST_CASE("VideoTestPattern_AvSyncCachedWhiteAndBlack") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::AvSync);
        ImageDesc desc(64, 32, PixelFormat::RGB8_sRGB);

        // tc.frame() == 0 -> white marker frame
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);
        REQUIRE(marker.isValid());
        REQUIRE(marker.frame() == 0);
        Image white = gen.create(desc, 0.0, marker);
        REQUIRE(white.isValid());
        CHECK(imagePixelMatches(white, 255, 255, 255));

        // tc.frame() != 0 -> black non-marker frame
        Timecode nonMarker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 5);
        REQUIRE(nonMarker.isValid());
        REQUIRE(nonMarker.frame() == 5);
        Image black = gen.create(desc, 0.0, nonMarker);
        REQUIRE(black.isValid());
        CHECK(imagePixelMatches(black, 0, 0, 0));

        // Repeated calls reuse the cached planes — the returned Image
        // should share the same underlying buffer as the first one.
        Image white2 = gen.create(desc, 0.0, marker);
        REQUIRE(white2.isValid());
        CHECK(white2.data() == white.data());

        Image black2 = gen.create(desc, 0.0, nonMarker);
        REQUIRE(black2.isValid());
        CHECK(black2.data() == black.data());

        // Default-constructed (invalid) timecode falls back to the
        // non-marker (black) frame so the pattern degrades gracefully
        // when no timecode is supplied.
        Image fallback = gen.create(desc, 0.0, Timecode());
        REQUIRE(fallback.isValid());
        CHECK(imagePixelMatches(fallback, 0, 0, 0));
}

// ============================================================================
// SDI pathological patterns — exact byte verification
// ============================================================================

TEST_CASE("VideoTestPattern_SDIPathEQ_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // 8-bit Check Field: W0 = 0xC0, W1 = 0x66
        const uint8_t *row0 = static_cast<const uint8_t *>(img.data(0));
        // Even line: Cb=W0 Y=W1 Cr=W0 Y=W1
        CHECK(row0[0] == 0xC0);
        CHECK(row0[1] == 0x66);
        CHECK(row0[2] == 0xC0);
        CHECK(row0[3] == 0x66);
        // Verify last macro block on even line
        CHECK(row0[124] == 0xC0);
        CHECK(row0[125] == 0x66);

        size_t stride = img.lineStride(0);
        const uint8_t *row1 = row0 + stride;
        // Odd line: Cb=W1 Y=W0 Cr=W1 Y=W0
        CHECK(row1[0] == 0x66);
        CHECK(row1[1] == 0xC0);
        CHECK(row1[2] == 0x66);
        CHECK(row1[3] == 0xC0);
}

TEST_CASE("VideoTestPattern_SDIPathPLL_UYVY8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathPLL);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // 8-bit Matrix: W0 = 0x80, W1 = 0x44
        const uint8_t *row0 = static_cast<const uint8_t *>(img.data(0));
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
        CHECK(row0[2] == 0x80);
        CHECK(row0[3] == 0x44);

        size_t stride = img.lineStride(0);
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
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // 10-bit LE Check Field: W0 = 0x0300, W1 = 0x0198
        const uint16_t *row0 = static_cast<const uint16_t *>(img.data(0));
        // Even line: Cb=W0 Y=W1 Cr=W0 Y=W1
        CHECK(row0[0] == 0x0300);
        CHECK(row0[1] == 0x0198);
        CHECK(row0[2] == 0x0300);
        CHECK(row0[3] == 0x0198);

        size_t stride = img.lineStride(0);
        const uint16_t *row1 = reinterpret_cast<const uint16_t *>(
                static_cast<const uint8_t *>(img.data(0)) + stride);
        // Odd line: Cb=W1 Y=W0 Cr=W1 Y=W0
        CHECK(row1[0] == 0x0198);
        CHECK(row1[1] == 0x0300);
        CHECK(row1[2] == 0x0198);
        CHECK(row1[3] == 0x0300);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_YUYV8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        const uint8_t *row0 = static_cast<const uint8_t *>(img.data(0));
        // YUYV even: Y=W1 Cb=W0 Y=W1 Cr=W0
        CHECK(row0[0] == 0x66);
        CHECK(row0[1] == 0xC0);
        CHECK(row0[2] == 0x66);
        CHECK(row0[3] == 0xC0);

        size_t stride = img.lineStride(0);
        const uint8_t *row1 = row0 + stride;
        // YUYV odd: Y=W0 Cb=W1 Y=W0 Cr=W1
        CHECK(row1[0] == 0xC0);
        CHECK(row1[1] == 0x66);
        CHECK(row1[2] == 0xC0);
        CHECK(row1[3] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_v210") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV10_422_v210_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // v210 even line: word A = Cb0|Y0|Cr0, word B = Y1|Cb1|Y2
        // With Cb=0x300, Y=0x198, Cr=0x300:
        //   wordA = 0x300 | (0x198 << 10) | (0x300 << 20) = 0x30066300
        //   wordB = 0x198 | (0x300 << 10) | (0x198 << 20) = 0x198C0198
        const uint32_t *row0 = static_cast<const uint32_t *>(img.data(0));
        CHECK((row0[0] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[1] & 0x3FFFFFFFu) == 0x198C0198u);
        // Pattern repeats
        CHECK((row0[2] & 0x3FFFFFFFu) == 0x30066300u);
        CHECK((row0[3] & 0x3FFFFFFFu) == 0x198C0198u);

        size_t stride = img.lineStride(0);
        const uint32_t *row1 = reinterpret_cast<const uint32_t *>(
                static_cast<const uint8_t *>(img.data(0)) + stride);
        // Odd line: swapped
        CHECK((row1[0] & 0x3FFFFFFFu) == 0x198C0198u);
        CHECK((row1[1] & 0x3FFFFFFFu) == 0x30066300u);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_Planar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_Planar_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // Plane 0 (Y): even=0x66, odd=0xC0
        const uint8_t *yRow0 = static_cast<const uint8_t *>(img.data(0));
        CHECK(yRow0[0] == 0x66);
        CHECK(yRow0[63] == 0x66);
        size_t yStride = img.lineStride(0);
        CHECK(yRow0[yStride] == 0xC0);

        // Plane 1 (Cb): even=0xC0, odd=0x66
        const uint8_t *cbRow0 = static_cast<const uint8_t *>(img.data(1));
        CHECK(cbRow0[0] == 0xC0);
        CHECK(cbRow0[31] == 0xC0);
        size_t cbStride = img.lineStride(1);
        CHECK(cbRow0[cbStride] == 0x66);

        // Plane 2 (Cr): even=0xC0, odd=0x66
        const uint8_t *crRow0 = static_cast<const uint8_t *>(img.data(2));
        CHECK(crRow0[0] == 0xC0);
        CHECK(crRow0[cbStride] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPathEQ_SemiPlanar8") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_SemiPlanar_Rec709);
        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        // Plane 0 (Y): even=0x66, odd=0xC0
        const uint8_t *yRow0 = static_cast<const uint8_t *>(img.data(0));
        CHECK(yRow0[0] == 0x66);
        size_t yStride = img.lineStride(0);
        CHECK(yRow0[yStride] == 0xC0);

        // Plane 1 (CbCr): even=[0xC0,0xC0], odd=[0x66,0x66]
        const uint8_t *cRow0 = static_cast<const uint8_t *>(img.data(1));
        CHECK(cRow0[0] == 0xC0);
        CHECK(cRow0[1] == 0xC0);
        size_t cStride = img.lineStride(1);
        CHECK(cRow0[cStride] == 0x66);
        CHECK(cRow0[cStride + 1] == 0x66);
}

TEST_CASE("VideoTestPattern_SDIPath_CacheBehavior") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::YUV8_422_UYVY_Rec709);

        Image a = gen.create(desc);
        Image b = gen.create(desc);
        REQUIRE(a.isValid());
        REQUIRE(b.isValid());
        CHECK(a.data() == b.data());

        gen.setPattern(VideoPattern::SDIPathPLL);
        Image c = gen.create(desc);
        REQUIRE(c.isValid());
        CHECK(c.data() != a.data());

        const uint8_t *row0 = static_cast<const uint8_t *>(c.data(0));
        CHECK(row0[0] == 0x80);
        CHECK(row0[1] == 0x44);
}

TEST_CASE("VideoTestPattern_SDIPath_RGBFallback") {
        VideoTestPattern gen;
        gen.setPattern(VideoPattern::SDIPathEQ);
        ImageDesc desc(64, 4, PixelFormat::RGB8_sRGB);

        Image img = gen.create(desc);
        REQUIRE(img.isValid());

        const uint8_t *row0 = static_cast<const uint8_t *>(img.data(0));
        size_t stride = img.lineStride(0);
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

        Image a = gen.create(ImageDesc(32, 16, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(a.isValid());
        CHECK(a.width() == 32);
        CHECK(a.height() == 16);
        CHECK(imagePixelMatches(a, 255, 255, 255));

        Image b = gen.create(ImageDesc(64, 32, PixelFormat::RGB8_sRGB), 0.0, marker);
        REQUIRE(b.isValid());
        CHECK(b.width() == 64);
        CHECK(b.height() == 32);
        CHECK(imagePixelMatches(b, 255, 255, 255));
        // Different desc => different cache backing => different pointer
        CHECK(b.data() != a.data());
}
