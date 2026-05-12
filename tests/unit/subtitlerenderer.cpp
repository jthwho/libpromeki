/**
 * @file      tests/subtitlerenderer.cpp
 * @copyright Howard Logic.  All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <chrono>
#include <cstring>
#include <doctest/doctest.h>
#include <promeki/color.h>
#include <promeki/enums.h>
#include <promeki/imagedesc.h>
#include <promeki/list.h>
#include <promeki/pixelformat.h>
#include <promeki/rect.h>
#include <promeki/string.h>
#include <promeki/subtitle.h>
#include <promeki/subtitlerenderer.h>
#include <promeki/timestamp.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        TimeStamp tsFromMs(int64_t ms) {
                using ClockDur = TimeStamp::Value::duration;
                return TimeStamp(TimeStamp::Value(std::chrono::duration_cast<ClockDur>(std::chrono::milliseconds(ms))));
        }

        UncompressedVideoPayload::Ptr makePayload(size_t w, size_t h, uint8_t fill = 0) {
                auto p = UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(PixelFormat::RGB8_sRGB)));
                if (!p.isValid()) return p;
                for (size_t i = 0; i < p->planeCount(); ++i) {
                        std::memset(p.modify()->data()[i].data(), fill, p->plane(i).size());
                }
                return p;
        }

        uint64_t planeSum(const UncompressedVideoPayload &p) {
                const uint8_t *data = p.plane(0).data();
                size_t         total = p.plane(0).size();
                uint64_t       sum = 0;
                for (size_t i = 0; i < total; ++i) sum += data[i];
                return sum;
        }

        /// @brief @c true when any pixel in @p p comes within @p tol on
        ///        every channel of the requested @c (r, g, b) target.
        ///        Antialiased glyph edges rarely hit exact channel
        ///        values, so the tests use a generous tolerance to
        ///        confirm the right colour landed on the frame.
        bool hasColouredPixel(const UncompressedVideoPayload &p, uint8_t r, uint8_t g, uint8_t b, int tol = 32) {
                const uint8_t *data = p.plane(0).data();
                const size_t   stride = p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
                for (size_t y = 0; y < p.desc().height(); ++y) {
                        const uint8_t *row = data + y * stride;
                        for (size_t x = 0; x < p.desc().width(); ++x) {
                                int dr = int(row[x * 3 + 0]) - int(r);
                                int dg = int(row[x * 3 + 1]) - int(g);
                                int db = int(row[x * 3 + 2]) - int(b);
                                if (std::abs(dr) <= tol && std::abs(dg) <= tol && std::abs(db) <= tol) return true;
                        }
                }
                return false;
        }

        /// @brief @c true when any pixel in rows [y0, y1) comes within
        ///        @p tol on every channel of @c (r, g, b).
        bool hasColouredPixelInRows(const UncompressedVideoPayload &p, size_t y0, size_t y1, uint8_t r,
                                    uint8_t g, uint8_t b, int tol = 32) {
                if (y1 > p.desc().height()) y1 = p.desc().height();
                const uint8_t *data = p.plane(0).data();
                const size_t   stride = p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
                for (size_t y = y0; y < y1; ++y) {
                        const uint8_t *row = data + y * stride;
                        for (size_t x = 0; x < p.desc().width(); ++x) {
                                int dr = int(row[x * 3 + 0]) - int(r);
                                int dg = int(row[x * 3 + 1]) - int(g);
                                int db = int(row[x * 3 + 2]) - int(b);
                                if (std::abs(dr) <= tol && std::abs(dg) <= tol && std::abs(db) <= tol) return true;
                        }
                }
                return false;
        }

} // namespace

TEST_CASE("SubtitleRenderer: render on an invalid payload reports InvalidArgument") {
        SubtitleRenderer rr;
        UncompressedVideoPayload bad;
        Subtitle s(tsFromMs(0), tsFromMs(1000), "hi");
        Error    err = rr.render(s, bad);
        CHECK(err.code() == Error::InvalidArgument);
}

TEST_CASE("SubtitleRenderer: empty cue paints nothing") {
        auto img = makePayload(320, 180);
        REQUIRE(img.isValid());
        const uint64_t before = planeSum(*img);

        SubtitleRenderer rr;
        Subtitle         empty;
        CHECK(rr.render(empty, *img.modify()).isOk());
        CHECK(planeSum(*img) == before);
}

TEST_CASE("SubtitleRenderer: plain cue paints text on the frame") {
        auto img = makePayload(640, 360);
        REQUIRE(img.isValid());

        SubtitleRenderer rr;
        rr.setFontSize(36);
        rr.setDefaultForeground(Color::White);
        rr.setDefaultBackground(Color::Black);
        rr.setDrawBackground(true);

        Subtitle s(tsFromMs(0), tsFromMs(1000), "Hello");
        REQUIRE(rr.render(s, *img.modify()).isOk());
        // At least one pixel must have changed.
        CHECK(planeSum(*img) > 0);
        // The cue's background was opaque black; check at least one
        // white pixel landed on the frame (the text foreground).
        CHECK(hasColouredPixel(*img, 255, 255, 255));
}

TEST_CASE("SubtitleRenderer: per-span colour override paints the requested colour") {
        auto img = makePayload(640, 360);
        REQUIRE(img.isValid());

        SubtitleRenderer rr;
        rr.setFontSize(36);
        rr.setDefaultForeground(Color::White);
        rr.setDefaultBackground(Color::Black);
        rr.setDrawBackground(true);

        SubtitleSpan::List spans;
        spans.pushToBack(SubtitleSpan("RED", false, false, false, Color::Red));
        Subtitle s(tsFromMs(0), tsFromMs(1000), spans, SubtitleAnchor::Default, Rect2Di32(), String(), Metadata());

        REQUIRE(rr.render(s, *img.modify()).isOk());
        // The span's explicit colour overrides the renderer default;
        // there must be at least one near-red pixel.
        CHECK(hasColouredPixel(*img, 255, 0, 0));
        // White (renderer default) must not appear for this single
        // all-red span.  Tight tolerance so we don't false-positive
        // on bright-red antialias edges.
        CHECK_FALSE(hasColouredPixel(*img, 255, 255, 255, 8));
}

TEST_CASE("SubtitleRenderer: cue's own anchor is honoured when no override is set") {
        auto img = makePayload(640, 360);
        REQUIRE(img.isValid());

        SubtitleRenderer rr;
        rr.setFontSize(36);
        rr.setDefaultForeground(Color::White);
        rr.setDefaultBackground(Color::Black);
        rr.setDrawBackground(false);
        // No anchor override — renderer should fall through to
        // the cue's own anchor.
        rr.setAnchorOverride(SubtitleAnchor::Default);

        Subtitle s(tsFromMs(0), tsFromMs(1000), "TOP", SubtitleAnchor::TopCenter);
        REQUIRE(rr.render(s, *img.modify()).isOk());

        // Top quarter has the text; bottom quarter does not.
        CHECK(hasColouredPixelInRows(*img, 0, img->desc().height() / 3, 255, 255, 255));
        CHECK_FALSE(
                hasColouredPixelInRows(*img, 2 * img->desc().height() / 3, img->desc().height(), 255, 255, 255));
}

TEST_CASE("SubtitleRenderer: anchor override pushes content to the top of the frame") {
        auto img = makePayload(640, 360);
        REQUIRE(img.isValid());

        SubtitleRenderer rr;
        rr.setFontSize(36);
        rr.setDefaultForeground(Color::White);
        rr.setDefaultBackground(Color::Black);
        rr.setDrawBackground(false);
        rr.setAnchorOverride(SubtitleAnchor::TopCenter);

        Subtitle s(tsFromMs(0), tsFromMs(1000), "TOP");
        REQUIRE(rr.render(s, *img.modify()).isOk());

        // Scan the top quarter and bottom quarter for near-white text;
        // we expect the top to have some and the bottom not.
        CHECK(hasColouredPixelInRows(*img, 0, img->desc().height() / 3, 255, 255, 255));
        CHECK_FALSE(hasColouredPixelInRows(*img, 2 * img->desc().height() / 3, img->desc().height(), 255, 255, 255));
}

TEST_CASE("SubtitleRenderer: multi-line cue stacks rows vertically") {
        auto img = makePayload(640, 360);
        REQUIRE(img.isValid());

        SubtitleRenderer rr;
        rr.setFontSize(36);
        rr.setDefaultForeground(Color::White);
        rr.setDefaultBackground(Color::Black);
        rr.setDrawBackground(true);

        Subtitle s(tsFromMs(0), tsFromMs(1000), "line one\nline two");
        REQUIRE(rr.render(s, *img.modify()).isOk());

        // Two lines plus the inter-line gap must span well more than
        // a single line height — count the y-rows that carry any
        // bright pixel and check the spread.
        const uint8_t *data = img->plane(0).data();
        const size_t   stride = img->desc().pixelFormat().memLayout().lineStride(0, img->desc().width());
        ::promeki::List<int> rowsWithText;
        for (size_t y = 0; y < img->desc().height(); ++y) {
                const uint8_t *row = data + y * stride;
                bool           hot = false;
                for (size_t x = 0; x < img->desc().width(); ++x) {
                        // Any "bright" pixel near full white counts.
                        if (row[x * 3 + 0] > 200 && row[x * 3 + 1] > 200 && row[x * 3 + 2] > 200) {
                                hot = true;
                                break;
                        }
                }
                if (hot) rowsWithText.pushToBack(static_cast<int>(y));
        }
        REQUIRE(rowsWithText.size() >= 2);
        const int firstY = rowsWithText[0];
        const int lastY = rowsWithText[rowsWithText.size() - 1];
        // Two lines plus inter-line gap must span at least the renderer's
        // line-height — which is comfortably > 8 for any usable font size.
        CHECK(lastY - firstY > 8);
}
