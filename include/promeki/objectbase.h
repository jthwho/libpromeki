/*****************************************************************************
 * objectbase.h
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

#include <tuple>
#include <functional>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/util.h>
#include <promeki/logger.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

#define PROMEKI_SIGNAL(name, ...) \
        Signal<__VA_ARGS__> name = Signal<__VA_ARGS__>(PROMEKI_STRINGIFY(name) "(" PROMEKI_STRINGIFY(__VA_ARGS__) ")", this);

PROMEKI_DEBUG(ObjectBase)

class ObjectBase;

using ObjectBaseList = List<ObjectBase *>;

class ObjectBase {
        public:
                template <typename... Args> class Signal {
                        public:
                                using Prototype = void(Args...);
                                using PrototypeFunc = std::function<Prototype>;
                                using PrototypeFuncList = List<PrototypeFunc>;
                                using ParamContainer = std::tuple<std::remove_reference_t<Args>...>;

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
                                template <typename T> size_t connect(T *obj, void (T::*memberFunction)(Args...)) {
                                        size_t slotID = _slots.size();
                                        _slots += Info(
                                                        ([obj, memberFunction](Args... args) {
                                                         (obj->*memberFunction)(args...);
                                                         }), obj
                                                      );
                                        obj->addCleanupFunc([&](ObjectBase *object) { disconnectFromObject(object); });
                                        promekiDebug("%s [%p] connect(%p) = %d", _name, this, obj, (int)slotID);
                                        return slotID;
                                }

                                void disconnect(size_t slotID) {
                                        _slots.remove(slotID);
                                        promekiDebug("%s [%p] disconnect(%d), %d left", _name, this, (int)slotID, (int)_slots.size());
                                        return;
                                }

                                template <typename T> void disconnect(T *object, void (T::*memberFunction)(Args...)) {
                                        _slots.removeIf([object, memberFunction](const Info &info) { 
                                                        return info.object == object && info.func == memberFunction; 
                                                        });
                                        promekiDebug("%s [%p] disconnect(%p), %d left", _name, this, object, (int)_slots.size());
                                        return;
                                }

                                void disconnectFromObject(ObjectBase *object) {
                                        _slots.removeIf([object](const Info &info) { return info.object == object; });
                                        promekiDebug("%s [%p] disconnectFromObject(%p), %d left", _name, this, object, (int)_slots.size());
                                        return;
                                }

                                // Calls all the connected functions
                                void emit(Args... args) {
                                        promekiDebug("%s [%p] emit", _name, this);
                                        for (const auto &slot : _slots) {
                                                ObjectBase *oldSender = slot.object->_signalSender;
                                                slot.object->_signalSender = _owner;
                                                slot.func(args...);
                                                slot.object->_signalSender = oldSender;
                                        }
                                        return;
                                }

                                void packedEmit(const ParamContainer &params) {
                                        promekiDebug("%s [%p] emitFromContainer", _name, this);
                                        for (const auto &slot : _slots) {
                                                ObjectBase *oldSender = slot.object->_signalSender;
                                                slot.object->_signalSender = _owner;
                                                std::apply(slot.func, params);
                                                slot.object->_signalSender = oldSender;
                                        }
                                }

                                // This packs all the signal parameters into ParamContainer object
                                // that can be used to marshal the data to either defer the emit
                                // or pass it as an event to another object (ex: on another thread).
                                // Note, this will also make copies of any arguments passed by
                                // reference as the references probably aren't going to be valid
                                // by the time you want to use the container.
                                ParamContainer packParams(Args&&... args) {
                                        return ParamContainer(std::forward<Args>(args)...);
                                }

                        private:
                                class Info {
                                        public:
                                                PrototypeFunc   func;
                                                ObjectBase      *object = nullptr;

                                                Info(PrototypeFunc f, ObjectBase *obj = nullptr) : func(f), object(obj) {}
                                };

                                const char              *_name;
                                ObjectBase              *_owner;
                                List<Info>              _slots;
                };

                using CleanupFunc = std::function<void(ObjectBase *)>;

                ObjectBase(ObjectBase *p = nullptr) : _parent(p) {
                        if(_parent != nullptr) _parent->addChild(this);
                }

                virtual ~ObjectBase() {
                        aboutToDestroy.emit(this);
                        setParent(nullptr);
                        destroyChildren();
                        runCleanup();
                }

                ObjectBase *parent() const {
                        return _parent;
                }

                void setParent(ObjectBase *p) {
                        if(_parent != nullptr) _parent->removeChild(this);
                        _parent = p;
                        if(_parent != nullptr) _parent->addChild(this);
                        return;
                }

                const ObjectBaseList &children() const {
                        return _children;
                }

                void addCleanupFunc(CleanupFunc func) {
                        _cleanup += func;
                        return;
                }

                PROMEKI_SIGNAL(aboutToDestroy, ObjectBase *);

        protected:
                ObjectBase *signalSender() { return _signalSender; }

        private:
                ObjectBase              *_parent = nullptr;
                ObjectBase              *_signalSender = nullptr;
                ObjectBaseList          _children;
                List<CleanupFunc>       _cleanup;

                void addChild(ObjectBase *c) {
                        _children += c;
                        return;
                }

                void removeChild(ObjectBase *c) {
                        _children.removeFirst(c);
                        return;
                }

                void destroyChildren() {
                        for(auto child : _children) {
                                child->removeChild(this);
                                delete child;
                        }
                        _children.clear();
                        return;
                }

                void runCleanup() {
                        for(auto item : _cleanup) item(this);
                        _cleanup.clear();
                        return;
                }
};

PROMEKI_NAMESPACE_END
