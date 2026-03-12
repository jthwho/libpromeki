/**
 * @file      lineedit.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/lineedit.h>

using namespace promeki;

TEST_CASE("TuiLineEdit: default construction") {
        TuiLineEdit edit;
        CHECK(edit.text().isEmpty());
        CHECK(edit.placeholder().isEmpty());
        CHECK(edit.focusPolicy() == StrongFocus);
}

TEST_CASE("TuiLineEdit: construction with text") {
        TuiLineEdit edit("Hello");
        CHECK(edit.text() == "Hello");
}

TEST_CASE("TuiLineEdit: setText") {
        TuiLineEdit edit;
        edit.setText("Test");
        CHECK(edit.text() == "Test");

        edit.setText("New");
        CHECK(edit.text() == "New");
}

TEST_CASE("TuiLineEdit: placeholder") {
        TuiLineEdit edit;
        edit.setPlaceholder("Enter text...");
        CHECK(edit.placeholder() == "Enter text...");
}

TEST_CASE("TuiLineEdit: sizeHint") {
        TuiLineEdit edit("Hello");
        Size2Di32 hint = edit.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() >= 1);
}

TEST_CASE("TuiLineEdit: setText clears previous") {
        TuiLineEdit edit("First");
        CHECK(edit.text() == "First");
        edit.setText("Second");
        CHECK(edit.text() == "Second");
}
