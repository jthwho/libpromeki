/**
 * @file      splitter.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/splitter.h>

using namespace promeki;

TEST_CASE("TuiSplitter: default construction") {
        TuiSplitter splitter;
        CHECK(splitter.orientation() == TuiSplitter::Horizontal);
        CHECK(splitter.splitRatio() == doctest::Approx(0.5));
        CHECK(splitter.firstWidget() == nullptr);
        CHECK(splitter.secondWidget() == nullptr);
}

TEST_CASE("TuiSplitter: construction with orientation") {
        TuiSplitter splitter(TuiSplitter::Vertical);
        CHECK(splitter.orientation() == TuiSplitter::Vertical);
}

TEST_CASE("TuiSplitter: setFirstWidget and setSecondWidget") {
        TuiSplitter splitter;
        TuiWidget *w1 = new TuiWidget(&splitter);
        TuiWidget *w2 = new TuiWidget(&splitter);

        splitter.setFirstWidget(w1);
        splitter.setSecondWidget(w2);
        CHECK(splitter.firstWidget() == w1);
        CHECK(splitter.secondWidget() == w2);
}

TEST_CASE("TuiSplitter: setSplitRatio") {
        TuiSplitter splitter;
        splitter.setSplitRatio(0.3);
        CHECK(splitter.splitRatio() == doctest::Approx(0.3));

        splitter.setSplitRatio(0.7);
        CHECK(splitter.splitRatio() == doctest::Approx(0.7));
}

TEST_CASE("TuiSplitter: split ratio clamping") {
        TuiSplitter splitter;

        splitter.setSplitRatio(0.0);
        CHECK(splitter.splitRatio() >= 0.0);

        splitter.setSplitRatio(1.0);
        CHECK(splitter.splitRatio() <= 1.0);
}

TEST_CASE("TuiSplitter: horizontal layout distributes geometry") {
        TuiSplitter splitter(TuiSplitter::Horizontal);
        TuiWidget *w1 = new TuiWidget(&splitter);
        TuiWidget *w2 = new TuiWidget(&splitter);

        splitter.setFirstWidget(w1);
        splitter.setSecondWidget(w2);
        splitter.setSplitRatio(0.5);
        splitter.setGeometry(Rect2Di32(0, 0, 80, 24));

        CHECK(w1->width() > 0);
        CHECK(w2->width() > 0);
        CHECK(w1->width() + w2->width() <= 80);
}

TEST_CASE("TuiSplitter: vertical layout distributes geometry") {
        TuiSplitter splitter(TuiSplitter::Vertical);
        TuiWidget *w1 = new TuiWidget(&splitter);
        TuiWidget *w2 = new TuiWidget(&splitter);

        splitter.setFirstWidget(w1);
        splitter.setSecondWidget(w2);
        splitter.setSplitRatio(0.5);
        splitter.setGeometry(Rect2Di32(0, 0, 80, 24));

        CHECK(w1->height() > 0);
        CHECK(w2->height() > 0);
        CHECK(w1->height() + w2->height() <= 24);
}

TEST_CASE("TuiSplitter: sizeHint") {
        TuiSplitter splitter;
        Size2Di32 hint = splitter.sizeHint();
        CHECK(hint.width() >= 0);
        CHECK(hint.height() >= 0);
}
