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

#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/signal.h>
#include <promeki/util.h>

PROMEKI_NAMESPACE_BEGIN

class ObjectBase;

using ObjectBaseList = List<ObjectBase *>;

class ObjectBase {
        public:
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

        private:
                ObjectBase              *_parent = nullptr;
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
