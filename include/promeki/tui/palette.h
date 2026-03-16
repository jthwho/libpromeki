/**
 * @file      tui/palette.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/tui/style.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Style palette for TUI widgets.
 * @ingroup tui_core
 *
 * Provides a centralized set of TuiStyle values organized by role and
 * state group.  Widgets query the palette for their styles instead of
 * hardcoding colors, giving a consistent look.
 *
 * Each role stores a TuiStyle whose foreground, background, and
 * attributes may individually be set or left ignored.  Widgets
 * typically merge several roles together (e.g. a text role on top of
 * a background role) using TuiStyle::merged().
 *
 * The TUI rendering pipeline gracefully degrades palette colors to
 * match the terminal's color capability (see Terminal::ColorSupport).
 * While any palette will render at any color level, choosing colors
 * that are distinct under the target mode (e.g. high-contrast values
 * for Basic 16-color, or well-separated grays for grayscale modes)
 * produces significantly better results.
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
                 * @brief Semantic style role.
                 */
                enum ColorRole {
                        Window,           ///< Container/frame background.
                        WindowText,       ///< Text on Window background.
                        Base,             ///< Input/editable widget background.
                        Text,             ///< Text on Base background.
                        Button,           ///< Button background (legacy).
                        ButtonText,       ///< Text on button backgrounds.
                        ButtonBorder,     ///< Single-character border around buttons.
                        ButtonLight,      ///< Button background in unpressed / active-tab state.
                        ButtonDark,       ///< Button background in pressed / inactive-tab state.
                        FocusText,        ///< Bright foreground for focused widgets.
                        Highlight,        ///< Selected item / accent background.
                        HighlightedText,  ///< Text on Highlight background.
                        PlaceholderText,  ///< Placeholder/hint text.
                        Mid,              ///< Borders, separators, frames.
                        StatusBar,        ///< Status bar background.
                        StatusBarText,    ///< Status bar text.
                        ProgressFilled,       ///< Filled portion of progress bar.
                        ProgressFilledText,   ///< Text on filled portion of progress bar.
                        ProgressEmpty,        ///< Empty portion of progress bar.
                        ProgressEmptyText,    ///< Text on empty portion of progress bar.
                        RoleCount
                };

                /** @brief Constructs a palette with default styles. */
                TuiPalette();

                /**
                 * @brief Sets a style for a specific group and role.
                 * @param group The state group.
                 * @param role  The semantic color role.
                 * @param style The style to set.
                 */
                void setStyle(ColorGroup group, ColorRole role, const TuiStyle &style);

                /**
                 * @brief Returns the style for a specific group and role.
                 * @param group The state group.
                 * @param role  The semantic color role.
                 * @return The stored TuiStyle.
                 */
                TuiStyle style(ColorGroup group, ColorRole role) const;

                /**
                 * @brief Returns the style for a role given widget state.
                 *
                 * Selects the appropriate color group from the state and
                 * returns the stored TuiStyle.
                 *
                 * @param role  The semantic color role.
                 * @param state The widget state to derive the group from.
                 * @return The stored TuiStyle for the derived group.
                 */
                TuiStyle style(ColorRole role, const TuiStyleState &state) const;

                /**
                 * @brief Convenience: picks the group from widget state flags.
                 * @param role    The semantic color role.
                 * @param focused True if the widget has focus.
                 * @param enabled True if the widget is enabled.
                 */
                TuiStyle style(ColorRole role, bool focused, bool enabled = true) const;

        private:
                TuiStyle _styles[GroupCount][RoleCount];
};

PROMEKI_NAMESPACE_END
