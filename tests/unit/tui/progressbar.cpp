/**
 * @file      progressbar.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/progressbar.h>

using namespace promeki;

TEST_CASE("TuiProgressBar: default construction") {
        TuiProgressBar bar;
        CHECK(bar.value() == 0);
        CHECK(bar.minimum() == 0);
        CHECK(bar.maximum() == 100);
}

TEST_CASE("TuiProgressBar: setValue") {
        TuiProgressBar bar;
        bar.setValue(50);
        CHECK(bar.value() == 50);

        bar.setValue(100);
        CHECK(bar.value() == 100);
}

TEST_CASE("TuiProgressBar: setRange") {
        TuiProgressBar bar;
        bar.setRange(10, 200);
        CHECK(bar.minimum() == 10);
        CHECK(bar.maximum() == 200);
}

TEST_CASE("TuiProgressBar: value clamping") {
        TuiProgressBar bar;
        bar.setRange(0, 100);

        bar.setValue(-10);
        CHECK(bar.value() >= 0);

        bar.setValue(200);
        CHECK(bar.value() <= 100);
}

TEST_CASE("TuiProgressBar: sizeHint") {
        TuiProgressBar bar;
        Size2Di32      hint = bar.sizeHint();
        CHECK(hint.width() > 0);
        CHECK(hint.height() >= 1);
}

TEST_CASE("TuiProgressBar: setRange then setValue") {
        TuiProgressBar bar;
        bar.setRange(0, 50);
        bar.setValue(25);
        CHECK(bar.value() == 25);
        CHECK(bar.minimum() == 0);
        CHECK(bar.maximum() == 50);
}

TEST_CASE("TuiProgressBar: focus policy is NoFocus") {
        TuiProgressBar bar;
        CHECK(bar.focusPolicy() == NoFocus);
}
