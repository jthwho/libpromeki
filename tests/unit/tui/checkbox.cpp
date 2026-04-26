/**
 * @file      checkbox.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/checkbox.h>

using namespace promeki;

TEST_CASE("TuiCheckBox: default construction") {
        TuiCheckBox cb;
        CHECK(cb.text().isEmpty());
        CHECK(cb.isChecked() == false);
}

TEST_CASE("TuiCheckBox: construction with text") {
        TuiCheckBox cb("Enable");
        CHECK(cb.text() == "Enable");
        CHECK(cb.isChecked() == false);
}

TEST_CASE("TuiCheckBox: setText") {
        TuiCheckBox cb;
        cb.setText("Option");
        CHECK(cb.text() == "Option");
}

TEST_CASE("TuiCheckBox: setChecked") {
        TuiCheckBox cb("Test");
        cb.setChecked(true);
        CHECK(cb.isChecked() == true);
        cb.setChecked(false);
        CHECK(cb.isChecked() == false);
}

TEST_CASE("TuiCheckBox: toggle") {
        TuiCheckBox cb;
        CHECK(cb.isChecked() == false);
        cb.toggle();
        CHECK(cb.isChecked() == true);
        cb.toggle();
        CHECK(cb.isChecked() == false);
}

TEST_CASE("TuiCheckBox: sizeHint") {
        TuiCheckBox cb("Test");
        Size2Di32   hint = cb.sizeHint();
        CHECK(hint.width() == 8); // "[x] Test" = 4 + 4
        CHECK(hint.height() == 1);
}

TEST_CASE("TuiCheckBox: sizeHint with UTF-8 text") {
        // "café" is 5 bytes but 4 codepoints (é is 2 bytes in UTF-8)
        TuiCheckBox cb(String::fromUtf8("caf\xc3\xa9", 5));
        Size2Di32   hint = cb.sizeHint();
        CHECK(hint.width() == 8); // "[x] café" = 4 + 4 codepoints
        CHECK(hint.height() == 1);
}
