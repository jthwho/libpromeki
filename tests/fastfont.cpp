/**
 * @file      tests/fastfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
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

TEST_CASE("FastFont: construction with default PaintEngine") {
        FastFont ff{PaintEngine()};
        CHECK(ff.fontFilename().isEmpty());
        CHECK(ff.fontSize() == 12);
        CHECK(ff.lineHeight() == 0);
        CHECK(ff.ascender() == 0);
        CHECK(ff.descender() == 0);
        CHECK_FALSE(ff.kerningEnabled());
        CHECK_FALSE(ff.isValid());
}

TEST_CASE("FastFont: construction with real PaintEngine") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        CHECK(ff.fontFilename().isEmpty());
        CHECK(ff.fontSize() == 12);
        CHECK_FALSE(ff.isValid());
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("FastFont: setFontFilename") {
        FastFont ff{PaintEngine()};
        ff.setFontFilename("/some/path.ttf");
        CHECK(ff.fontFilename() == "/some/path.ttf");
}

TEST_CASE("FastFont: setFontFilename same value no-op") {
        FastFont ff{PaintEngine()};
        ff.setFontFilename("/some/path.ttf");
        ff.setFontFilename("/some/path.ttf");
        CHECK(ff.fontFilename() == "/some/path.ttf");
}

TEST_CASE("FastFont: setFontSize") {
        FastFont ff{PaintEngine()};
        ff.setFontSize(48);
        CHECK(ff.fontSize() == 48);
}

TEST_CASE("FastFont: setFontSize same value no-op") {
        FastFont ff{PaintEngine()};
        ff.setFontSize(12);
        CHECK(ff.fontSize() == 12);
}

TEST_CASE("FastFont: isValid requires all properties") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        CHECK_FALSE(ff.isValid());

        ff.setFontFilename(testFontPath);
        CHECK(ff.isValid());

        ff.setFontSize(0);
        CHECK_FALSE(ff.isValid());
}

// ============================================================================
// drawText / measureText without font loaded
// ============================================================================

TEST_CASE("FastFont: drawText fails without font") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        CHECK_FALSE(ff.drawText("test", 0, 0));
}

TEST_CASE("FastFont: measureText returns 0 without font") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        CHECK(ff.measureText("test") == 0);
}

TEST_CASE("FastFont: drawText fails with bad font path") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename("/nonexistent/font.ttf");
        CHECK_FALSE(ff.drawText("test", 0, 0));
}

TEST_CASE("FastFont: measureText returns 0 with bad font path") {
        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename("/nonexistent/font.ttf");
        CHECK(ff.measureText("test") == 0);
}

// ============================================================================
// Font loading and metrics
// ============================================================================

TEST_CASE("FastFont: font metrics after loading") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        // Trigger font loading via measureText
        int w = ff.measureText("A");
        CHECK(w > 0);
        CHECK(ff.lineHeight() > 0);
        CHECK(ff.ascender() > 0);
        CHECK(ff.descender() > 0);
        CHECK(ff.lineHeight() >= ff.ascender());
}

// ============================================================================
// measureText
// ============================================================================

TEST_CASE("FastFont: measureText empty string") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        CHECK(ff.measureText("") == 0);
}

TEST_CASE("FastFont: measureText single character") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        int w = ff.measureText("A");
        CHECK(w > 0);
}

TEST_CASE("FastFont: measureText longer string is wider") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        int w1 = ff.measureText("A");
        int w3 = ff.measureText("ABC");
        CHECK(w3 > w1);
}

TEST_CASE("FastFont: measureText monospace characters same width") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        // Fira Code is monospace, so all characters should be same width
        int wA = ff.measureText("A");
        int wW = ff.measureText("W");
        int wi = ff.measureText("i");
        CHECK(wA == wW);
        CHECK(wA == wi);
}

// ============================================================================
// drawText
// ============================================================================

TEST_CASE("FastFont: drawText renders pixels") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        img.fill(128);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        bool ok = ff.drawText("Hello", 10, 40);
        CHECK(ok);

        // Verify that some pixels changed from the gray fill
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        size_t diffCount = 0;
        for(size_t i = 0; i < total; i++) {
                if(data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: drawText with different colors") {
        if(!fontAvailable()) return;

        auto renderAndSum = [](Color fg, Color bg) -> uint64_t {
                Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
                img.fill(0);
                FastFont ff(img.createPaintEngine());
                ff.setFontFilename(testFontPath);
                ff.setFontSize(24);
                ff.setForegroundColor(fg);
                ff.setBackgroundColor(bg);
                ff.drawText("ABC", 10, 40);

                const uint8_t *data = static_cast<const uint8_t *>(img.data());
                size_t total = img.lineStride() * img.height();
                uint64_t sum = 0;
                for(size_t i = 0; i < total; i++) sum += data[i];
                return sum;
        };

        uint64_t sumWhiteBlack = renderAndSum(Color::White, Color::Black);
        uint64_t sumRedGreen = renderAndSum(Color::Red, Color::Green);
        CHECK(sumWhiteBlack > 0);
        CHECK(sumRedGreen > 0);
        // Different color combinations produce different pixel data
        CHECK(sumWhiteBlack != sumRedGreen);
}

TEST_CASE("FastFont: drawText on RGBA8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGBA8_sRGB_Full);
        img.fill(0);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        CHECK(sum > 0);
}

// ============================================================================
// Cache invalidation
// ============================================================================

TEST_CASE("FastFont: changing font size invalidates cache") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(12);

        int w12 = ff.measureText("ABC");

        ff.setFontSize(48);
        int w48 = ff.measureText("ABC");

        CHECK(w12 > 0);
        CHECK(w48 > 0);
        CHECK(w48 > w12);
}

TEST_CASE("FastFont: changing font filename invalidates cache") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        int w = ff.measureText("ABC");
        CHECK(w > 0);

        // Change to invalid font - should fail
        ff.setFontFilename("/nonexistent/font.ttf");
        int w2 = ff.measureText("ABC");
        CHECK(w2 == 0);
}

TEST_CASE("FastFont: setPaintEngine same pixel format preserves cache") {
        if(!fontAvailable()) return;

        Image img1(256, 64, PixelDesc::RGB8_sRGB_Full);
        Image img2(256, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img1.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);

        // Load font and cache glyphs
        int w1 = ff.measureText("ABC");
        CHECK(w1 > 0);

        // Switch to a different PaintEngine with the same pixel format
        ff.setPaintEngine(img2.createPaintEngine());

        // Should still work without re-loading font
        int w2 = ff.measureText("ABC");
        CHECK(w2 == w1);
}

// ============================================================================
// Deferred PaintEngine pattern (construct default, then switch)
// ============================================================================

TEST_CASE("FastFont: deferred PaintEngine on RGB8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);
        img.fill(128);

        // Construct with no-op PaintEngine, configure, then switch
        FastFont ff{PaintEngine()};
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img.createPaintEngine());

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        size_t diffCount = 0;
        for(size_t i = 0; i < total; i++) {
                if(data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: deferred PaintEngine on RGBA8") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGBA8_sRGB_Full);
        img.fill(128);

        FastFont ff{PaintEngine()};
        ff.setFontFilename(testFontPath);
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img.createPaintEngine());

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        size_t total = img.lineStride() * img.height();
        size_t diffCount = 0;
        for(size_t i = 0; i < total; i++) {
                if(data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: deferred PaintEngine measures correctly") {
        if(!fontAvailable()) return;

        Image img(256, 64, PixelDesc::RGB8_sRGB_Full);

        // Direct construction for reference
        FastFont direct(img.createPaintEngine());
        direct.setFontFilename(testFontPath);
        direct.setFontSize(24);
        int wDirect = direct.measureText("ABC");

        // Deferred pattern
        FastFont deferred{PaintEngine()};
        deferred.setFontFilename(testFontPath);
        deferred.setFontSize(24);
        deferred.setPaintEngine(img.createPaintEngine());
        int wDeferred = deferred.measureText("ABC");

        CHECK(wDirect > 0);
        CHECK(wDeferred == wDirect);
}

// ============================================================================
// Drawing at bottom of large images (regression for RGBA8 blit clipping)
// ============================================================================

TEST_CASE("FastFont: drawText at bottom of large RGBA8 image") {
        if(!fontAvailable()) return;

        Image img(1920, 1080, PixelDesc::RGBA8_sRGB_Full);
        img.fill(128);

        FastFont ff{PaintEngine()};
        ff.setFontFilename(testFontPath);
        ff.setFontSize(48);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img.createPaintEngine());

        // Draw near the bottom (simulates TimecodeOverlayNode's BottomCenter)
        int baseline = 1040;
        bool ok = ff.drawText("01:00:00:00", 795, baseline);
        CHECK(ok);

        // Verify pixels changed in the text area
        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        int cellTop = baseline - ff.ascender();
        size_t changed = 0;
        for(int y = cellTop; y < cellTop + ff.lineHeight() && y < 1080; y++) {
                for(int x = 795; x < 795 + ff.measureText("01:00:00:00") && x < 1920; x++) {
                        size_t off = y * img.lineStride() + x * 4;
                        if(data[off] != 128 || data[off + 1] != 128 || data[off + 2] != 128) {
                                changed++;
                        }
                }
        }
        CHECK(changed > 0);
}

TEST_CASE("FastFont: drawText at bottom of large RGB8 image") {
        if(!fontAvailable()) return;

        Image img(1920, 1080, PixelDesc::RGB8_sRGB_Full);
        img.fill(128);

        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(48);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        int baseline = 1040;
        bool ok = ff.drawText("01:00:00:00", 795, baseline);
        CHECK(ok);

        const uint8_t *data = static_cast<const uint8_t *>(img.data());
        int cellTop = baseline - ff.ascender();
        size_t changed = 0;
        for(int y = cellTop; y < cellTop + ff.lineHeight() && y < 1080; y++) {
                for(int x = 795; x < 795 + ff.measureText("01:00:00:00") && x < 1920; x++) {
                        size_t off = y * img.lineStride() + x * 3;
                        if(data[off] != 128 || data[off + 1] != 128 || data[off + 2] != 128) {
                                changed++;
                        }
                }
        }
        CHECK(changed > 0);
}

// ============================================================================
// Font metrics consistency
// ============================================================================

TEST_CASE("FastFont: lineHeight equals ascender plus descender") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        FastFont ff(img.createPaintEngine());
        ff.setFontFilename(testFontPath);
        ff.setFontSize(48);

        // Trigger lazy loading
        ff.measureText("A");

        CHECK(ff.lineHeight() == ff.ascender() + ff.descender());
}

TEST_CASE("FastFont: metrics scale with font size") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);

        FastFont small(img.createPaintEngine());
        small.setFontFilename(testFontPath);
        small.setFontSize(12);
        small.measureText("A");

        FastFont large(img.createPaintEngine());
        large.setFontFilename(testFontPath);
        large.setFontSize(48);
        large.measureText("A");

        CHECK(large.lineHeight() > small.lineHeight());
        CHECK(large.ascender() > small.ascender());
        CHECK(large.descender() >= small.descender());
}

TEST_CASE("FastFont: metrics match between deferred and direct construction") {
        if(!fontAvailable()) return;

        Image img(64, 64, PixelDesc::RGB8_sRGB_Full);
        PaintEngine pe = img.createPaintEngine();

        FastFont direct(pe);
        direct.setFontFilename(testFontPath);
        direct.setFontSize(48);
        direct.measureText("A");

        FastFont deferred{PaintEngine()};
        deferred.setFontFilename(testFontPath);
        deferred.setFontSize(48);
        deferred.setPaintEngine(pe);
        deferred.measureText("A");

        CHECK(direct.lineHeight() == deferred.lineHeight());
        CHECK(direct.ascender() == deferred.ascender());
        CHECK(direct.descender() == deferred.descender());
}
