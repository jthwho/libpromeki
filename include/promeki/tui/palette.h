/**
 * @file      palette.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/color.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Color palette for TUI widgets.
 *
 * Provides a centralized set of colors organized by role and state group,
 * similar to Qt's QPalette.  Widgets query the palette for their colors
 * instead of hardcoding them, giving a consistent look and making focus
 * state clearly visible through background color changes.
 */
class TuiPalette {
        public:
                /**
                 * @brief Color group based on widget state.
                 */
                enum ColorGroup {
                        Active,         ///< Widget has focus.
                        Inactive,       ///< Widget does not have focus.
                        Disabled,       ///< Widget is disabled.
                        GroupCount
                };

                /**
                 * @brief Semantic color role.
                 */
                enum ColorRole {
                        Window,           ///< Container/frame background.
                        WindowText,       ///< Text on Window background.
                        Base,             ///< Input/editable widget background.
                        Text,             ///< Text on Base background.
                        Button,           ///< Button background.
                        ButtonText,       ///< Text on Button background.
                        Highlight,        ///< Selected item / accent background.
                        HighlightedText,  ///< Text on Highlight background.
                        PlaceholderText,  ///< Placeholder/hint text.
                        Mid,              ///< Borders, separators, frames.
                        StatusBar,        ///< Status bar background.
                        StatusBarText,    ///< Status bar text.
                        ProgressFilled,   ///< Filled portion of progress bar.
                        ProgressEmpty,    ///< Empty portion of progress bar.
                        RoleCount
                };

                /** @brief Constructs a palette with default colors. */
                TuiPalette();

                /**
                 * @brief Sets a color for a specific group and role.
                 */
                void setColor(ColorGroup group, ColorRole role, const Color &color);

                /**
                 * @brief Returns the color for a specific group and role.
                 */
                Color color(ColorGroup group, ColorRole role) const;

                /**
                 * @brief Convenience: picks the group from widget state.
                 * @param role    The semantic color role.
                 * @param focused True if the widget has focus.
                 * @param enabled True if the widget is enabled.
                 */
                Color color(ColorRole role, bool focused, bool enabled = true) const;

        private:
                Color _colors[GroupCount][RoleCount];
};

PROMEKI_NAMESPACE_END
