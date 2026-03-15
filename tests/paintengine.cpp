/**
 * @file      paintengine.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/proav/paintengine.h>

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
