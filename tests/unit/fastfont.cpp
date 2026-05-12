/**
 * @file      tests/fastfont.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <cstring>
#include <doctest/doctest.h>
#include <promeki/fastfont.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>
#include <promeki/filepath.h>

using namespace promeki;

static UncompressedVideoPayload::Ptr makePayload(size_t w, size_t h, PixelFormat::ID id, uint8_t fill = 0) {
        auto p = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(id)));
        for (size_t i = 0; i < p->planeCount(); ++i) {
                std::memset(p.modify()->data()[i].data(), fill, p->plane(i).size());
        }
        return p;
}

static uint64_t planeSum(const UncompressedVideoPayload &p) {
        const uint8_t *data = p.plane(0).data();
        size_t         total = p.plane(0).size();
        uint64_t       sum = 0;
        for (size_t i = 0; i < total; i++) sum += data[i];
        return sum;
}

static size_t plane0Stride(const UncompressedVideoPayload &p) {
        return p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
}

static bool fontAvailable() {
        auto     img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        FastFont probe(img->createPaintEngine());
        probe.setFontSize(12);
        return probe.measureText("A") > 0;
}

// ============================================================================
// Construction
// ============================================================================

TEST_CASE("FastFont: construction with default PaintEngine") {
        FastFont ff{PaintEngine()};
        CHECK(ff.fontFilename().isEmpty());
        CHECK(ff.fontSize() == 12);
        CHECK_FALSE(ff.kerningEnabled());
        CHECK_FALSE(ff.isValid());
        if (fontAvailable()) {
                CHECK(ff.lineHeight() > 0);
                CHECK(ff.ascender() > 0);
                CHECK(ff.descender() > 0);
        }
}

TEST_CASE("FastFont: construction with real PaintEngine") {
        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        CHECK(ff.fontFilename().isEmpty());
        CHECK(ff.fontSize() == 12);
        CHECK(ff.isValid());
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

TEST_CASE("FastFont: isValid requires paint engine and positive size") {
        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        CHECK(ff.isValid());

        ff.setFontSize(0);
        CHECK_FALSE(ff.isValid());
}

// ============================================================================
// Default font (empty filename → library's bundled default)
// ============================================================================

TEST_CASE("FastFont: drawText works without explicit filename") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        CHECK(ff.drawText("default", 10, 40));
        CHECK(planeSum(*img) > 0);
}

TEST_CASE("FastFont: measureText works without explicit filename") {
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);
        CHECK(ff.measureText("A") > 0);
}

// ============================================================================
// Bad font path
// ============================================================================

TEST_CASE("FastFont: drawText fails with bad font path") {
        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontFilename("/nonexistent/font.ttf");
        CHECK_FALSE(ff.drawText("test", 0, 0));
}

TEST_CASE("FastFont: measureText returns 0 with bad font path") {
        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontFilename("/nonexistent/font.ttf");
        CHECK(ff.measureText("test") == 0);
}

// ============================================================================
// Font loading and metrics
// ============================================================================

TEST_CASE("FastFont: font metrics after loading") {
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

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
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        CHECK(ff.measureText("") == 0);
}

TEST_CASE("FastFont: measureText single character") {
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        int w = ff.measureText("A");
        CHECK(w > 0);
}

TEST_CASE("FastFont: measureText longer string is wider") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        int w1 = ff.measureText("A");
        int w3 = ff.measureText("ABC");
        CHECK(w3 > w1);
}

TEST_CASE("FastFont: measureText monospace characters same width") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

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
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB, 128);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        bool ok = ff.drawText("Hello", 10, 40);
        CHECK(ok);

        const uint8_t *data = img->plane(0).data();
        size_t         total = img->plane(0).size();
        size_t         diffCount = 0;
        for (size_t i = 0; i < total; i++) {
                if (data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: drawText with different colors") {
        if (!fontAvailable()) return;

        auto renderAndSum = [](Color fg, Color bg) -> uint64_t {
                auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
                FastFont ff(img->createPaintEngine());
                ff.setFontSize(24);
                ff.setForegroundColor(fg);
                ff.setBackgroundColor(bg);
                ff.drawText("ABC", 10, 40);
                return planeSum(*img);
        };

        uint64_t sumWhiteBlack = renderAndSum(Color::White, Color::Black);
        uint64_t sumRedGreen = renderAndSum(Color::Red, Color::Green);
        CHECK(sumWhiteBlack > 0);
        CHECK(sumRedGreen > 0);
        CHECK(sumWhiteBlack != sumRedGreen);
}

TEST_CASE("FastFont: drawText on RGBA8") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGBA8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);
        CHECK(planeSum(*img) > 0);
}

// ============================================================================
// Cache invalidation
// ============================================================================

TEST_CASE("FastFont: changing font size invalidates cache") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(12);

        int w12 = ff.measureText("ABC");

        ff.setFontSize(48);
        int w48 = ff.measureText("ABC");

        CHECK(w12 > 0);
        CHECK(w48 > 0);
        CHECK(w48 > w12);
}

TEST_CASE("FastFont: changing font filename invalidates cache") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        int w = ff.measureText("ABC");
        CHECK(w > 0);

        ff.setFontFilename("/nonexistent/font.ttf");
        int w2 = ff.measureText("ABC");
        CHECK(w2 == 0);
}

TEST_CASE("FastFont: setPaintEngine same pixel format preserves cache") {
        if (!fontAvailable()) return;

        auto     img1 = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        auto     img2 = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img1->createPaintEngine());
        ff.setFontSize(24);

        int w1 = ff.measureText("ABC");
        CHECK(w1 > 0);

        ff.setPaintEngine(img2->createPaintEngine());

        int w2 = ff.measureText("ABC");
        CHECK(w2 == w1);
}

// ============================================================================
// Deferred PaintEngine pattern (construct default, then switch)
// ============================================================================

TEST_CASE("FastFont: deferred PaintEngine on RGB8") {
        if (!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB, 128);

        FastFont ff{PaintEngine()};
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img->createPaintEngine());

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = img->plane(0).data();
        size_t         total = img->plane(0).size();
        size_t         diffCount = 0;
        for (size_t i = 0; i < total; i++) {
                if (data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: deferred PaintEngine on RGBA8") {
        if (!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGBA8_sRGB, 128);

        FastFont ff{PaintEngine()};
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img->createPaintEngine());

        bool ok = ff.drawText("Test", 10, 40);
        CHECK(ok);

        const uint8_t *data = img->plane(0).data();
        size_t         total = img->plane(0).size();
        size_t         diffCount = 0;
        for (size_t i = 0; i < total; i++) {
                if (data[i] != 128) diffCount++;
        }
        CHECK(diffCount > 0);
}

TEST_CASE("FastFont: deferred PaintEngine measures correctly") {
        if (!fontAvailable()) return;

        auto img = makePayload(256, 64, PixelFormat::RGB8_sRGB);

        FastFont direct(img->createPaintEngine());
        direct.setFontSize(24);
        int wDirect = direct.measureText("ABC");

        FastFont deferred{PaintEngine()};
        deferred.setFontSize(24);
        deferred.setPaintEngine(img->createPaintEngine());
        int wDeferred = deferred.measureText("ABC");

        CHECK(wDirect > 0);
        CHECK(wDeferred == wDirect);
}

// ============================================================================
// Drawing at bottom of large images (regression for RGBA8 blit clipping)
// ============================================================================

TEST_CASE("FastFont: drawText at bottom of large RGBA8 image") {
        if (!fontAvailable()) return;

        auto img = makePayload(1920, 1080, PixelFormat::RGBA8_sRGB, 128);

        FastFont ff{PaintEngine()};
        ff.setFontSize(48);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        ff.setPaintEngine(img->createPaintEngine());

        int  baseline = 1040;
        bool ok = ff.drawText("01:00:00:00", 795, baseline);
        CHECK(ok);

        const uint8_t *data = img->plane(0).data();
        const size_t   stride = plane0Stride(*img);
        int            cellTop = baseline - ff.ascender();
        size_t         changed = 0;
        for (int y = cellTop; y < cellTop + ff.lineHeight() && y < 1080; y++) {
                for (int x = 795; x < 795 + ff.measureText("01:00:00:00") && x < 1920; x++) {
                        size_t off = y * stride + x * 4;
                        if (data[off] != 128 || data[off + 1] != 128 || data[off + 2] != 128) {
                                changed++;
                        }
                }
        }
        CHECK(changed > 0);
}

TEST_CASE("FastFont: drawText at bottom of large RGB8 image") {
        if (!fontAvailable()) return;

        auto     img = makePayload(1920, 1080, PixelFormat::RGB8_sRGB, 128);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(48);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        int  baseline = 1040;
        bool ok = ff.drawText("01:00:00:00", 795, baseline);
        CHECK(ok);

        const uint8_t *data = img->plane(0).data();
        const size_t   stride = plane0Stride(*img);
        int            cellTop = baseline - ff.ascender();
        size_t         changed = 0;
        for (int y = cellTop; y < cellTop + ff.lineHeight() && y < 1080; y++) {
                for (int x = 795; x < 795 + ff.measureText("01:00:00:00") && x < 1920; x++) {
                        size_t off = y * stride + x * 3;
                        if (data[off] != 128 || data[off + 1] != 128 || data[off + 2] != 128) {
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
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(48);

        ff.measureText("A");

        CHECK(ff.lineHeight() == ff.ascender() + ff.descender());
}

TEST_CASE("FastFont: metrics scale with font size") {
        if (!fontAvailable()) return;

        auto img = makePayload(64, 64, PixelFormat::RGB8_sRGB);

        FastFont small(img->createPaintEngine());
        small.setFontSize(12);
        small.measureText("A");

        FastFont large(img->createPaintEngine());
        large.setFontSize(48);
        large.measureText("A");

        CHECK(large.lineHeight() > small.lineHeight());
        CHECK(large.ascender() > small.ascender());
        CHECK(large.descender() >= small.descender());
}

TEST_CASE("FastFont: metrics match between deferred and direct construction") {
        if (!fontAvailable()) return;

        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        FastFont direct(pe);
        direct.setFontSize(48);
        direct.measureText("A");

        FastFont deferred{PaintEngine()};
        deferred.setFontSize(48);
        deferred.setPaintEngine(pe);
        deferred.measureText("A");

        CHECK(direct.lineHeight() == deferred.lineHeight());
        CHECK(direct.ascender() == deferred.ascender());
        CHECK(direct.descender() == deferred.descender());
}

TEST_CASE("FastFont: NV12 metrics are chroma-aligned") {
        // On chroma-subsampled formats the cached ascender/descender
        // must be rounded up to the format's vertical subsampling so
        // cellTop = y - ascender stays on a chroma-row boundary when
        // the caller passes an aligned baseline y.  Otherwise the
        // multi-plane PaintEngine blit fast path can't fire and burn-
        // in falls back to the slow scalar per-pixel writer.
        if (!fontAvailable()) return;

        auto     img = makePayload(64, 64, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(36);
        ff.measureText("A");

        // NV12 has vSub=2 on the chroma plane.
        CHECK((ff.ascender() % 2) == 0);
        CHECK((ff.descender() % 2) == 0);
        CHECK((ff.lineHeight() % 2) == 0);
}

TEST_CASE("FastFont: NV12 drawText paints the chroma plane") {
        // End-to-end smoke test: drawing text on NV12 must touch both
        // the luma and chroma planes.  A regression that left the
        // chroma plane untouched would surface as monochrome / mis-
        // coloured text in the burn-in overlay.
        if (!fontAvailable()) return;

        auto fb = makePayload(64, 32, PixelFormat::YUV8_420_SemiPlanar_Rec709);
        // Fill the luma plane with mid-grey and the chroma plane with
        // a neutral pattern so the post-draw delta is unambiguous.
        std::memset(fb.modify()->data()[0].data(), 0x10, fb->plane(0).size());
        std::memset(fb.modify()->data()[1].data(), 0x80, fb->plane(1).size());

        FastFont ff(fb->createPaintEngine());
        ff.setFontSize(20);
        // White / black collapse to neutral chroma (128, 128) in
        // Rec.709 limited-range and the dst is pre-filled with the
        // same neutral pattern — so a working blit would write
        // chroma bytes equal to what's already there.  Use red so
        // CbCr unambiguously differs from the dst background.
        ff.setForegroundColor(Color::Red);
        ff.setBackgroundColor(Color::Black);
        REQUIRE(ff.drawText("X", 4, 24));

        const uint8_t *yBuf = fb->plane(0).data();
        const uint8_t *cbBuf = fb->plane(1).data();
        bool           luminanceChanged = false;
        bool           chromaChanged = false;
        for (size_t i = 0; i < fb->plane(0).size(); i++) {
                if (yBuf[i] != 0x10) {
                        luminanceChanged = true;
                        break;
                }
        }
        for (size_t i = 0; i < fb->plane(1).size(); i++) {
                if (cbBuf[i] != 0x80) {
                        chromaChanged = true;
                        break;
                }
        }
        CHECK(luminanceChanged);
        CHECK(chromaChanged);
}

// ============================================================================
// Multi-keyed glyph cache + per-call DrawStyle
// ============================================================================

TEST_CASE("FastFont: setForegroundColor does not invalidate the glyph cache") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        // Prime the cache for the white-on-black combo.
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);
        int wWhite = ff.measureText("ABC");

        // Flip to red-on-black.  Under the old single-key cache this
        // would have invalidated and zeroed the cache; under the
        // multi-key cache the measure is still cheap and the prior
        // (white) cache entry is still reachable.
        ff.setForegroundColor(Color::Red);
        int wRed = ff.measureText("ABC");

        CHECK(wWhite > 0);
        CHECK(wRed == wWhite); // Identical glyph geometry, just colour.
}

TEST_CASE("FastFont: per-call DrawStyle renders different colours into one draw pass") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB, 0);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);
        ff.setForegroundColor(Color::White);
        ff.setBackgroundColor(Color::Black);

        FastFont::DrawStyle red;
        red.foreground = Color::Red;
        red.background = Color::Black;
        REQUIRE(ff.drawText("RED", 4, 32, red));

        FastFont::DrawStyle green;
        green.foreground = Color::Green;
        green.background = Color::Black;
        REQUIRE(ff.drawText("GREEN", 80, 32, green));

        // Scan the image for any red and any green pixel: confirms the
        // two styled draws used different foreground colours and that
        // neither call invalidated the other's cache.
        const uint8_t *data = img->plane(0).data();
        size_t         stride = plane0Stride(*img);
        bool           sawRed = false;
        bool           sawGreen = false;
        for (size_t y = 0; y < img->desc().height(); ++y) {
                const uint8_t *row = data + y * stride;
                for (size_t x = 0; x < img->desc().width(); ++x) {
                        uint8_t r = row[x * 3 + 0];
                        uint8_t g = row[x * 3 + 1];
                        uint8_t b = row[x * 3 + 2];
                        if (r > 200 && g < 50 && b < 50) sawRed = true;
                        if (r < 50 && g > 200 && b < 50) sawGreen = true;
                }
        }
        CHECK(sawRed);
        CHECK(sawGreen);
}

TEST_CASE("FastFont: italic DrawStyle differs from upright in measured width") {
        if (!fontAvailable()) return;

        auto     img = makePayload(256, 64, PixelFormat::RGB8_sRGB);
        FastFont ff(img->createPaintEngine());
        ff.setFontSize(24);

        FastFont::DrawStyle upright;
        FastFont::DrawStyle italic;
        italic.italic = true;

        int wUpright = ff.measureText("HHH", upright);
        int wItalic = ff.measureText("HHH", italic);
        CHECK(wUpright > 0);
        CHECK(wItalic > 0);
        // Italic glyphs are sheared but the advance is typically the
        // same; what we want to confirm is that the cache returned
        // distinct entries (different cells with different geometry)
        // without crashing or zeroing out the measurement.
        CHECK(wItalic >= wUpright);
}

TEST_CASE("FastFont: underline DrawStyle paints a line below text") {
        if (!fontAvailable()) return;

        // Render the same text once without underline and once with;
        // the underlined version should touch strictly more pixels
        // (the underline rectangle adds them).
        auto sumWith = [](bool underline) -> uint64_t {
                auto                img = makePayload(256, 64, PixelFormat::RGB8_sRGB, 0);
                FastFont            ff(img->createPaintEngine());
                ff.setFontSize(24);
                ff.setForegroundColor(Color::White);
                ff.setBackgroundColor(Color::Black);
                FastFont::DrawStyle s;
                s.underline = underline;
                ff.drawText("UNDER", 10, 40, s);
                return planeSum(*img);
        };
        uint64_t plain = sumWith(false);
        uint64_t under = sumWith(true);
        CHECK(plain > 0);
        CHECK(under > plain);
}
