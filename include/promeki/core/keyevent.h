/**
 * @file      core/keyevent.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/event.h>
#include <promeki/core/string.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Event delivered when a key is pressed or released.
 * @ingroup core_util
 *
 * Carries a key code, modifier flags, and the printable text
 * for the key press.  Registered via Event::registerType().
 */
class KeyEvent : public Event {
        public:
                /** @brief Event type ID for KeyEvent. */
                static const Type KeyPress;
                /** @brief Event type ID for KeyRelease (rarely used in TUI). */
                static const Type KeyRelease;

                /** @brief Key code enumeration. */
                enum Key {
                        Key_Unknown = 0,
                        // Printable ASCII range handled by char value
                        Key_Space = 32,
                        // Control keys
                        Key_Escape = 256,
                        Key_Enter,
                        Key_Tab,
                        Key_Backspace,
                        Key_Insert,
                        Key_Delete,
                        Key_Home,
                        Key_End,
                        Key_PageUp,
                        Key_PageDown,
                        // Arrow keys
                        Key_Up,
                        Key_Down,
                        Key_Left,
                        Key_Right,
                        // Function keys
                        Key_F1, Key_F2, Key_F3, Key_F4,
                        Key_F5, Key_F6, Key_F7, Key_F8,
                        Key_F9, Key_F10, Key_F11, Key_F12
                };

                /** @brief Modifier flags. */
                enum Modifier : uint8_t {
                        NoModifier    = 0x00,
                        ShiftModifier = 0x01,
                        CtrlModifier  = 0x02,
                        AltModifier   = 0x04,
                        MetaModifier  = 0x08
                };

                /**
                 * @brief Constructs a KeyEvent.
                 * @param type      The event type (KeyPress or KeyRelease).
                 * @param key       The key code.
                 * @param modifiers The modifier flags.
                 * @param text      The printable text for this key.
                 */
                KeyEvent(Type type, Key key, uint8_t modifiers = NoModifier,
                         const String &text = String())
                        : Event(type), _key(key), _modifiers(modifiers), _text(text) {}

                /** @brief Returns the key code. */
                Key key() const { return _key; }

                /** @brief Returns the modifier flags. */
                uint8_t modifiers() const { return _modifiers; }

                /** @brief Returns the printable text. */
                const String &text() const { return _text; }

                /** @brief Returns true if the Shift modifier is set. */
                bool isShift() const { return _modifiers & ShiftModifier; }

                /** @brief Returns true if the Ctrl modifier is set. */
                bool isCtrl() const { return _modifiers & CtrlModifier; }

                /** @brief Returns true if the Alt modifier is set. */
                bool isAlt() const { return _modifiers & AltModifier; }

                /** @brief Returns true if the Meta modifier is set. */
                bool isMeta() const { return _modifiers & MetaModifier; }

                /** @brief Returns a human-readable name for the given key code. */
                static String keyName(Key key);

                /** @brief Returns a human-readable string for the given modifier flags (e.g. "Ctrl+Shift+"). */
                static String modifierString(uint8_t modifiers);

        private:
                Key             _key;
                uint8_t         _modifiers;
                String          _text;
};

PROMEKI_NAMESPACE_END
