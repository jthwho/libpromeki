/*****************************************************************************
 * signal.h
 * May 19, 2023
 *
 * Copyright 2023 - Howard Logic
 * https://howardlogic.com
 * All Rights Reserved
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *
 *****************************************************************************/

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_SIGNAL(name, ...) \
        Signal<__VA_ARGS__> name = Signal<__VA_ARGS__>(PROMEKI_STRINGIFY(name) "(" PROMEKI_STRINGIFY(__VA_ARGS__) ")", this);

PROMEKI_DEBUG(Signal)

class ObjectBase;

template <typename... Args> class Signal {
        public:
                using Prototype = void(Args...);
                using PrototypeFunc = std::function<Prototype>;
                using PrototypeFuncList = List<PrototypeFunc>;
                
                Signal(const char *n, ObjectBase *o) : _name(n), _owner(o) {
                        promekiDebug("%s [%p] Signal(%p)", _name, this, _owner);
                }

                const char *name() const { return _name; }

                // Allows you to connect this signal to a normal lambda
                size_t connect(PrototypeFunc slot) {
                        size_t slotID = _slots.size();
                        _slots += Info(slot);
                        promekiDebug("%s [%p] connect() = %d", _name, this, (int)slotID);
                        return slotID;
                }

                // Allows you to connect this signal to an object member function that matches
                // this signal's argument prototype.
                template <typename T> size_t connect(T* obj, void (T::*memberFunction)(Args...)) {
                        size_t slotID = _slots.size();
                        _slots += Info(
                                ([obj, memberFunction](Args... args) {
                                        (obj->*memberFunction)(args...);
                                }), obj
                        );
                        obj->addCleanupFunc([&](void *object) { disconnectFromObject(object); });
                        promekiDebug("%s [%p] connect(%p) = %d", _name, this, obj, (int)slotID);
                        return slotID;
                }

                void disconnect(size_t slotID) {
                        _slots.remove(slotID);
                        promekiDebug("%s [%p] disconnect(%d), %d left", _name, this, (int)slotID, (int)_slots.size());
                        return;
                }

                template <typename T> void disconnect(T* object, void (T::*memberFunction)(Args...)) {
                        _slots.removeIf([object, memberFunction](const Info &info) { 
                                        return info.object == object && info.func == memberFunction; 
                        });
                        promekiDebug("%s [%p] disconnect(%p), %d left", _name, this, object, (int)_slots.size());
                        return;
                }

                void disconnectFromObject(void *object) {
                        _slots.removeIf([object](const Info &info) { return info.object == object; });
                        promekiDebug("%s [%p] disconnectFromObject(%p), %d left", _name, this, object, (int)_slots.size());
                        return;
                }

                // Calls all the connected functions
                void emit(Args... args) {
                        promekiDebug("%s [%p] emit", _name, this);
                        for (const auto &slot : _slots) slot.func(args...);
                        return;
                }

        private:
                class Info {
                        public:
                                PrototypeFunc   func;
                                void            *object = nullptr;

                                Info(PrototypeFunc f, void *obj = nullptr) : func(f), object(obj) {}
                };

                const char              *_name;
                ObjectBase              *_owner;
                List<Info>              _slots;
};

PROMEKI_NAMESPACE_END

