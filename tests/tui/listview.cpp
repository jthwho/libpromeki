/**
 * @file      listview.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/listview.h>

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
        StringList items;
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

TEST_CASE("TuiListView: scrollBy moves currentIndex") {
        TuiListView list;
        for(int i = 0; i < 20; i++) {
                list.addItem(String::number(i));
        }
        list.setCurrentIndex(0);

        list.scrollBy(5);
        CHECK(list.currentIndex() == 5);

        list.scrollBy(-3);
        CHECK(list.currentIndex() == 2);
}

TEST_CASE("TuiListView: scrollBy clamps to bounds") {
        TuiListView list;
        list.addItem("A");
        list.addItem("B");
        list.setCurrentIndex(0);
        list.scrollBy(-10);
        CHECK(list.currentIndex() == 0);

        list.scrollBy(100);
        CHECK(list.currentIndex() == 1);
}

TEST_CASE("TuiListView: sizeHint") {
        TuiListView list;
        list.addItem("Hello");
        list.addItem("World");
        Size2Di32 hint = list.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() > 0);
}
