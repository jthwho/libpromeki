/**
 * @file      tests/basicfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/basicfont.h>
#include <promeki/fastfont.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>
#include <promeki/filepath.h>

using namespace promeki;

// Helper: allocate a zeroed payload of the given size / format.
static UncompressedVideoPayload::Ptr makePayload(size_t w, size_t h,
                                                 PixelFormat::ID id) {
        auto p = UncompressedVideoPayload::allocate(
                ImageDesc(w, h, PixelFormat(id)));
        for(size_t i = 0; i < p->planeCount(); ++i) {
                std::memset(p.modify()->data()[i].data(), 0, p->plane(i).size());
        }
        return p;
}

// Sum all bytes of plane 0 across the full payload extent.
static uint64_t planeSum(const UncompressedVideoPayload &p) {
        const uint8_t *data = p.plane(0).data();
        size_t total = p.plane(0).size();
        uint64_t sum = 0;
        for(size_t i = 0; i < total; i++) sum += data[i];
        return sum;
}

static bool fontAvailable() {
        auto img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        BasicFont probe(img->createPaintEngine());
        probe.setFontSize(12);
        return probe.measureText("A") > 0;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("BasicFont: construction with default PaintEngine") {
        BasicFont bf{PaintEngine()};
        CHECK(bf.fontFilename().isEmpty());
        CHECK(bf.fontSize() == 12);
        CHECK_FALSE(bf.kerningEnabled());
        CHECK_FALSE(bf.isValid());
        if(fontAvailable()) {
                CHECK(bf.lineHeight() > 0);
                CHECK(bf.ascender() > 0);
                CHECK(bf.descender() > 0);
        }
}

TEST_CASE("BasicFont: construction with real PaintEngine") {
        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        CHECK(bf.fontFilename().isEmpty());
        CHECK(bf.isValid());
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
// Default font (empty filename → library's bundled default)
// ============================================================================

TEST_CASE("BasicFont: drawText works without explicit filename") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);
        CHECK(bf.drawText("default", 10, 40));

        CHECK(planeSum(*img) > 0);
}

TEST_CASE("BasicFont: measureText works without explicit filename") {
        if(!fontAvailable()) return;

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(24);
        CHECK(bf.measureText("A") > 0);
}

// ============================================================================
// Bad font path
// ============================================================================

TEST_CASE("BasicFont: drawText fails with bad font path") {
        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontFilename("/nonexistent/font.ttf");
        CHECK_FALSE(bf.drawText("test", 0, 0));
}

// ============================================================================
// Font loading and metrics
// ============================================================================

TEST_CASE("BasicFont: font metrics after loading") {
        if(!fontAvailable()) return;

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
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

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(24);

        CHECK(bf.measureText("") == 0);
}

TEST_CASE("BasicFont: measureText longer string is wider") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
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

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("Hello", 10, 40);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

TEST_CASE("BasicFont: drawText uses foreground color") {
        if(!fontAvailable()) return;

        auto renderAndSum = [](Color fg) -> uint64_t {
                auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
                BasicFont bf(img->createPaintEngine());
                bf.setFontSize(24);
                bf.setForegroundColor(fg);
                bf.drawText("ABC", 10, 40);
                return planeSum(*img);
        };

        uint64_t sumWhite = renderAndSum(Color::White);
        uint64_t sumRed = renderAndSum(Color::Red);
        CHECK(sumWhite > 0);
        CHECK(sumRed > 0);
        CHECK(sumWhite != sumRed);
}

TEST_CASE("BasicFont: drawText on RGBA8") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGBA8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

// ============================================================================
// Font property changes without recreation
// ============================================================================

TEST_CASE("BasicFont: changing font size works") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
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

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
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

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);

        BasicFont bf{PaintEngine()};
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);
        bf.setPaintEngine(img->createPaintEngine());

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

TEST_CASE("BasicFont: deferred PaintEngine on RGBA8") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGBA8_sRGB);

        BasicFont bf{PaintEngine()};
        bf.setFontSize(24);
        bf.setForegroundColor(Color::White);
        bf.setPaintEngine(img->createPaintEngine());

        bool ok = bf.drawText("Test", 10, 40);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

// ============================================================================
// Drawing at bottom of large images
// ============================================================================

TEST_CASE("BasicFont: drawText at bottom of large RGBA8 image") {
        if(!fontAvailable()) return;

        auto img = makePayload(1920, 1080, PixelFormat::RGBA8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(48);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("01:00:00:00", 795, 1040);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

TEST_CASE("BasicFont: drawText at bottom of large RGB8 image") {
        if(!fontAvailable()) return;

        auto img = makePayload(1920, 1080, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(48);
        bf.setForegroundColor(Color::White);

        bool ok = bf.drawText("01:00:00:00", 795, 1040);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

// ============================================================================
// Font metrics consistency
// ============================================================================

TEST_CASE("BasicFont: lineHeight equals ascender plus descender") {
        if(!fontAvailable()) return;

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        BasicFont bf(img->createPaintEngine());
        bf.setFontSize(48);

        bf.measureText("A");

        CHECK(bf.lineHeight() == bf.ascender() + bf.descender());
}

TEST_CASE("BasicFont: metrics match FastFont for same font and size") {
        if(!fontAvailable()) return;

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        FastFont ff(pe);
        ff.setFontSize(48);
        ff.measureText("A");

        BasicFont bf(pe);
        bf.setFontSize(48);
        bf.measureText("A");

        CHECK(ff.lineHeight() == bf.lineHeight());
        CHECK(ff.ascender() == bf.ascender());
        CHECK(ff.descender() == bf.descender());
}

TEST_CASE("BasicFont: measureText matches FastFont") {
        if(!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        FastFont ff(pe);
        ff.setFontSize(24);

        BasicFont bf(pe);
        bf.setFontSize(24);

        CHECK(ff.measureText("01:00:00:00") == bf.measureText("01:00:00:00"));
}
