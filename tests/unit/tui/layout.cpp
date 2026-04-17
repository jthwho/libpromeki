/**
 * @file      layout.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/widget.h>

using namespace promeki;

TEST_CASE("TuiVBoxLayout: basic vertical layout") {
        TuiVBoxLayout layout;

        TuiWidget w1;
        w1.setSizePolicy(SizeFixed);
        TuiWidget w2;
        w2.setSizePolicy(SizeFixed);

        layout.addWidget(&w1);
        layout.addWidget(&w2);

        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        // Both widgets should have been positioned
        CHECK(w1.y() == 0);
        CHECK(w2.y() > 0);
        CHECK(w1.width() == 80);
        CHECK(w2.width() == 80);
}

TEST_CASE("TuiHBoxLayout: basic horizontal layout") {
        TuiHBoxLayout layout;

        TuiWidget w1;
        w1.setSizePolicy(SizeFixed);
        TuiWidget w2;
        w2.setSizePolicy(SizeFixed);

        layout.addWidget(&w1);
        layout.addWidget(&w2);

        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        CHECK(w1.x() == 0);
        CHECK(w2.x() > 0);
        CHECK(w1.height() == 24);
        CHECK(w2.height() == 24);
}

TEST_CASE("TuiBoxLayout: margins") {
        TuiVBoxLayout layout;
        layout.setMargins(2, 3, 2, 3);

        TuiWidget w1;
        layout.addWidget(&w1);
        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        CHECK(w1.x() == 3);
        CHECK(w1.y() == 2);
        CHECK(w1.width() == 74); // 80 - 3 - 3
}

TEST_CASE("TuiBoxLayout: spacing") {
        TuiVBoxLayout layout;
        layout.setSpacing(1);

        TuiWidget w1;
        TuiWidget w2;
        layout.addWidget(&w1);
        layout.addWidget(&w2);

        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        // w2 should start after w1 + 1 spacing
        CHECK(w2.y() == w1.y() + w1.height() + 1);
}

TEST_CASE("TuiBoxLayout: stretch") {
        TuiVBoxLayout layout;

        TuiWidget w1;
        w1.setSizePolicy(SizeFixed);
        TuiWidget w2;
        w2.setSizePolicy(SizeExpanding);

        layout.addWidget(&w1);
        layout.setStretch(0, 1);
        layout.addWidget(&w2);
        layout.setStretch(1, 3);

        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        // w2 should get proportionally more space
        CHECK(w2.height() > w1.height());
}

TEST_CASE("TuiGridLayout: basic grid") {
        TuiGridLayout layout;

        TuiWidget w1, w2, w3, w4;
        layout.addWidget(&w1, 0, 0);
        layout.addWidget(&w2, 0, 1);
        layout.addWidget(&w3, 1, 0);
        layout.addWidget(&w4, 1, 1);

        layout.calculateLayout(Rect2Di32(0, 0, 80, 24));

        // All should be positioned
        CHECK(w1.x() == 0);
        CHECK(w1.y() == 0);
        CHECK(w2.x() > w1.x());
        CHECK(w3.y() > w1.y());
        CHECK(w4.x() > w3.x());
        CHECK(w4.y() > w2.y());
}
