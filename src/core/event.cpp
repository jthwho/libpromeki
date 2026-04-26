/**
 * @file      event.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/event.h>
#include <promeki/atomic.h>

PROMEKI_NAMESPACE_BEGIN

static Atomic<Event::Type> _nextType{1};

Event::Type Event::registerType() {
        return _nextType.fetchAndAdd(1);
}

const Event::Type Event::Timer = Event::registerType();
const Event::Type Event::DeferredCall = Event::registerType();
const Event::Type Event::SignalEvent = Event::registerType();
const Event::Type Event::Quit = Event::registerType();

PROMEKI_NAMESPACE_END
