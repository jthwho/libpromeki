/**
 * @file      palette.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <doctest/doctest.h>
#include <promeki/tui/palette.h>

using namespace promeki;

TEST_CASE("TuiPalette: default construction") {
        TuiPalette pal;
        // Default palette should have non-default colors for common roles
        Color win = pal.color(TuiPalette::Active, TuiPalette::Window);
        Color text = pal.color(TuiPalette::Active, TuiPalette::WindowText);
        // Just verify we can query without crashing and get valid colors
        CHECK(win.isValid());
        CHECK(text.isValid());
}

TEST_CASE("TuiPalette: setColor and color") {
        TuiPalette pal;
        pal.setColor(TuiPalette::Active, TuiPalette::Window, Color::Red);
        CHECK(pal.color(TuiPalette::Active, TuiPalette::Window) == Color::Red);

        pal.setColor(TuiPalette::Inactive, TuiPalette::Window, Color::Blue);
        CHECK(pal.color(TuiPalette::Inactive, TuiPalette::Window) == Color::Blue);

        // Active should still be red
        CHECK(pal.color(TuiPalette::Active, TuiPalette::Window) == Color::Red);
}

TEST_CASE("TuiPalette: color groups are independent") {
        TuiPalette pal;
        pal.setColor(TuiPalette::Active, TuiPalette::Text, Color::White);
        pal.setColor(TuiPalette::Inactive, TuiPalette::Text, Color::LightGray);
        pal.setColor(TuiPalette::Disabled, TuiPalette::Text, Color::DarkGray);

        CHECK(pal.color(TuiPalette::Active, TuiPalette::Text) == Color::White);
        CHECK(pal.color(TuiPalette::Inactive, TuiPalette::Text) == Color::LightGray);
        CHECK(pal.color(TuiPalette::Disabled, TuiPalette::Text) == Color::DarkGray);
}

TEST_CASE("TuiPalette: convenience color method") {
        TuiPalette pal;
        pal.setColor(TuiPalette::Active, TuiPalette::Base, Color::White);
        pal.setColor(TuiPalette::Inactive, TuiPalette::Base, Color::LightGray);
        pal.setColor(TuiPalette::Disabled, TuiPalette::Base, Color::DarkGray);

        CHECK(pal.color(TuiPalette::Base, true, true) == Color::White);     // focused + enabled = Active
        CHECK(pal.color(TuiPalette::Base, false, true) == Color::LightGray); // !focused + enabled = Inactive
        CHECK(pal.color(TuiPalette::Base, false, false) == Color::DarkGray); // disabled
        CHECK(pal.color(TuiPalette::Base, true, false) == Color::DarkGray);  // disabled overrides focus
}

TEST_CASE("TuiPalette: all color roles queryable") {
        TuiPalette pal;
        for(int role = 0; role < TuiPalette::RoleCount; ++role) {
                Color c = pal.color(TuiPalette::Active, static_cast<TuiPalette::ColorRole>(role));
                CHECK(c.isValid());
        }
}
