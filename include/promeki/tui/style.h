/**
 * @file      tui/style.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstdint>
#include <promeki/core/namespace.h>
#include <promeki/core/color.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Visual properties of a TUI cell (everything except the character).
 *
 * A TuiStyle bundles foreground color, background color, and text
 * attributes into a single value.  Any property can be flagged as
 * "ignored" so that it inherits from a lower layer when styles are
 * merged via merged().
 *
 * An ignored color is represented by Color::Ignored (an invalid Color).
 * Attributes use a separate mask: bits set in the mask are defined by
 * this style; bits not in the mask are inherited during merging.
 */
class TuiStyle {
        public:
                /**
                 * @brief Text attribute flags.
                 */
                enum Attr : uint8_t {
                        None          = 0x00,
                        Bold          = 0x01,
                        Dim           = 0x02,
                        Italic        = 0x04,
                        Underline     = 0x08,
                        Blink         = 0x10,
                        Inverse       = 0x20,
                        Strikethrough = 0x40
                };

                /** @brief Default constructor.  All properties are ignored. */
                TuiStyle() = default;

                /** @brief Constructs with foreground, background, and attributes (all defined). */
                TuiStyle(const Color &fg, const Color &bg, uint8_t attrs = None)
                        : _fg(fg), _bg(bg), _attrs(attrs), _attrMask(0xFF) {}

                /** @brief Returns the foreground color. */
                Color foreground() const { return _fg; }

                /** @brief Returns the background color. */
                Color background() const { return _bg; }

                /** @brief Returns the attribute flags. */
                uint8_t attrs() const { return _attrs; }

                /** @brief Returns the attribute mask (1 = defined, 0 = ignored). */
                uint8_t attrMask() const { return _attrMask; }

                /** @brief Sets the foreground color. */
                void setForeground(const Color &color) { _fg = color; }

                /** @brief Sets the background color. */
                void setBackground(const Color &color) { _bg = color; }

                /** @brief Sets all attribute flags (marks all bits as defined). */
                void setAttrs(uint8_t attrs) { _attrs = attrs; _attrMask = 0xFF; }

                /** @brief Sets attribute flags with an explicit mask. */
                void setAttrs(uint8_t attrs, uint8_t mask) { _attrs = attrs; _attrMask = mask; }

                /** @brief Returns true if the foreground color is defined (not ignored). */
                bool hasForeground() const { return _fg.isValid(); }

                /** @brief Returns true if the background color is defined (not ignored). */
                bool hasBackground() const { return _bg.isValid(); }

                /**
                 * @brief Merges this style on top of another.
                 *
                 * Ignored properties in this style are filled from @p below.
                 */
                TuiStyle merged(const TuiStyle &below) const;

                /** @brief Creates a foreground-only style (background ignored). */
                static TuiStyle fromForeground(const Color &fg, uint8_t attrs = None, uint8_t attrMask = 0) {
                        TuiStyle s;
                        s._fg = fg;
                        s._attrs = attrs;
                        s._attrMask = attrMask;
                        return s;
                }

                /** @brief Creates a background-only style (foreground ignored). */
                static TuiStyle fromBackground(const Color &bg) {
                        TuiStyle s;
                        s._bg = bg;
                        return s;
                }

                bool operator==(const TuiStyle &o) const {
                        return _fg == o._fg && _bg == o._bg &&
                               _attrs == o._attrs && _attrMask == o._attrMask;
                }
                bool operator!=(const TuiStyle &o) const { return !(*this == o); }

        private:
                Color   _fg;                    ///< Invalid = ignored.
                Color   _bg;                    ///< Invalid = ignored.
                uint8_t _attrs = None;          ///< Attribute flag values.
                uint8_t _attrMask = 0x00;       ///< Which attr bits are defined.
};

/**
 * @brief Widget state bundle fed into palette lookups.
 *
 * Encapsulates all state that can influence which palette style is
 * returned.  Adding new fields here does not break existing callers.
 */
class TuiStyleState {
        public:
                TuiStyleState() = default;

                bool focused() const { return _focused; }
                bool enabled() const { return _enabled; }
                bool pressed() const { return _pressed; }
                bool selected() const { return _selected; }

                void setFocused(bool v) { _focused = v; }
                void setEnabled(bool v) { _enabled = v; }
                void setPressed(bool v) { _pressed = v; }
                void setSelected(bool v) { _selected = v; }

        private:
                bool _focused  = false;
                bool _enabled  = true;
                bool _pressed  = false;
                bool _selected = false;
};

PROMEKI_NAMESPACE_END
