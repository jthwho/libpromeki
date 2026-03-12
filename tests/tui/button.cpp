/**
 * @file      button.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/button.h>

using namespace promeki;

TEST_CASE("TuiButton: construction with text") {
        TuiButton button("OK");
        CHECK(button.text() == "OK");
        CHECK(button.focusPolicy() == StrongFocus);
}

TEST_CASE("TuiButton: setText") {
        TuiButton button;
        button.setText("Cancel");
        CHECK(button.text() == "Cancel");
}

TEST_CASE("TuiButton: sizeHint") {
        TuiButton button("OK");
        Size2Di32 hint = button.sizeHint();
        CHECK(hint.width() == 6); // "OK" + 4 padding
        CHECK(hint.height() == 3);
}
