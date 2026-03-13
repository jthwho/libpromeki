/**
 * @file      frame.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/frame.h>
#include <promeki/tui/layout.h>
#include <promeki/tui/widget.h>

using namespace promeki;

TEST_CASE("TuiFrame: layout propagation on resize") {
        TuiFrame frame("Test");

        TuiWidget child1;
        TuiWidget child2;

        auto *layout = new TuiVBoxLayout(&frame);
        layout->addWidget(&child1);
        layout->addWidget(&child2);
        frame.setLayout(layout);

        // Set geometry on the frame, which should trigger resizeEvent
        // and propagate layout to children via contentRect()
        frame.setGeometry(Rect2Di32(5, 10, 40, 10));

        // Content rect is (1, 1, 38, 8) in frame-local (parent-relative) coordinates.
        // Children are positioned relative to the frame, not the screen.
        Rect2Di32 content = frame.contentRect();
        CHECK(content.x() == 1);
        CHECK(content.y() == 1);
        CHECK(content.width() == 38);
        CHECK(content.height() == 8);

        // Children should be positioned within the content area
        CHECK(child1.x() == content.x());
        CHECK(child1.y() == content.y());
        CHECK(child1.width() == content.width());
        CHECK(child1.height() > 0);
        CHECK(child2.x() == content.x());
        CHECK(child2.y() == child1.y() + child1.height());
        CHECK(child2.width() == content.width());
        CHECK(child2.height() > 0);
        // Children stay within the content area bounds
        CHECK(child2.y() + child2.height() <= content.y() + content.height());
}

TEST_CASE("TuiFrame: sizeHint reflects layout content") {
        TuiFrame frame("Title");

        TuiWidget child1;
        TuiWidget child2;
        TuiWidget child3;

        auto *layout = new TuiVBoxLayout(&frame);
        layout->setMargins(1);
        layout->addWidget(&child1);
        layout->addWidget(&child2);
        layout->addWidget(&child3);
        frame.setLayout(layout);

        auto hint = frame.sizeHint();
        // 3 children × height 1 + margins (1+1) + border (1+1) = 7
        CHECK(hint.height() >= 7);
}

TEST_CASE("TuiWidget: default resizeEvent runs layout") {
        TuiWidget parent;

        TuiWidget child1;
        TuiWidget child2;

        auto *layout = new TuiVBoxLayout(&parent);
        layout->addWidget(&child1);
        layout->addWidget(&child2);
        parent.setLayout(layout);

        parent.setGeometry(Rect2Di32(0, 0, 80, 24));

        // Children should have been positioned by the layout
        CHECK(child1.width() == 80);
        CHECK(child1.height() > 0);
        CHECK(child2.width() == 80);
        CHECK(child2.height() > 0);
        CHECK(child2.y() > child1.y());
}
