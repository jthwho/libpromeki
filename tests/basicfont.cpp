/**
 * @file      tests/basicfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/basicfont.h>
#include <promeki/proav/fastfont.h>
#include <promeki/proav/image.h>
#include <promeki/core/pixeldesc.h>
#include <promeki/core/color.h>
#include <promeki/core/filepath.h>

using namespace promeki;

static const String testFontPath = String(PROMEKI_SOURCE_DIR) + "/etc/fonts/FiraCodeNerdFontMono-Regular.ttf";

static bool fontAvailable() {
        return FilePath(testFontPath).exists();
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("BasicFont: construction with default PaintEngine") {
        BasicFont bf{PaintEngine()};
        CHECK(bf.fontFilename().isEmpty());
        CHECK(bf.fontSize() == 12);
        CHECK(bf.lineHeight() == 0);
        CHECK(bf.ascender() == 0);
        CHECK(bf.descender() == 0);
        CHECK_FALSE(bf.kerningEnabled());
        CHECK_FALSE(bf.isValid());
}

TEST_CASE("BasicFont: construction with real PaintEngine") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        CHECK(bf.fontFilename().isEmpty());
        CHECK_FALSE(bf.isValid());
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("BasicFont: setFontFilename") {
        BasicFont bf{PaintEngine()};
        bf.setFontFilename("/some/path.ttf");
        CHECK(bf.fontFilename() == "/some/path.ttf");
}

TEST_CASE("BasicFont: setFontSize") {
        BasicFont bf{PaintEngine()};
        bf.setFontSize(48);
        CHECK(bf.fontSize() == 48);
}

TEST_CASE("BasicFont: setForegroundColor") {
        BasicFont bf{PaintEngine()};
        bf.setForegroundColor(Color::Red);
        CHECK(bf.foregroundColor() == Color::Red);
}

TEST_CASE("BasicFont: setKerningEnabled") {
        BasicFont bf{PaintEngine()};
        bf.setKerningEnabled(true);
        CHECK(bf.kerningEnabled());
}

// ============================================================================
// drawText / measureText without font loaded
// ============================================================================

TEST_CASE("BasicFont: drawText fails without font") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        CHECK_FALSE(bf.drawText("test", 0, 0));
}

TEST_CASE("BasicFont: measureText returns 0 without font") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        CHECK(bf.measureText("test") == 0);
}

TEST_CASE("BasicFont: drawText fails with bad font path") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename("/nonexistent/font.ttf");
        CHECK_FALSE(bf.drawText("test", 0, 0));
}

// ============================================================================
// Font loading and metrics
// ============================================================================

TEST_CASE("BasicFont: font metrics after loading") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);

        int w = bf.measureText("A");
        CHECK(w > 0);
        CHECK(bf.lineHeight() > 0);
        CHECK(bf.ascender() > 0);
        CHECK(bf.descender() > 0);
        CHECK(bf.lineHeight() >= bf.ascender());
}

// ============================================================================
// measureText
// ============================================================================

TEST_CASE("BasicFont: measureText empty string") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);

        CHECK(bf.measureText("") == 0);
}

TEST_CASE("BasicFont: measureText longer string is wider") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);

        int w1 = bf.measureText("A");
        int w3 = bf.measureText("ABC");
        CHECK(w3 > w1);
}

// ============================================================================
// drawText
// ============================================================================

TEST_CASE("BasicFont: drawText renders pixels") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        img.fill(0);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("Hello", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

TEST_CASE("BasicFont: drawText uses foreground color") {
        if(!fontAvailable()) return;

        auto renderAndSum = [](Color fg) -> uint64_t {
                Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
                img.fill(0);
                BasicFont bf(img.createPaintEngine());
                bf.setFontFilename(testFontPath);
                bf.setFontSize(24);
                bf.setForegroundColor(fg);
                bf.drawText("ABC", 10, 40);

                const uint8_t *data = static_cast<const uint8_t *>(img.data());
                size_t total = img.lineStride() * img.height();
                uint64_t sum = 0;
                for(size_t i = 0; i < total; i++) sum += data[i];
                return sum;
        };

        uint64_t sumWhite = renderAndSum(Color::White);
        uint64_t sumRed = renderAndSum(Color::Red);
        CHECK(sumWhite > 0);
        CHECK(sumRed > 0);
        // Different foreground colors produce different pixel data
        CHECK(sumWhite != sumRed);
}

TEST_CASE("BasicFont: drawText on RGBA8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGBA8_sRGB_Full);
        img.fill(0);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

// ============================================================================
// Font property changes without recreation
// ============================================================================

TEST_CASE("BasicFont: changing font size works") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(12);

        int w12 = bf.measureText("ABC");

        bf.setFontSize(48);
        int w48 = bf.measureText("ABC");

        CHECK(w12 > 0);
        CHECK(w48 > 0);
        CHECK(w48 > w12);
}

TEST_CASE("BasicFont: changing font filename works") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);

        int w = bf.measureText("ABC");
        CHECK(w > 0);

        bf.setFontFilename("/nonexistent/font.ttf");
        int w2 = bf.measureText("ABC");
        CHECK(w2 == 0);
}

// ============================================================================
// Deferred PaintEngine pattern
// ============================================================================

TEST_CASE("BasicFont: deferred PaintEngine on RGB8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        img.fill(0);

        BasicFont bf{PaintEngine()};
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);
        bf.setPaintEngine(img.createPaintEngine());

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

TEST_CASE("BasicFont: deferred PaintEngine on RGBA8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGBA8_sRGB_Full);
        img.fill(0);

        BasicFont bf{PaintEngine()};
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);
        bf.setPaintEngine(img.createPaintEngine());

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

// ============================================================================
// Drawing at bottom of large images
// ============================================================================

TEST_CASE("BasicFont: drawText at bottom of large RGBA8 image") {
        if(!fontAvailable()) return;

        Image img(1920, 1080, PixelDesc::RGBA8_sRGB_Full);
        img.fill(0);

        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(48);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("01:00:00:00", 795, 1040);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

TEST_CASE("BasicFont: drawText at bottom of large RGB8 image") {
        if(!fontAvailable()) return;

        Image img(1920, 1080, PixelDesc::RGB8_sRGB_Full);
        img.fill(0);

        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(48);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("01:00:00:00", 795, 1040);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

// ============================================================================
// Font metrics consistency
// ============================================================================

TEST_CASE("BasicFont: lineHeight equals ascender plus descender") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        BasicFont bf(img.createPaintEngine());
        bf.setFontFilename(testFontPath);
        bf.setFontSize(48);

        bf.measureText("A");

        CHECK(bf.lineHeight() == bf.ascender() + bf.descender());
}

TEST_CASE("BasicFont: metrics match FastFont for same font and size") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        PaintEngine pe = img.createPaintEngine();

        FastFont ff(pe);
        ff.setFontFilename(testFontPath);
        ff.setFontSize(48);
        ff.measureText("A");

        BasicFont bf(pe);
        bf.setFontFilename(testFontPath);
        bf.setFontSize(48);
        bf.measureText("A");

        CHECK(ff.lineHeight() == bf.lineHeight());
        CHECK(ff.ascender() == bf.ascender());
        CHECK(ff.descender() == bf.descender());
}

TEST_CASE("BasicFont: measureText matches FastFont") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        PaintEngine pe = img.createPaintEngine();

        FastFont ff(pe);
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        BasicFont bf(pe);
        bf.setFontFilename(testFontPath);
        bf.setFontSize(24);

        CHECK(ff.measureText("01:00:00:00") == bf.measureText("01:00:00:00"));
}
