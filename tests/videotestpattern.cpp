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
        CHECK(gen.solidColorR() == 0);
        CHECK(gen.solidColorG() == 0);
        CHECK(gen.solidColorB() == 0);
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
                VideoTestPattern::ZonePlate
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
        gen.setSolidColor(65535, 0, 32768);

        ImageDesc desc = testDesc(8, 8);
        Image img = gen.create(desc);
        CHECK(img.isValid());

        // First pixel should be R=255, G=0, B=128 (16-bit mapped to 8-bit)
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        CHECK(data[0] == 255);
        CHECK(data[1] == 0);
        CHECK(data[2] == 128);
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
                VideoTestPattern::ZonePlate
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
