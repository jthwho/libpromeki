/**
 * @file      paintengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/paintengine.h>
#include <promeki/pixelformat.h>
#include <promeki/color.h>
#include <promeki/uncompressedvideopayload.h>

using namespace promeki;

namespace {

        UncompressedVideoPayload::Ptr makePayload(size_t w, size_t h, PixelFormat::ID id) {
                return UncompressedVideoPayload::allocate(ImageDesc(w, h, PixelFormat(id)));
        }

        size_t stride0(const UncompressedVideoPayload &p) {
                return p.desc().pixelFormat().memLayout().lineStride(0, p.desc().width());
        }

} // namespace

TEST_CASE("PaintEngine: default construction") {
        PaintEngine pe;
        CHECK_FALSE(pe.pixelFormat().isValid());
}

TEST_CASE("PaintEngine: plotLine horizontal line") {
        auto pts = PaintEngine::plotLine(0, 0, 5, 0);
        CHECK(pts.size() == 5);
        for (size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == (int)i);
                CHECK(pts[i].y() == 0);
        }
}

TEST_CASE("PaintEngine: plotLine vertical line") {
        auto pts = PaintEngine::plotLine(0, 0, 0, 5);
        CHECK(pts.size() == 5);
        for (size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == 0);
                CHECK(pts[i].y() == (int)i);
        }
}

TEST_CASE("PaintEngine: plotLine diagonal line") {
        auto pts = PaintEngine::plotLine(0, 0, 5, 5);
        CHECK(pts.size() == 5);
        for (size_t i = 0; i < pts.size(); i++) {
                CHECK(pts[i].x() == (int)i);
                CHECK(pts[i].y() == (int)i);
        }
}

TEST_CASE("PaintEngine: plotLine single point returns empty") {
        auto pts = PaintEngine::plotLine(3, 7, 3, 7);
        CHECK(pts.size() == 0);
}

TEST_CASE("PaintEngine: plotLine negative direction horizontal") {
        auto pts = PaintEngine::plotLine(5, 0, 0, 0);
        CHECK(pts.size() == 5);
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
// Shape drawing tests
// ============================================================================

TEST_CASE("PaintEngine: fillRect") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 20, 15));
        CHECK(drawn == 20 * 15);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *p = buf + stride * 15 + 15 * 3;
        CHECK(p[0] == 255);
        CHECK(p[1] == 255);
        CHECK(p[2] == 255);

        const uint8_t *outside = buf + stride * 5 + 5 * 3;
        CHECK(outside[0] == 0);
        CHECK(outside[1] == 0);
        CHECK(outside[2] == 0);
}

TEST_CASE("PaintEngine: drawRect") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   red = pe.createPixel(65535, 0, 0);
        size_t drawn = pe.drawRect(red, Rect<int32_t>(10, 10, 20, 15));
        CHECK(drawn > 0);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *edge = buf + stride * 10 + 15 * 3;
        CHECK(edge[0] == 255);
        CHECK(edge[1] == 0);
        CHECK(edge[2] == 0);

        const uint8_t *interior = buf + stride * 17 + 20 * 3;
        CHECK(interior[0] == 0);
}

TEST_CASE("PaintEngine: fillCircle") {
        auto        img = makePayload(128, 128, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   green = pe.createPixel(0, 65535, 0);
        size_t drawn = pe.fillCircle(green, Point2Di32(64, 64), 20);
        CHECK(drawn > 0);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
        CHECK(center[1] == 255);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: drawCircle") {
        auto        img = makePayload(128, 128, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   blue = pe.createPixel(0, 0, 65535);
        size_t drawn = pe.drawCircle(blue, Point2Di32(64, 64), 30);
        CHECK(drawn > 0);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
        CHECK(center[1] == 0);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: fillEllipse") {
        auto        img = makePayload(128, 128, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   yellow = pe.createPixel(65535, 65535, 0);
        size_t drawn = pe.fillEllipse(yellow, Point2Di32(64, 64), Size2Du32(30, 15));
        CHECK(drawn > 0);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 255);
        CHECK(center[1] == 255);
        CHECK(center[2] == 0);
}

TEST_CASE("PaintEngine: drawEllipse") {
        auto        img = makePayload(128, 128, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        black = pe.createPixel(0, 0, 0);
        pe.fill(black);

        auto   white = pe.createPixel(65535, 65535, 65535);
        size_t drawn = pe.drawEllipse(white, Point2Di32(64, 64), Size2Du32(30, 15));
        CHECK(drawn > 0);

        const uint8_t *buf = img->plane(0).data();
        size_t         stride = stride0(*img);
        const uint8_t *center = buf + stride * 64 + 64 * 3;
        CHECK(center[0] == 0);
}

// ============================================================================
// Edge cases: zero dimensions
// ============================================================================

TEST_CASE("PaintEngine: fillRect zero width") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 0, 10));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: fillRect zero height") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 10, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect zero width") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 0, 10));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect zero height") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 10, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawCircle zero radius") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawCircle(white, Point2Di32(32, 32), 0);
        CHECK(drawn >= 0);
}

TEST_CASE("PaintEngine: fillCircle zero radius") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.fillCircle(white, Point2Di32(32, 32), 0);
        CHECK(drawn >= 0);
}

TEST_CASE("PaintEngine: drawEllipse zero radii") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawEllipse(white, Point2Di32(32, 32), Size2Du32(0, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: fillEllipse zero radii") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.fillEllipse(white, Point2Di32(32, 32), Size2Du32(0, 0));
        CHECK(drawn == 0);
}

TEST_CASE("PaintEngine: drawRect exact point count") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawRect(white, Rect<int32_t>(5, 5, 10, 10));
        CHECK(drawn == 36);
}

TEST_CASE("PaintEngine: fillRect 1x1") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.fillRect(white, Rect<int32_t>(10, 10, 1, 1));
        CHECK(drawn == 1);
}

TEST_CASE("PaintEngine: drawRect 1x1") {
        auto        img = makePayload(64, 64, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();
        auto        white = pe.createPixel(65535, 65535, 65535);
        size_t      drawn = pe.drawRect(white, Rect<int32_t>(10, 10, 1, 1));
        CHECK(drawn > 0);
}

TEST_CASE("PaintEngine: createPixel scales uint16 to uint8") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto white = pe.createPixel(65535, 65535, 65535);
        pe.fill(white);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);

        auto mid = pe.createPixel(32768, 32768, 32768);
        pe.fill(mid);
        CHECK(buf[0] == 128);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 128);

        auto level75 = pe.createPixel(49151, 49151, 49151);
        pe.fill(level75);
        CHECK(buf[0] == 191);
        CHECK(buf[1] == 191);
        CHECK(buf[2] == 191);

        auto black = pe.createPixel(0, 0, 0);
        pe.fill(black);
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

TEST_CASE("PaintEngine: createPixel scales uint16 to uint8 RGBA") {
        auto        img = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(65535, 0, 32768);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 128);
        CHECK(buf[3] == 255);
}

TEST_CASE("PaintEngine: createPixel RGBA8 with explicit alpha") {
        auto        img = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(65535, 32768, 0, 32768);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 0);
        CHECK(buf[3] == 128);

        auto transparent = pe.createPixel(65535, 65535, 65535, 0);
        pe.fill(transparent);
        CHECK(buf[0] == 255);
        CHECK(buf[3] == 0);
}

TEST_CASE("PaintEngine: createPixel monochrome (1-component) RGB8") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto gray = pe.createPixel(32768);
        pe.fill(gray);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 128);
        CHECK(buf[1] == 128);
        CHECK(buf[2] == 128);

        auto white = pe.createPixel(65535);
        pe.fill(white);
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);

        auto black = pe.createPixel((uint16_t)0);
        pe.fill(black);
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

TEST_CASE("PaintEngine: createPixel monochrome (1-component) RGBA8") {
        auto        img = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto gray = pe.createPixel(65535);
        pe.fill(gray);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);
        CHECK(buf[3] == 0xFF);
}

TEST_CASE("PaintEngine: createPixel 2-component returns empty") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(65535, 32768);
        CHECK(pixel.isEmpty());
}

// ============================================================================
// createPixel(Color) tests
// ============================================================================

TEST_CASE("PaintEngine: createPixel from Color RGB8") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(Color::White);
        CHECK(!pixel.isEmpty());

        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 255);
}

TEST_CASE("PaintEngine: createPixel from Color red") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(Color::Red);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 255);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

TEST_CASE("PaintEngine: createPixel from Color mid-values") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        Color c(128, 64, 32);
        auto  pixel = pe.createPixel(c);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 128);
        CHECK(buf[1] == 64);
        CHECK(buf[2] == 32);
}

TEST_CASE("PaintEngine: createPixel from Color RGBA8") {
        auto        img = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        Color c(200, 100, 50, 128);
        auto  pixel = pe.createPixel(c);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 200);
        CHECK(buf[1] == 100);
        CHECK(buf[2] == 50);
        CHECK(buf[3] == 128);
}

TEST_CASE("PaintEngine: createPixel from Color black") {
        auto        img = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine pe = img->createPaintEngine();

        auto pixel = pe.createPixel(Color::Black);
        pe.fill(pixel);
        const uint8_t *buf = img->plane(0).data();
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 0);
        CHECK(buf[2] == 0);
}

// ============================================================================
// blit() tests
// ============================================================================

TEST_CASE("PaintEngine: blit full image") {
        auto        src = makePayload(8, 8, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(4, 4), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 6 + 6 * 3;
        CHECK(p[0] == 255);
        CHECK(p[1] == 0);
        CHECK(p[2] == 0);

        const uint8_t *outside = buf + stride * 0 + 0 * 3;
        CHECK(outside[0] == 0);
        CHECK(outside[1] == 0);
        CHECK(outside[2] == 0);
}

TEST_CASE("PaintEngine: blit clipped at right/bottom edge") {
        auto        src = makePayload(8, 8, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Green));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(12, 12), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 13 + 13 * 3;
        CHECK(p[0] == 0);
        CHECK(p[1] == 255);
        CHECK(p[2] == 0);

        const uint8_t *corner = buf + stride * 15 + 15 * 3;
        CHECK(corner[0] == 0);
        CHECK(corner[1] == 255);
        CHECK(corner[2] == 0);
}

TEST_CASE("PaintEngine: blit completely outside returns true") {
        auto        src = makePayload(8, 8, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(100, 100), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         total = dst->plane(0).size();
        uint64_t       sum = 0;
        for (size_t i = 0; i < total; i++) sum += buf[i];
        CHECK(sum == 0);
}

TEST_CASE("PaintEngine: blit with negative dest clips left/top") {
        auto        src = makePayload(8, 8, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Blue));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(-4, -4), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf;
        CHECK(p[0] == 0);
        CHECK(p[1] == 0);
        CHECK(p[2] == 255);

        const uint8_t *p2 = buf + stride * 3 + 3 * 3;
        CHECK(p2[0] == 0);
        CHECK(p2[1] == 0);
        CHECK(p2[2] == 255);

        const uint8_t *p3 = buf + stride * 0 + 4 * 3;
        CHECK(p3[0] == 0);
        CHECK(p3[1] == 0);
        CHECK(p3[2] == 0);
}

TEST_CASE("PaintEngine: blit sub-region of source") {
        auto        src = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));
        srcPe.fillRect(srcPe.createPixel(Color::Green), Rect<int32_t>(4, 4, 4, 4));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(0, 0), *src, Point2Di32(4, 4), Size2Du32(4, 4));
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        CHECK(buf[0] == 0);
        CHECK(buf[1] == 255);
        CHECK(buf[2] == 0);

        CHECK(buf[4 * 3] == 0);
        CHECK(buf[4 * 3 + 1] == 0);
        CHECK(buf[4 * 3 + 2] == 0);
}

TEST_CASE("PaintEngine: blit mismatched pixel format returns false") {
        auto        src = makePayload(8, 8, PixelFormat::RGBA8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(16, 16, PixelFormat::RGB8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(0, 0), *src);
        CHECK_FALSE(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         total = dst->plane(0).size();
        uint64_t       sum = 0;
        for (size_t i = 0; i < total; i++) sum += buf[i];
        CHECK(sum == 0);
}

TEST_CASE("PaintEngine: blit mismatched RGBA dst from RGB src returns false") {
        auto        src = makePayload(8, 8, PixelFormat::RGB8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(0, 0), *src);
        CHECK_FALSE(ok);
}

// ============================================================================
// RGBA8 blit tests (regression: clipping at high Y coordinates)
// ============================================================================

TEST_CASE("PaintEngine: RGBA8 blit full image") {
        auto        src = makePayload(8, 8, PixelFormat::RGBA8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(4, 4), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 6 + 6 * 4;
        CHECK(p[0] == 255);
        CHECK(p[1] == 0);
        CHECK(p[2] == 0);
        CHECK(p[3] == 255);

        const uint8_t *outside = buf + stride * 0 + 0 * 4;
        CHECK(outside[0] == 0);
        CHECK(outside[1] == 0);
        CHECK(outside[2] == 0);
}

TEST_CASE("PaintEngine: RGBA8 blit at high Y coordinate") {
        auto        src = makePayload(4, 4, PixelFormat::RGBA8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Red));

        auto        dst = makePayload(32, 1080, PixelFormat::RGBA8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(0, 1000), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 1001 + 1 * 4;
        CHECK(p[0] == 255);
        CHECK(p[1] == 0);
        CHECK(p[2] == 0);
}

TEST_CASE("PaintEngine: RGBA8 blit at lower half of large image") {
        auto        src = makePayload(10, 60, PixelFormat::RGBA8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Green));

        auto        dst = makePayload(1920, 1080, PixelFormat::RGBA8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(795, 998), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 1020 + 800 * 4;
        CHECK(p[0] == 0);
        CHECK(p[1] == 255);
        CHECK(p[2] == 0);

        const uint8_t *outside = buf + stride * 500 + 500 * 4;
        CHECK(outside[0] == 0);
        CHECK(outside[1] == 0);
        CHECK(outside[2] == 0);
}

TEST_CASE("PaintEngine: RGBA8 blit clipped at right/bottom edge") {
        auto        src = makePayload(8, 8, PixelFormat::RGBA8_sRGB);
        PaintEngine srcPe = src->createPaintEngine();
        srcPe.fill(srcPe.createPixel(Color::Blue));

        auto        dst = makePayload(16, 16, PixelFormat::RGBA8_sRGB);
        PaintEngine dstPe = dst->createPaintEngine();
        dstPe.fill(dstPe.createPixel(Color::Black));

        bool ok = dstPe.blit(Point2Di32(12, 12), *src);
        CHECK(ok);

        const uint8_t *buf = dst->plane(0).data();
        size_t         stride = stride0(*dst);

        const uint8_t *p = buf + stride * 13 + 13 * 4;
        CHECK(p[0] == 0);
        CHECK(p[1] == 0);
        CHECK(p[2] == 255);

        const uint8_t *corner = buf + stride * 15 + 15 * 4;
        CHECK(corner[0] == 0);
        CHECK(corner[1] == 0);
        CHECK(corner[2] == 255);
}
