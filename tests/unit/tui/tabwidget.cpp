/**
 * @file      tabwidget.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/tabwidget.h>

using namespace promeki;

TEST_CASE("TuiTabWidget: default construction") {
        TuiTabWidget tabs;
        CHECK(tabs.count() == 0);
        CHECK(tabs.currentIndex() == -1);
        CHECK(tabs.currentWidget() == nullptr);
}

TEST_CASE("TuiTabWidget: addTab") {
        TuiTabWidget tabs;
        TuiWidget   *w1 = new TuiWidget(&tabs);
        TuiWidget   *w2 = new TuiWidget(&tabs);

        tabs.addTab(w1, "Tab 1");
        CHECK(tabs.count() == 1);
        CHECK(tabs.currentIndex() == 0);
        CHECK(tabs.currentWidget() == w1);

        tabs.addTab(w2, "Tab 2");
        CHECK(tabs.count() == 2);
}

TEST_CASE("TuiTabWidget: setCurrentIndex") {
        TuiTabWidget tabs;
        TuiWidget   *w1 = new TuiWidget(&tabs);
        TuiWidget   *w2 = new TuiWidget(&tabs);
        TuiWidget   *w3 = new TuiWidget(&tabs);

        tabs.addTab(w1, "A");
        tabs.addTab(w2, "B");
        tabs.addTab(w3, "C");

        tabs.setCurrentIndex(1);
        CHECK(tabs.currentIndex() == 1);
        CHECK(tabs.currentWidget() == w2);

        tabs.setCurrentIndex(2);
        CHECK(tabs.currentIndex() == 2);
        CHECK(tabs.currentWidget() == w3);
}

TEST_CASE("TuiTabWidget: removeTab") {
        TuiTabWidget tabs;
        TuiWidget   *w1 = new TuiWidget(&tabs);
        TuiWidget   *w2 = new TuiWidget(&tabs);

        tabs.addTab(w1, "A");
        tabs.addTab(w2, "B");
        CHECK(tabs.count() == 2);

        tabs.removeTab(0);
        CHECK(tabs.count() == 1);
}

TEST_CASE("TuiTabWidget: sizeHint") {
        TuiTabWidget tabs;
        TuiWidget   *w1 = new TuiWidget(&tabs);
        tabs.addTab(w1, "Tab");

        Size2Di32 hint = tabs.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() > 0);
}
