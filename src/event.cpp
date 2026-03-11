/**
 * @file      event.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/event.h>

PROMEKI_NAMESPACE_BEGIN

static std::atomic<Event::Type> _nextType{1};

Event::Type Event::registerType() {
        return _nextType.fetch_add(1, std::memory_order_relaxed);
}

const Event::Type Event::Timer       = Event::registerType();
const Event::Type Event::DeferredCall = Event::registerType();
const Event::Type Event::SignalEvent = Event::registerType();
const Event::Type Event::Quit        = Event::registerType();

PROMEKI_NAMESPACE_END
