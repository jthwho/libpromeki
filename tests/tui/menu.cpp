/**
 * @file      menu.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/menu.h>

using namespace promeki;

TEST_CASE("TuiAction: construction") {
        TuiAction action("Open");
        CHECK(action.text() == "Open");
        CHECK(action.isEnabled());
}

TEST_CASE("TuiAction: setText") {
        TuiAction action("Old");
        action.setText("New");
        CHECK(action.text() == "New");
}

TEST_CASE("TuiAction: setEnabled") {
        TuiAction action("Test");
        CHECK(action.isEnabled());

        action.setEnabled(false);
        CHECK_FALSE(action.isEnabled());

        action.setEnabled(true);
        CHECK(action.isEnabled());
}

TEST_CASE("TuiMenu: default construction") {
        TuiMenu menu;
        CHECK(menu.title().isEmpty());
        CHECK(menu.actions().size() == 0);
        CHECK_FALSE(menu.isOpen());
        CHECK(menu.currentIndex() == 0);
}

TEST_CASE("TuiMenu: construction with title") {
        TuiMenu menu("File");
        CHECK(menu.title() == "File");
}

TEST_CASE("TuiMenu: setTitle") {
        TuiMenu menu;
        menu.setTitle("Edit");
        CHECK(menu.title() == "Edit");
}

TEST_CASE("TuiMenu: addAction") {
        TuiMenu menu("File");
        TuiAction *open = menu.addAction("Open");
        TuiAction *save = menu.addAction("Save");

        CHECK(open != nullptr);
        CHECK(save != nullptr);
        CHECK(menu.actions().size() == 2);
        CHECK(menu.actions()[0]->text() == "Open");
        CHECK(menu.actions()[1]->text() == "Save");
}

TEST_CASE("TuiMenu: open and close") {
        TuiMenu menu("Test");
        CHECK_FALSE(menu.isOpen());

        menu.open();
        CHECK(menu.isOpen());

        menu.close();
        CHECK_FALSE(menu.isOpen());
}

TEST_CASE("TuiMenu: setCurrentIndex") {
        TuiMenu menu("Test");
        menu.addAction("A");
        menu.addAction("B");
        menu.addAction("C");

        menu.setCurrentIndex(2);
        CHECK(menu.currentIndex() == 2);
}

TEST_CASE("TuiMenu: sizeHint") {
        TuiMenu menu("File");
        menu.addAction("Open");
        menu.addAction("Save");
        Size2Di32 hint = menu.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() > 0);
}

TEST_CASE("TuiMenuBar: default construction") {
        TuiMenuBar bar;
        CHECK(bar.menus().size() == 0);
        CHECK(bar.currentIndex() == 0);
}

TEST_CASE("TuiMenuBar: addMenu") {
        TuiMenuBar bar;
        TuiMenu *file = bar.addMenu("File");
        TuiMenu *edit = bar.addMenu("Edit");

        CHECK(file != nullptr);
        CHECK(edit != nullptr);
        CHECK(bar.menus().size() == 2);
        CHECK(bar.menus()[0]->title() == "File");
        CHECK(bar.menus()[1]->title() == "Edit");
}

TEST_CASE("TuiMenuBar: sizeHint") {
        TuiMenuBar bar;
        bar.addMenu("File");
        bar.addMenu("Edit");
        Size2Di32 hint = bar.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() >= 1);
}
