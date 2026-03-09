/**
 * @file      memspace.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/string.h>

PROMEKI_NAMESPACE_BEGIN

class MemSpace {
        public:
                enum ID {
                        System = 0,
                        Default = System
                };

                struct Ops {
                        ID id;
                        String name;
                        void *(*alloc)(size_t bytes, size_t align);
                        void (*release)(void *ptr);
                        bool (*copy)(ID toSpace, void *to, const void *from, size_t bytes);
                        bool (*set)(void *to, size_t bytes, char value);
                };

                MemSpace(ID id = Default) : d(lookup(id)) { }

                void *alloc(size_t bytes, size_t align) const {
                        return d->alloc(bytes, align);
                }

                void release(void *ptr) const {
                        if(ptr == nullptr) return;
                        d->release(ptr);
                        return;
                }
                
                bool copy(ID toSpace, void *to, const void *from, size_t bytes) const {
                        if(to == nullptr || from == nullptr) return false;
                        return d->copy(toSpace, to, from, bytes);
                }

                bool set(void *to, size_t bytes, char value) const {
                        if(to == nullptr) return false;
                        return d->set(to, bytes, value);
                }

        private:
                const Ops *d = nullptr;
                static const Ops *lookup(ID id);
};

PROMEKI_NAMESPACE_END


