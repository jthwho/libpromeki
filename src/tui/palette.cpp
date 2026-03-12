/**
 * @file      palette.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/tui/palette.h>

PROMEKI_NAMESPACE_BEGIN

TuiPalette::TuiPalette() {
        // ── Active (widget has focus) ────────────────────────────
        _colors[Active][Window]           = Color(0, 0, 48);
        _colors[Active][WindowText]       = Color::White;
        _colors[Active][Base]             = Color(0, 0, 64);
        _colors[Active][Text]             = Color::White;
        _colors[Active][Button]           = Color(40, 80, 200);
        _colors[Active][ButtonText]       = Color::White;
        _colors[Active][Highlight]        = Color(60, 120, 220);
        _colors[Active][HighlightedText]  = Color::White;
        _colors[Active][PlaceholderText]  = Color(128, 128, 160);
        _colors[Active][Mid]              = Color(100, 120, 200);
        _colors[Active][StatusBar]        = Color(40, 80, 200);
        _colors[Active][StatusBarText]    = Color::White;
        _colors[Active][ProgressFilled]   = Color(32, 160, 64);
        _colors[Active][ProgressEmpty]    = Color(32, 32, 32);

        // ── Inactive (widget does not have focus) ────────────────
        _colors[Inactive][Window]           = Color::Black;
        _colors[Inactive][WindowText]       = Color(192, 192, 192);
        _colors[Inactive][Base]             = Color(16, 16, 16);
        _colors[Inactive][Text]             = Color(192, 192, 192);
        _colors[Inactive][Button]           = Color(48, 48, 48);
        _colors[Inactive][ButtonText]       = Color(192, 192, 192);
        _colors[Inactive][Highlight]        = Color(48, 48, 64);
        _colors[Inactive][HighlightedText]  = Color::White;
        _colors[Inactive][PlaceholderText]  = Color(96, 96, 96);
        _colors[Inactive][Mid]              = Color(80, 80, 80);
        _colors[Inactive][StatusBar]        = Color(48, 48, 48);
        _colors[Inactive][StatusBarText]    = Color(192, 192, 192);
        _colors[Inactive][ProgressFilled]   = Color(32, 96, 48);
        _colors[Inactive][ProgressEmpty]    = Color(24, 24, 24);

        // ── Disabled ─────────────────────────────────────────────
        _colors[Disabled][Window]           = Color::Black;
        _colors[Disabled][WindowText]       = Color(96, 96, 96);
        _colors[Disabled][Base]             = Color(8, 8, 8);
        _colors[Disabled][Text]             = Color(96, 96, 96);
        _colors[Disabled][Button]           = Color(32, 32, 32);
        _colors[Disabled][ButtonText]       = Color(96, 96, 96);
        _colors[Disabled][Highlight]        = Color(32, 32, 32);
        _colors[Disabled][HighlightedText]  = Color(96, 96, 96);
        _colors[Disabled][PlaceholderText]  = Color(64, 64, 64);
        _colors[Disabled][Mid]              = Color(48, 48, 48);
        _colors[Disabled][StatusBar]        = Color(32, 32, 32);
        _colors[Disabled][StatusBarText]    = Color(96, 96, 96);
        _colors[Disabled][ProgressFilled]   = Color(32, 48, 32);
        _colors[Disabled][ProgressEmpty]    = Color(16, 16, 16);
}

void TuiPalette::setColor(ColorGroup group, ColorRole role, const Color &color) {
        _colors[group][role] = color;
}

Color TuiPalette::color(ColorGroup group, ColorRole role) const {
        return _colors[group][role];
}

Color TuiPalette::color(ColorRole role, bool focused, bool enabled) const {
        if(!enabled) return _colors[Disabled][role];
        return _colors[focused ? Active : Inactive][role];
}

PROMEKI_NAMESPACE_END
