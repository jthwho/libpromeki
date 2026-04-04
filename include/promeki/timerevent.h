/**
 * @file      timerevent.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/event.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Event delivered when a timer fires.
 * @ingroup events
 *
 * Carries the timer ID so that the receiver can identify which timer
 * triggered the event.  Delivered to ObjectBase::timerEvent() by the
 * EventLoop.
 */
class TimerEvent : public Event {
        public:
                /**
                 * @brief Constructs a TimerEvent for the given timer.
                 * @param timerId The ID of the timer that fired.
                 */
                TimerEvent(int timerId) : Event(Event::Timer), _timerId(timerId) {}

                /**
                 * @brief Returns the ID of the timer that fired.
                 * @return The timer ID.
                 */
                int timerId() const { return _timerId; }

        private:
                int _timerId;
};

PROMEKI_NAMESPACE_END
