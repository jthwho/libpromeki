/**
 * @file      widget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/widget.h>

using namespace promeki;

TEST_CASE("TuiWidget: default construction") {
        TuiWidget w;
        CHECK(w.x() == 0);
        CHECK(w.y() == 0);
        CHECK(w.width() == 0);
        CHECK(w.height() == 0);
        CHECK(w.isVisible());
        CHECK(w.isEnabled());
        CHECK_FALSE(w.hasFocus());
        CHECK(w.isDirty());
}

TEST_CASE("TuiWidget: setGeometry") {
        TuiWidget w;
        w.setGeometry(Rect2Di32(10, 20, 100, 50));
        CHECK(w.x() == 10);
        CHECK(w.y() == 20);
        CHECK(w.width() == 100);
        CHECK(w.height() == 50);
}

TEST_CASE("TuiWidget: visibility") {
        TuiWidget w;
        CHECK(w.isVisible());

        // Can't easily test the visibilityChanged signal here without a full
        // ObjectBase setup; just verify show/hide flip the visibility flag.
        w.hide();
        CHECK_FALSE(w.isVisible());

        w.show();
        CHECK(w.isVisible());
}

TEST_CASE("TuiWidget: parent-child") {
        TuiWidget  parent;
        TuiWidget *child = new TuiWidget(&parent);

        CHECK(child->parent() == &parent);
        CHECK(parent.childList().size() == 1);

        // Parent destruction cleans up child
}

TEST_CASE("TuiWidget: mapToParent and mapFromParent") {
        TuiWidget w;
        w.setGeometry(Rect2Di32(10, 20, 100, 50));

        Point2Di32 local(5, 5);
        Point2Di32 parent = w.mapToParent(local);
        CHECK(parent.x() == 15);
        CHECK(parent.y() == 25);

        Point2Di32 back = w.mapFromParent(parent);
        CHECK(back.x() == 5);
        CHECK(back.y() == 5);
}

TEST_CASE("TuiWidget: focus policy") {
        TuiWidget w;
        CHECK(w.focusPolicy() == NoFocus);

        w.setFocusPolicy(StrongFocus);
        CHECK(w.focusPolicy() == StrongFocus);

        w.setFocus();
        CHECK(w.hasFocus());
}

TEST_CASE("TuiWidget: size policy") {
        TuiWidget w;
        CHECK(w.sizePolicy() == SizePreferred);

        w.setSizePolicy(SizeExpanding);
        CHECK(w.sizePolicy() == SizeExpanding);
}

TEST_CASE("TuiWidget: dirty tracking") {
        TuiWidget w;
        CHECK(w.isDirty());

        w.clearDirty();
        CHECK_FALSE(w.isDirty());

        w.update();
        CHECK(w.isDirty());
}
