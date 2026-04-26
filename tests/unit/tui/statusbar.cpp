/**
 * @file      statusbar.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/statusbar.h>

using namespace promeki;

TEST_CASE("TuiStatusBar: default construction") {
        TuiStatusBar bar;
        CHECK(bar.message().isEmpty());
        CHECK(bar.permanentMessage().isEmpty());
}

TEST_CASE("TuiStatusBar: showMessage") {
        TuiStatusBar bar;
        bar.showMessage("Ready");
        CHECK(bar.message() == "Ready");
}

TEST_CASE("TuiStatusBar: clearMessage") {
        TuiStatusBar bar;
        bar.showMessage("Temporary");
        CHECK(bar.message() == "Temporary");

        bar.clearMessage();
        CHECK(bar.message().isEmpty());
}

TEST_CASE("TuiStatusBar: permanentMessage") {
        TuiStatusBar bar;
        bar.setPermanentMessage("Status: OK");
        CHECK(bar.permanentMessage() == "Status: OK");
}

TEST_CASE("TuiStatusBar: showMessage replaces previous") {
        TuiStatusBar bar;
        bar.showMessage("First");
        bar.showMessage("Second");
        CHECK(bar.message() == "Second");
}

TEST_CASE("TuiStatusBar: sizeHint") {
        TuiStatusBar bar;
        Size2Di32    hint = bar.sizeHint();
        CHECK(hint.width() >= 0);
        CHECK(hint.height() >= 1);
}
