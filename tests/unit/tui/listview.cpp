/**
 * @file      listview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/listview.h>
#include <promeki/rect.h>

using namespace promeki;

TEST_CASE("TuiListView: default construction") {
        TuiListView list;
        CHECK(list.count() == 0);
        CHECK(list.currentIndex() == -1);
        CHECK(list.scrollOffset() == 0);
}

TEST_CASE("TuiListView: addItem") {
        TuiListView list;
        list.addItem("Item 1");
        list.addItem("Item 2");
        list.addItem("Item 3");
        CHECK(list.count() == 3);
}

TEST_CASE("TuiListView: insertItem") {
        TuiListView list;
        list.addItem("First");
        list.addItem("Third");
        list.insertItem(1, "Second");
        CHECK(list.count() == 3);
}

TEST_CASE("TuiListView: setItems") {
        TuiListView list;
        StringList  items;
        items += "A";
        items += "B";
        items += "C";
        list.setItems(items);
        CHECK(list.count() == 3);
}

TEST_CASE("TuiListView: clear") {
        TuiListView list;
        list.addItem("A");
        list.addItem("B");
        list.clear();
        CHECK(list.count() == 0);
        CHECK(list.currentIndex() == -1);
}

TEST_CASE("TuiListView: currentIndex and setCurrentIndex") {
        TuiListView list;
        list.addItem("A");
        list.addItem("B");
        list.addItem("C");

        list.setCurrentIndex(1);
        CHECK(list.currentIndex() == 1);
        CHECK(list.currentItem() == "B");

        list.setCurrentIndex(2);
        CHECK(list.currentIndex() == 2);
        CHECK(list.currentItem() == "C");
}

TEST_CASE("TuiListView: currentItem empty list") {
        TuiListView list;
        CHECK(list.currentItem().isEmpty());
}

TEST_CASE("TuiListView: scrollBy moves viewport without changing selection") {
        TuiListView list;
        list.setGeometry(Rect2Di32(0, 0, 20, 10));
        for (int i = 0; i < 20; i++) {
                list.addItem(String::number(i));
        }

        // Verify initial state before scrolling
        CHECK(list.currentIndex() == 0);
        CHECK(list.scrollOffset() == 0);

        // Scroll down — viewport moves but selection stays at index 0
        list.scrollBy(5);
        CHECK(list.scrollOffset() == 5);
        CHECK(list.currentIndex() == 0);

        list.scrollBy(-3);
        CHECK(list.scrollOffset() == 2);
        CHECK(list.currentIndex() == 0);
}

TEST_CASE("TuiListView: scrollBy clamps to bounds") {
        TuiListView list;
        list.setGeometry(Rect2Di32(0, 0, 20, 10));
        for (int i = 0; i < 20; i++) {
                list.addItem(String::number(i));
        }

        list.scrollBy(-10);
        CHECK(list.scrollOffset() == 0);

        list.scrollBy(100);
        CHECK(list.scrollOffset() == 10); // maxOffset = 20 - 10
}

TEST_CASE("TuiListView: ensureVisible adjusts scroll offset") {
        TuiListView list;
        list.setGeometry(Rect2Di32(0, 0, 20, 5));
        for (int i = 0; i < 20; i++) {
                list.addItem(String::number(i));
        }

        // Scroll so selection is off-screen, then bring it back
        list.scrollBy(10);
        CHECK(list.scrollOffset() == 10);
        CHECK(list.currentIndex() == 0);

        // ensureVisible should scroll back so index 0 is visible
        list.ensureVisible(0);
        CHECK(list.scrollOffset() == 0);

        // Ensure item near the end is visible
        list.ensureVisible(19);
        CHECK(list.scrollOffset() == 15); // 19 - 5 + 1
}

TEST_CASE("TuiListView: sizeHint") {
        TuiListView list;
        list.addItem("Hello");
        list.addItem("World");
        Size2Di32 hint = list.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() > 0);
}
