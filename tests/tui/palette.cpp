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
        // Default palette should have styles with valid colors for common roles
        TuiStyle win = pal.style(TuiPalette::Active, TuiPalette::Window);
        TuiStyle text = pal.style(TuiPalette::Active, TuiPalette::WindowText);
        // Window is a background role, WindowText is a foreground role
        CHECK(win.hasBackground());
        CHECK(text.hasForeground());
}

TEST_CASE("TuiPalette: setStyle and style") {
        TuiPalette pal;
        TuiStyle red = TuiStyle::fromBackground(Color::Red);
        TuiStyle blue = TuiStyle::fromBackground(Color::Blue);

        pal.setStyle(TuiPalette::Active, TuiPalette::Window, red);
        CHECK(pal.style(TuiPalette::Active, TuiPalette::Window).background() == Color::Red);

        pal.setStyle(TuiPalette::Inactive, TuiPalette::Window, blue);
        CHECK(pal.style(TuiPalette::Inactive, TuiPalette::Window).background() == Color::Blue);

        // Active should still be red
        CHECK(pal.style(TuiPalette::Active, TuiPalette::Window).background() == Color::Red);
}

TEST_CASE("TuiPalette: style groups are independent") {
        TuiPalette pal;
        pal.setStyle(TuiPalette::Active, TuiPalette::Text, TuiStyle::fromForeground(Color::White));
        pal.setStyle(TuiPalette::Inactive, TuiPalette::Text, TuiStyle::fromForeground(Color::LightGray));
        pal.setStyle(TuiPalette::Disabled, TuiPalette::Text, TuiStyle::fromForeground(Color::DarkGray));

        CHECK(pal.style(TuiPalette::Active, TuiPalette::Text).foreground() == Color::White);
        CHECK(pal.style(TuiPalette::Inactive, TuiPalette::Text).foreground() == Color::LightGray);
        CHECK(pal.style(TuiPalette::Disabled, TuiPalette::Text).foreground() == Color::DarkGray);
}

TEST_CASE("TuiPalette: convenience style method") {
        TuiPalette pal;
        pal.setStyle(TuiPalette::Active, TuiPalette::Base, TuiStyle::fromBackground(Color::White));
        pal.setStyle(TuiPalette::Inactive, TuiPalette::Base, TuiStyle::fromBackground(Color::LightGray));
        pal.setStyle(TuiPalette::Disabled, TuiPalette::Base, TuiStyle::fromBackground(Color::DarkGray));

        CHECK(pal.style(TuiPalette::Base, true, true).background() == Color::White);      // focused + enabled = Active
        CHECK(pal.style(TuiPalette::Base, false, true).background() == Color::LightGray);  // !focused + enabled = Inactive
        CHECK(pal.style(TuiPalette::Base, false, false).background() == Color::DarkGray);  // disabled
        CHECK(pal.style(TuiPalette::Base, true, false).background() == Color::DarkGray);   // disabled overrides focus
}

TEST_CASE("TuiPalette: all roles queryable") {
        TuiPalette pal;
        for(int role = 0; role < TuiPalette::RoleCount; ++role) {
                TuiStyle s = pal.style(TuiPalette::Active, static_cast<TuiPalette::ColorRole>(role));
                // Every role should have at least a foreground or background defined
                CHECK((s.hasForeground() || s.hasBackground()));
        }
}
