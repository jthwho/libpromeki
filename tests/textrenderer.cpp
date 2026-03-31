/**
 * @file      tests/textrenderer.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/textrenderer.h>
#include <promeki/proav/image.h>
#include <promeki/proav/pixelformat.h>
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

TEST_CASE("TextRenderer: default construction") {
        TextRenderer tr;
        CHECK(tr.fontFilename().isEmpty());
        CHECK(tr.fontSize() == 12);
        CHECK(tr.lineHeight() == 0);
        CHECK(tr.ascender() == 0);
}

// ============================================================================
// Setters
// ============================================================================

TEST_CASE("TextRenderer: setFontFilename") {
        TextRenderer tr;
        tr.setFontFilename("/some/path.ttf");
        CHECK(tr.fontFilename() == "/some/path.ttf");
}

TEST_CASE("TextRenderer: setFontFilename same value no-op") {
        TextRenderer tr;
        tr.setFontFilename("/some/path.ttf");
        // Setting same value should not crash or misbehave
        tr.setFontFilename("/some/path.ttf");
        CHECK(tr.fontFilename() == "/some/path.ttf");
}

TEST_CASE("TextRenderer: setFontSize") {
        TextRenderer tr;
        tr.setFontSize(48);
        CHECK(tr.fontSize() == 48);
}

TEST_CASE("TextRenderer: setFontSize same value no-op") {
        TextRenderer tr;
        tr.setFontSize(12);
        CHECK(tr.fontSize() == 12);
}

// ============================================================================
// drawText / measureText without font loaded
// ============================================================================

TEST_CASE("TextRenderer: drawText fails without font") {
        TextRenderer tr;
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);
        CHECK_FALSE(tr.drawText("test", 0, 0));
}

TEST_CASE("TextRenderer: measureText returns 0 without font") {
        TextRenderer tr;
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);
        CHECK(tr.measureText("test") == 0);
}

TEST_CASE("TextRenderer: drawText fails with bad font path") {
        TextRenderer tr;
        tr.setFontFilename("/nonexistent/font.ttf");
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);
        CHECK_FALSE(tr.drawText("test", 0, 0));
}

TEST_CASE("TextRenderer: measureText returns 0 with bad font path") {
        TextRenderer tr;
        tr.setFontFilename("/nonexistent/font.ttf");
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);
        CHECK(tr.measureText("test") == 0);
}

// ============================================================================
// Font loading and metrics
// ============================================================================

TEST_CASE("TextRenderer: font metrics after loading") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);

        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        // Trigger font loading via measureText
        int w = tr.measureText("A");
        CHECK(w > 0);
        CHECK(tr.lineHeight() > 0);
        CHECK(tr.ascender() > 0);
        CHECK(tr.lineHeight() >= tr.ascender());
}

// ============================================================================
// measureText
// ============================================================================

TEST_CASE("TextRenderer: measureText empty string") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        CHECK(tr.measureText("") == 0);
}

TEST_CASE("TextRenderer: measureText single character") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        int w = tr.measureText("A");
        CHECK(w > 0);
}

TEST_CASE("TextRenderer: measureText longer string is wider") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        Image img(256, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        int w1 = tr.measureText("A");
        int w3 = tr.measureText("ABC");
        CHECK(w3 > w1);
}

TEST_CASE("TextRenderer: measureText monospace characters same width") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        Image img(256, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        // Fira Code is monospace, so all characters should be same width
        int wA = tr.measureText("A");
        int wW = tr.measureText("W");
        int wi = tr.measureText("i");
        CHECK(wA == wW);
        CHECK(wA == wi);
}

// ============================================================================
// drawText
// ============================================================================

TEST_CASE("TextRenderer: drawText renders pixels") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        tr.setForegroundColor(Color::White);
        tr.setBackgroundColor(Color::Black);

        Image img(256, 64, PixelFormat::RGB8);
        img.fill(128); // fill with gray so we can detect changes
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        bool ok = tr.drawText("Hello", 10, 40);
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

TEST_CASE("TextRenderer: drawText with different colors") {
        if(!fontAvailable()) return;

        auto renderAndSum = [](Color fg, Color bg) -> uint64_t {
                TextRenderer tr;
                tr.setFontFilename(testFontPath);
                tr.setFontSize(24);
                tr.setForegroundColor(fg);
                tr.setBackgroundColor(bg);

                Image img(256, 64, PixelFormat::RGB8);
                img.fill(0);
                PaintEngine pe = img.createPaintEngine();
                tr.setPaintEngine(pe);
                tr.drawText("ABC", 10, 40);

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

TEST_CASE("TextRenderer: drawText on RGBA8") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);
        tr.setForegroundColor(Color::White);
        tr.setBackgroundColor(Color::Black);

        Image img(256, 64, PixelFormat::RGBA8);
        img.fill(0);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        bool ok = tr.drawText("Test", 10, 40);
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

TEST_CASE("TextRenderer: changing font size invalidates cache") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(12);

        Image img(256, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        int w12 = tr.measureText("ABC");

        // Change font size - cache should be invalidated
        tr.setFontSize(48);
        tr.setPaintEngine(pe); // re-attach paint engine after invalidation
        int w48 = tr.measureText("ABC");

        CHECK(w12 > 0);
        CHECK(w48 > 0);
        CHECK(w48 > w12);
}

TEST_CASE("TextRenderer: changing font filename invalidates cache") {
        if(!fontAvailable()) return;

        TextRenderer tr;
        tr.setFontFilename(testFontPath);
        tr.setFontSize(24);

        Image img(256, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        tr.setPaintEngine(pe);

        int w = tr.measureText("ABC");
        CHECK(w > 0);

        // Change to invalid font - should fail
        tr.setFontFilename("/nonexistent/font.ttf");
        tr.setPaintEngine(pe);
        int w2 = tr.measureText("ABC");
        CHECK(w2 == 0);
}

