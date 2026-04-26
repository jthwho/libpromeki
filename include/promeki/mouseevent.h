/**
 * @file      mouseevent.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/event.h>
#include <promeki/string.h>
#include <promeki/point.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Event delivered for mouse input.
 * @ingroup util
 *
 * Carries the mouse position, button, action type, and modifier flags.
 * Registered via Event::registerType().
 */
class MouseEvent : public Event {
        public:
                /** @brief Event type ID for MouseEvent. */
                static const Type Mouse;

                /** @brief Mouse button flags (bit values for combining). */
                enum Button : uint8_t {
                        NoButton = 0x00,
                        LeftButton = 0x01,
                        MiddleButton = 0x02,
                        RightButton = 0x04
                };

                /** @brief Mouse action enumeration. */
                enum Action : uint8_t {
                        Press,
                        Release,
                        Move,
                        DoubleClick,
                        ScrollUp,
                        ScrollDown
                };

                /** @brief Modifier flags (same as KeyEvent). */
                enum Modifier : uint8_t {
                        NoModifier = 0x00,
                        ShiftModifier = 0x01,
                        CtrlModifier = 0x02,
                        AltModifier = 0x04,
                        MetaModifier = 0x08
                };

                /**
                 * @brief Constructs a MouseEvent.
                 * @param pos       The mouse position (column, row).
                 * @param button    The button that triggered this event.
                 * @param action    The action type.
                 * @param modifiers The modifier flags.
                 * @param buttons   Bitmask of all currently pressed buttons.
                 */
                MouseEvent(const Point2Di32 &pos, Button button, Action action, uint8_t modifiers = NoModifier,
                           uint8_t buttons = 0)
                    : Event(Mouse), _pos(pos), _button(button), _action(action), _modifiers(modifiers),
                      _buttons(buttons) {}

                /** @brief Returns the mouse position (column, row). */
                const Point2Di32 &pos() const { return _pos; }

                /** @brief Returns the X coordinate (column). */
                int x() const { return _pos.x(); }

                /** @brief Returns the Y coordinate (row). */
                int y() const { return _pos.y(); }

                /** @brief Returns the mouse button. */
                Button button() const { return _button; }

                /** @brief Returns the mouse action. */
                Action action() const { return _action; }

                /** @brief Returns the modifier flags. */
                uint8_t modifiers() const { return _modifiers; }

                /** @brief Returns the bitmask of all currently pressed buttons. */
                uint8_t buttons() const { return _buttons; }

                /** @brief Returns a human-readable name for the given button. */
                static String buttonName(Button button);

                /** @brief Returns a human-readable string for a button bitmask (e.g. "Left Right"). */
                static String buttonsString(uint8_t buttons);

                /** @brief Returns a human-readable name for the given action. */
                static String actionName(Action action);

        private:
                Point2Di32 _pos;
                Button     _button;
                Action     _action;
                uint8_t    _modifiers;
                uint8_t    _buttons = 0;
};

PROMEKI_NAMESPACE_END
