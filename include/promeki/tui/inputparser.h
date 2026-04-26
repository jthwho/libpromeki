/**
 * @file      tui/inputparser.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/keyevent.h>
#include <promeki/mouseevent.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief State machine for parsing terminal escape sequences into events.
 * @ingroup tui_core
 *
 * Handles CSI sequences (arrows, function keys, mouse), SS3 sequences,
 * UTF-8 multi-byte input, and ambiguous Escape timing.
 *
 * @par Thread Safety
 * Conditionally thread-safe.  Distinct instances may be used concurrently;
 * concurrent access to a single instance must be externally synchronized.
 * Typically owned by the TUI thread that drives @ref TuiSubsystem.
 */
class TuiInputParser {
        public:
                /** @brief Parsed event variant. */
                struct ParsedEvent {
                        enum Type { None, Key, Mouse };
                        Type            type = None;
                        KeyEvent::Key   key = KeyEvent::Key_Unknown;
                        uint8_t         modifiers = 0;
                        String          text;
                        // Mouse fields
                        Point2Di32         mousePos;
                        MouseEvent::Button      mouseButton = MouseEvent::NoButton;
                        MouseEvent::Action      mouseAction = MouseEvent::Press;
                        uint8_t                 mouseButtons = 0; ///< Bitmask of all held buttons.
                };

                TuiInputParser();

                /**
                 * @brief Feeds raw input bytes into the parser.
                 * @param data The raw bytes.
                 * @param len  Number of bytes.
                 * @return List of parsed events.
                 */
                List<ParsedEvent> feed(const char *data, int len);

        private:
                enum State {
                        Normal,
                        EscapeStart,
                        CSI,
                        SS3,
                        CSIParam,
                        MouseSGR
                };

                State           _state = Normal;
                String          _buf;
                uint8_t         _buttonState = 0; ///< Accumulated button press state.

                void parseCSI(const String &seq, List<ParsedEvent> &events);
                void parseSS3(char ch, List<ParsedEvent> &events);
                void parseMouseSGR(const String &seq, List<ParsedEvent> &events);
                static KeyEvent::Key csiToKey(int code, int modifier);
};

PROMEKI_NAMESPACE_END
