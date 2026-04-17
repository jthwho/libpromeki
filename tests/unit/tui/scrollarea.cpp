/**
 * @file      scrollarea.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/scrollarea.h>

using namespace promeki;

TEST_CASE("TuiScrollArea: default construction") {
        TuiScrollArea area;
        CHECK(area.contentWidget() == nullptr);
        CHECK(area.scrollX() == 0);
        CHECK(area.scrollY() == 0);
}

TEST_CASE("TuiScrollArea: setContentWidget") {
        TuiScrollArea area;
        TuiWidget *content = new TuiWidget(&area);
        area.setContentWidget(content);
        CHECK(area.contentWidget() == content);
}

TEST_CASE("TuiScrollArea: scroll position") {
        TuiScrollArea area;
        area.setGeometry(Rect2Di32(0, 0, 40, 20));

        TuiWidget *content = new TuiWidget(&area);
        content->setGeometry(Rect2Di32(0, 0, 100, 100));
        area.setContentWidget(content);

        area.setScrollX(10);
        CHECK(area.scrollX() == 10);

        area.setScrollY(20);
        CHECK(area.scrollY() == 20);
}

TEST_CASE("TuiScrollArea: scrollTo") {
        TuiScrollArea area;
        area.setGeometry(Rect2Di32(0, 0, 40, 20));

        TuiWidget *content = new TuiWidget(&area);
        content->setGeometry(Rect2Di32(0, 0, 100, 100));
        area.setContentWidget(content);

        area.scrollTo(15, 25);
        CHECK(area.scrollX() == 15);
        CHECK(area.scrollY() == 25);
}

TEST_CASE("TuiScrollArea: sizeHint") {
        TuiScrollArea area;
        Size2Di32 hint = area.sizeHint();
        CHECK(hint.width() >= 0);
        CHECK(hint.height() >= 0);
}
