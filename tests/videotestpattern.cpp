/**
 * @file      tests/videotestpattern.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/videotestpattern.h>
#include <promeki/image.h>
#include <promeki/imagedesc.h>
#include <promeki/pixeldesc.h>
#include <promeki/color.h>

using namespace promeki;

static ImageDesc testDesc(int w = 320, int h = 240, PixelDesc::ID fmt = PixelDesc::RGB8_sRGB) {
        return ImageDesc(w, h, fmt);
}

// ============================================================================
// Construction and defaults
// ============================================================================

TEST_CASE("VideoTestPattern_Defaults") {
        VideoTestPattern gen;
        CHECK(gen.pattern() == VideoTestPattern::ColorBars);
        CHECK_FALSE(gen.solidColor().isValid());
}

// ============================================================================
// create() produces valid images for all patterns
// ============================================================================

TEST_CASE("VideoTestPattern_CreateAllPatterns") {
        VideoTestPattern gen;
        ImageDesc desc = testDesc();

        VideoTestPattern::Pattern patterns[] = {
                VideoTestPattern::ColorBars,
                VideoTestPattern::ColorBars75,
                VideoTestPattern::Ramp,
                VideoTestPattern::Grid,
                VideoTestPattern::Crosshatch,
                VideoTestPattern::Checkerboard,
                VideoTestPattern::SolidColor,
                VideoTestPattern::White,
                VideoTestPattern::Black,
                VideoTestPattern::Noise,
                VideoTestPattern::ZonePlate,
                VideoTestPattern::AvSync
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
        gen.setPattern(VideoTestPattern::White);

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
        gen.setPattern(VideoTestPattern::ColorBars);

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
        gen.setPattern(VideoTestPattern::SolidColor);
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
        gen.setPattern(VideoTestPattern::White);

        ImageDesc desc = testDesc(16, 16, PixelDesc::RGBA8_sRGB);
        Image img = gen.create(desc);
        CHECK(img.isValid());
        CHECK(img.pixelDesc().id() == PixelDesc::RGBA8_sRGB);
}

// ============================================================================
// fromString / toString round-trip
// ============================================================================

TEST_CASE("VideoTestPattern_StringRoundTrip") {
        VideoTestPattern::Pattern patterns[] = {
                VideoTestPattern::ColorBars,
                VideoTestPattern::ColorBars75,
                VideoTestPattern::Ramp,
                VideoTestPattern::Grid,
                VideoTestPattern::Crosshatch,
                VideoTestPattern::Checkerboard,
                VideoTestPattern::SolidColor,
                VideoTestPattern::White,
                VideoTestPattern::Black,
                VideoTestPattern::Noise,
                VideoTestPattern::ZonePlate,
                VideoTestPattern::AvSync
        };

        for(auto pat : patterns) {
                String name = VideoTestPattern::toString(pat);
                CHECK_FALSE(name.isEmpty());
                auto [parsed, err] = VideoTestPattern::fromString(name);
                CHECK(err.isOk());
                CHECK(parsed == pat);
        }
}

TEST_CASE("VideoTestPattern_FromStringInvalid") {
        auto [pat, err] = VideoTestPattern::fromString("bogus");
        CHECK(err.isError());
}

#include <promeki/timecode.h>

static bool imagePixelMatches(const Image &img, uint8_t r, uint8_t g, uint8_t b) {
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        return data[0] == r && data[1] == g && data[2] == b;
}

TEST_CASE("VideoTestPattern_AvSyncCachedWhiteAndBlack") {
        VideoTestPattern gen;
        gen.setPattern(VideoTestPattern::AvSync);
        ImageDesc desc(64, 32, PixelDesc::RGB8_sRGB);

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

TEST_CASE("VideoTestPattern_AvSyncCacheRebuildsOnDescChange") {
        VideoTestPattern gen;
        gen.setPattern(VideoTestPattern::AvSync);
        Timecode marker(Timecode::Mode(FrameRate::FPS_30, false), 1, 0, 0, 0);

        Image a = gen.create(ImageDesc(32, 16, PixelDesc::RGB8_sRGB), 0.0, marker);
        REQUIRE(a.isValid());
        CHECK(a.width() == 32);
        CHECK(a.height() == 16);
        CHECK(imagePixelMatches(a, 255, 255, 255));

        Image b = gen.create(ImageDesc(64, 32, PixelDesc::RGB8_sRGB), 0.0, marker);
        REQUIRE(b.isValid());
        CHECK(b.width() == 64);
        CHECK(b.height() == 32);
        CHECK(imagePixelMatches(b, 255, 255, 255));
        // Different desc => different cache backing => different pointer
        CHECK(b.data() != a.data());
}
