/**
 * @file      event.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/event.h>
#include <promeki/atomic.h>
#include <promeki/hashmap.h>
#include <promeki/mutex.h>

PROMEKI_NAMESPACE_BEGIN

static Atomic<Event::Type> _nextType{1};

namespace {

// The name registry is intentionally allocated as a Meyers singleton
// so a registerType() call from another translation unit's static
// initializer cannot race with construction order — the first call
// constructs both the mutex and the map, subsequent calls reuse them.
struct NameRegistry {
                Mutex                       mutex;
                HashMap<Event::Type, String> names;
};

static NameRegistry &registry() {
        static NameRegistry r;
        return r;
}

} // namespace

Event::Type Event::registerType() {
        return _nextType.fetchAndAdd(1);
}

Event::Type Event::registerType(const String &name) {
        Type        id = _nextType.fetchAndAdd(1);
        NameRegistry &r = registry();
        Mutex::Locker lock(r.mutex);
        r.names.insert(id, name);
        return id;
}

String Event::typeName(Type type) {
        if (type == InvalidType) return String();
        NameRegistry &r = registry();
        Mutex::Locker lock(r.mutex);
        auto          it = r.names.find(type);
        if (it == r.names.end()) return String();
        return it->second;
}

const Event::Type Event::Timer = Event::registerType("Timer");
const Event::Type Event::DeferredCall = Event::registerType("DeferredCall");
const Event::Type Event::SignalEvent = Event::registerType("Signal");
const Event::Type Event::Quit = Event::registerType("Quit");

PROMEKI_NAMESPACE_END
