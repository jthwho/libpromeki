/**
 * @file      windowfocusevent.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <promeki/event.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Event delivered when the terminal window gains or loses focus.
 * @ingroup util
 *
 * Produced from the terminal's focus in/out reporting (xterm mode 1004,
 * decoded by @ref TuiInputParser) and dispatched through the normal widget
 * @c event() path.  Unlike @ref Widget::focusInEvent / focusOutEvent — which
 * concern *keyboard* focus moving between widgets — this reports the whole
 * window/terminal gaining or losing focus from the windowing system, letting
 * an application pause animations, dim a selection, or stop polling while it
 * is in the background.
 *
 * Registered via Event::registerType().
 */
class WindowFocusEvent : public Event {
        public:
                /** @brief Event type ID for WindowFocusEvent. */
                static const Type WindowFocus;

                /**
                 * @brief Constructs a WindowFocusEvent.
                 * @param gained True if the window gained focus, false if it lost focus.
                 */
                explicit WindowFocusEvent(bool gained) : Event(WindowFocus), _gained(gained) {}

                /** @brief Returns true if the window gained focus. */
                bool gained() const { return _gained; }

                /** @brief Returns true if the window lost focus. */
                bool lost() const { return !_gained; }

        private:
                bool _gained;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
