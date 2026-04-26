/**
 * @file      label.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/label.h>

using namespace promeki;

TEST_CASE("TuiLabel: construction with text") {
        TuiLabel label("Hello World");
        CHECK(label.text() == "Hello World");
        CHECK(label.focusPolicy() == NoFocus);
}

TEST_CASE("TuiLabel: setText") {
        TuiLabel label;
        label.setText("Test");
        CHECK(label.text() == "Test");
}

TEST_CASE("TuiLabel: alignment") {
        TuiLabel label("Test");
        CHECK(label.alignment() == AlignLeft);

        label.setAlignment(AlignCenter);
        CHECK(label.alignment() == AlignCenter);

        label.setAlignment(AlignRight);
        CHECK(label.alignment() == AlignRight);
}

TEST_CASE("TuiLabel: sizeHint") {
        TuiLabel  label("Hello");
        Size2Di32 hint = label.sizeHint();
        CHECK(hint.width() == 5);
        CHECK(hint.height() == 1);
}

TEST_CASE("TuiLabel: word wrap") {
        TuiLabel label;
        CHECK_FALSE(label.wordWrap());

        label.setWordWrap(true);
        CHECK(label.wordWrap());
}
