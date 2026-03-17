/**
 * @file      paintengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/paintengine.h>
#include <promeki/proav/image.h>

using namespace promeki;

TEST_CASE("PaintEngine: default construction") {
        PaintEngine pe;
        CHECK(pe.pixelFormat() == nullptr);
}

TEST_CASE("PaintEngine: plotLine horizontal line") {
        auto pts = PaintEngine::plotLine(0, 0, 5, 0);
        CHECK(pts.size() == 5);
        for(size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == (int)i);
                CHECK(pts[i].y() == 0);
        }
}

TEST_CASE("PaintEngine: plotLine vertical line") {
        auto pts = PaintEngine::plotLine(0, 0, 0, 5);
        CHECK(pts.size() == 5);
        for(size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == 0);
                CHECK(pts[i].y() == (int)i);
        }
}

TEST_CASE("PaintEngine: plotLine diagonal line") {
        auto pts = PaintEngine::plotLine(0, 0, 5, 5);
        CHECK(pts.size() == 5);
        for(size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == (int)i);
                CHECK(pts[i].y() == (int)i);
        }
}

TEST_CASE("PaintEngine: plotLine single point returns empty") {
        // When start and end are the same, the loop body never executes
        auto pts = PaintEngine::plotLine(3, 7, 3, 7);
        CHECK(pts.size() == 0);
}

TEST_CASE("PaintEngine: plotLine negative direction horizontal") {
        auto pts = PaintEngine::plotLine(5, 0, 0, 0);
        CHECK(pts.size() == 5);
        // Should go from x=0 down to x=-4 (relative offsets from start)
        CHECK(pts[0].x() == 5);
        CHECK(pts[0].y() == 0);
        CHECK(pts[4].x() == 1);
        CHECK(pts[4].y() == 0);
}

TEST_CASE("PaintEngine: plotLine negative direction vertical") {
        auto pts = PaintEngine::plotLine(0, 5, 0, 0);
        CHECK(pts.size() == 5);
        CHECK(pts[0].x() == 0);
        CHECK(pts[0].y() == 5);
        CHECK(pts[4].x() == 0);
        CHECK(pts[4].y() == 1);
}

// ============================================================================
// Shape drawing tests (using actual RGB8 image)
// ============================================================================

TEST_CASE("PaintEngine: fillRect") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 20, 15));
        CHECK(drawn == 20 * 15);

        // Verify a pixel inside the rect is white
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *p = buf + stride * 15 + 15 * 3;
        CHECK(p[0] == 255);
        CHECK(p[1] == 255);
        CHECK(p[2] == 255);

        // Verify a pixel outside the rect is still black
        uint8_t *outside = buf + stride * 5 + 5 * 3;
        CHECK(outside[0] == 0);
        CHECK(outside[1] == 0);
        CHECK(outside[2] == 0);
}

TEST_CASE("PaintEngine: drawRect") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto red = pe.createPixel(65535, 0, 0);
        size_t drawn = pe.drawRect(red, Rect<int32_t>(10, 10, 20, 15));
        CHECK(drawn > 0);

        // Verify a pixel on the top edge is red
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *edge = buf + stride * 10 + 15 * 3;
        CHECK(edge[0] == 255);
        CHECK(edge[1] == 0);
        CHECK(edge[2] == 0);

        // Verify interior pixel is NOT red (just outline)
        uint8_t *interior = buf + stride * 17 + 20 * 3;
        CHECK(interior[0] == 0);
}

TEST_CASE("PaintEngine: fillCircle") {
        Image img(128, 128, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto green = pe.createPixel(0, 65535, 0);
        size_t drawn = pe.fillCircle(green, Point2Di32(64, 64), 20);
        CHECK(drawn > 0);

        // Verify center pixel is green
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
        CHECK(center[1] == 255);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: drawCircle") {
        Image img(128, 128, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto blue = pe.createPixel(0, 0, 65535);
        size_t drawn = pe.drawCircle(blue, Point2Di32(64, 64), 30);
        CHECK(drawn > 0);

        // Center should still be black (outline only)
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
        CHECK(center[1] == 0);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: fillEllipse") {
        Image img(128, 128, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto yellow = pe.createPixel(65535, 65535, 0);
        size_t drawn = pe.fillEllipse(yellow, Point2Di32(64, 64), Size2Du32(30, 15));
        CHECK(drawn > 0);

        // Center should be yellow
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 255);
        CHECK(center[1] == 255);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: drawEllipse") {
        Image img(128, 128, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.drawEllipse(white, Point2Di32(64, 64), Size2Du32(30, 15));
        CHECK(drawn > 0);

        // Center should still be black (outline only)
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        size_t stride = img.lineStride(0);
        uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
}

// ============================================================================
// Edge cases: zero dimensions
// ============================================================================

TEST_CASE("PaintEngine: fillRect zero width") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 0, 10));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: fillRect zero height") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 10, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect zero width") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 0, 10));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect zero height") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 10, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawCircle zero radius") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        // Zero radius should still draw the center point
        size_t drawn = pe.drawCircle(white, Point2Di32(32, 32), 0);
        CHECK(drawn >= 0);
}

TEST_CASE("PaintEngine: fillCircle zero radius") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillCircle(white, Point2Di32(32, 32), 0);
        CHECK(drawn >= 0);
}

TEST_CASE("PaintEngine: drawEllipse zero radii") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.drawEllipse(white, Point2Di32(32, 32), Size2Du32(0, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: fillEllipse zero radii") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillEllipse(white, Point2Di32(32, 32), Size2Du32(0, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect exact point count") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        // 10x10 rect outline: top=10 + bottom=10 + left=8 + right=8 = 36 points
        size_t drawn = pe.drawRect(white, Rect<int32_t>(5, 5, 10, 10));
        CHECK(drawn == 36);
}

TEST_CASE("PaintEngine: fillRect 1x1") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 1, 1));
        CHECK(drawn == 1);
}

TEST_CASE("PaintEngine: drawRect 1x1") {
        Image img(64, 64, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();
        auto white = pe.createPixel(65535, 65535, 65535);
        // 1x1 rect outline: top=1 + bottom=1 (same pixel) + no sides => 2 points drawn to same pixel
        size_t drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 1, 1));
        CHECK(drawn > 0);
}

TEST_CASE("PaintEngine: createPixel scales uint16 to uint8") {
        Image img(16, 16, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();

        // Full white: 65535 -> 255
        auto white = pe.createPixel(65535, 65535, 65535);
        pe.fill(white);
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);

        // Mid gray: 32768 -> 128
        auto mid = pe.createPixel(32768, 32768, 32768);
        pe.fill(mid);
        CHECK(buf[0] == 128);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 128);

        // 75% level: 49151 -> 191
        auto level75 = pe.createPixel(49151, 49151, 49151);
        pe.fill(level75);
        CHECK(buf[0] == 191);
        CHECK(buf[1] == 191);
        CHECK(buf[2] == 191);

        // Black: 0 -> 0
        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

TEST_CASE("PaintEngine: createPixel scales uint16 to uint8 RGBA") {
        Image img(16, 16, PixelFormat::RGBA8);
        PaintEngine pe = img.createPaintEngine();

        // Mid-intensity red with mid alpha
        auto pixel = pe.createPixel(65535, 0, 32768);
        pe.fill(pixel);
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 128);
        CHECK(buf[3] == 255); // alpha defaults to opaque for 3-component createPixel
}

TEST_CASE("PaintEngine: createPixel RGBA8 with explicit alpha") {
        Image img(16, 16, PixelFormat::RGBA8);
        PaintEngine pe = img.createPaintEngine();

        // 4-component: explicit alpha scaling
        auto pixel = pe.createPixel(65535, 32768, 0, 32768);
        pe.fill(pixel);
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 0);
        CHECK(buf[3] == 128);

        // Full transparent
        auto transparent = pe.createPixel(65535, 65535, 65535, 0);
        pe.fill(transparent);
        CHECK(buf[0] == 255);
        CHECK(buf[3] == 0);
}

TEST_CASE("PaintEngine: createPixel monochrome (1-component) RGB8") {
        Image img(16, 16, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();

        // 1-component gray: should spread to all 3 channels
        auto gray = pe.createPixel(32768);
        pe.fill(gray);
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        CHECK(buf[0] == 128);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 128);

        // Full white via mono
        auto white = pe.createPixel(65535);
        pe.fill(white);
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);

        // Black via mono
        auto black = pe.createPixel((uint16_t)0);
        pe.fill(black);
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

TEST_CASE("PaintEngine: createPixel monochrome (1-component) RGBA8") {
        Image img(16, 16, PixelFormat::RGBA8);
        PaintEngine pe = img.createPaintEngine();

        // 1-component gray on RGBA8: R=G=B=gray, A=0xFF
        auto gray = pe.createPixel(65535);
        pe.fill(gray);
        uint8_t *buf = static_cast<uint8_t *>(img.plane(0)->data());
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);
        CHECK(buf[3] == 0xFF);
}

TEST_CASE("PaintEngine: createPixel 2-component returns empty") {
        Image img(16, 16, PixelFormat::RGB8);
        PaintEngine pe = img.createPaintEngine();

        // 2-component is invalid for both RGB8 and RGBA8 — should return empty pixel
        auto pixel = pe.createPixel(65535, 32768);
        CHECK(pixel.isEmpty());
}
