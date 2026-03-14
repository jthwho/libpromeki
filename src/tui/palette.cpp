/**
 * @file      palette.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/palette.h>

PROMEKI_NAMESPACE_BEGIN

TuiPalette::TuiPalette() {
        // Helper: foreground-only style (background ignored).
        auto fg = [](const Color &c, uint8_t attrs = TuiStyle::None) {
                return TuiStyle::fromForeground(c, attrs, attrs ? 0xFF : 0);
        };
        // Helper: background-only style (foreground ignored).
        auto bg = [](const Color &c) {
                return TuiStyle::fromBackground(c);
        };

        // ── Active (widget has focus) ────────────────────────────
        _styles[Active][Window]           = bg(Color(0, 0, 48));
        _styles[Active][WindowText]       = fg(Color::White);
        _styles[Active][Base]             = bg(Color(0, 0, 64));
        _styles[Active][Text]             = fg(Color::White);
        _styles[Active][Button]           = bg(Color(40, 80, 200));
        _styles[Active][ButtonText]       = fg(Color::White);
        _styles[Active][ButtonBorder]     = fg(Color(80, 130, 230));
        _styles[Active][ButtonLight]      = bg(Color(60, 100, 220));
        _styles[Active][ButtonDark]       = bg(Color(30, 50, 120));
        _styles[Active][FocusText]        = fg(Color::White);
        _styles[Active][Highlight]        = bg(Color(60, 120, 220));
        _styles[Active][HighlightedText]  = fg(Color::White);
        _styles[Active][PlaceholderText]  = fg(Color(128, 128, 160));
        _styles[Active][Mid]              = fg(Color(100, 120, 200));
        _styles[Active][StatusBar]        = bg(Color(40, 80, 200));
        _styles[Active][StatusBarText]    = fg(Color::White);
        _styles[Active][ProgressFilled]       = bg(Color(32, 160, 64));
        _styles[Active][ProgressFilledText]   = fg(Color(32, 160, 64).contrastingBW());
        _styles[Active][ProgressEmpty]        = bg(Color(32, 32, 32));
        _styles[Active][ProgressEmptyText]    = fg(Color(192, 192, 192));

        // ── Inactive (widget does not have focus) ────────────────
        _styles[Inactive][Window]           = bg(Color::Black);
        _styles[Inactive][WindowText]       = fg(Color(192, 192, 192));
        _styles[Inactive][Base]             = bg(Color(16, 16, 16));
        _styles[Inactive][Text]             = fg(Color(192, 192, 192));
        _styles[Inactive][Button]           = bg(Color(48, 48, 48));
        _styles[Inactive][ButtonText]       = fg(Color(192, 192, 192));
        _styles[Inactive][ButtonBorder]     = fg(Color(80, 80, 80));
        _styles[Inactive][ButtonLight]      = bg(Color(64, 64, 64));
        _styles[Inactive][ButtonDark]       = bg(Color(32, 32, 32));
        _styles[Inactive][FocusText]        = fg(Color::White);
        _styles[Inactive][Highlight]        = bg(Color(48, 48, 64));
        _styles[Inactive][HighlightedText]  = fg(Color::White);
        _styles[Inactive][PlaceholderText]  = fg(Color(96, 96, 96));
        _styles[Inactive][Mid]              = fg(Color(80, 80, 80));
        _styles[Inactive][StatusBar]        = bg(Color(48, 48, 48));
        _styles[Inactive][StatusBarText]    = fg(Color(192, 192, 192));
        _styles[Inactive][ProgressFilled]       = bg(Color(32, 96, 48));
        _styles[Inactive][ProgressFilledText]   = fg(Color(32, 96, 48).contrastingBW());
        _styles[Inactive][ProgressEmpty]        = bg(Color(24, 24, 24));
        _styles[Inactive][ProgressEmptyText]    = fg(Color(128, 128, 128));

        // ── Disabled ─────────────────────────────────────────────
        _styles[Disabled][Window]           = bg(Color::Black);
        _styles[Disabled][WindowText]       = fg(Color(96, 96, 96));
        _styles[Disabled][Base]             = bg(Color(8, 8, 8));
        _styles[Disabled][Text]             = fg(Color(96, 96, 96));
        _styles[Disabled][Button]           = bg(Color(32, 32, 32));
        _styles[Disabled][ButtonText]       = fg(Color(96, 96, 96));
        _styles[Disabled][ButtonBorder]     = fg(Color(48, 48, 48));
        _styles[Disabled][ButtonLight]      = bg(Color(40, 40, 40));
        _styles[Disabled][ButtonDark]       = bg(Color(24, 24, 24));
        _styles[Disabled][FocusText]        = fg(Color::White);
        _styles[Disabled][Highlight]        = bg(Color(32, 32, 32));
        _styles[Disabled][HighlightedText]  = fg(Color(96, 96, 96));
        _styles[Disabled][PlaceholderText]  = fg(Color(64, 64, 64));
        _styles[Disabled][Mid]              = fg(Color(48, 48, 48));
        _styles[Disabled][StatusBar]        = bg(Color(32, 32, 32));
        _styles[Disabled][StatusBarText]    = fg(Color(96, 96, 96));
        _styles[Disabled][ProgressFilled]       = bg(Color(32, 48, 32));
        _styles[Disabled][ProgressFilledText]   = fg(Color(32, 48, 32).contrastingBW());
        _styles[Disabled][ProgressEmpty]        = bg(Color(16, 16, 16));
        _styles[Disabled][ProgressEmptyText]    = fg(Color(64, 64, 64));
}

void TuiPalette::setStyle(ColorGroup group, ColorRole role, const TuiStyle &style) {
        _styles[group][role] = style;
}

TuiStyle TuiPalette::style(ColorGroup group, ColorRole role) const {
        return _styles[group][role];
}

TuiStyle TuiPalette::style(ColorRole role, const TuiStyleState &state) const {
        if(!state.enabled()) return _styles[Disabled][role];
        return _styles[state.focused() ? Active : Inactive][role];
}

TuiStyle TuiPalette::style(ColorRole role, bool focused, bool enabled) const {
        if(!enabled) return _styles[Disabled][role];
        return _styles[focused ? Active : Inactive][role];
}

PROMEKI_NAMESPACE_END
