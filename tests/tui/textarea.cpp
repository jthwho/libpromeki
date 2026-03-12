/**
 * @file      textarea.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/textarea.h>

using namespace promeki;

TEST_CASE("TuiTextArea: default construction") {
        TuiTextArea area;
        CHECK(area.text().isEmpty());
        CHECK_FALSE(area.isReadOnly());
}

TEST_CASE("TuiTextArea: setText and text") {
        TuiTextArea area;
        area.setText("Line 1\nLine 2\nLine 3");
        CHECK(area.text() == "Line 1\nLine 2\nLine 3");
}

TEST_CASE("TuiTextArea: appendLine") {
        TuiTextArea area;
        area.appendLine("First");
        area.appendLine("Second");
        String t = area.text();
        CHECK(t.contains("First"));
        CHECK(t.contains("Second"));
}

TEST_CASE("TuiTextArea: readOnly") {
        TuiTextArea area;
        CHECK_FALSE(area.isReadOnly());

        area.setReadOnly(true);
        CHECK(area.isReadOnly());

        area.setReadOnly(false);
        CHECK_FALSE(area.isReadOnly());
}

TEST_CASE("TuiTextArea: sizeHint") {
        TuiTextArea area;
        area.setText("Hello\nWorld");
        Size2Di32 hint = area.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() > 0);
}

TEST_CASE("TuiTextArea: setText replaces content") {
        TuiTextArea area;
        area.setText("Original");
        area.setText("Replaced");
        CHECK(area.text() == "Replaced");
}
