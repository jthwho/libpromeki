/*****************************************************************************
 * memspace.h
 * April 29, 2023
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

#include <promeki/string.h>

namespace promeki {

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
                        bool (*copy)(void *to, const void *from, size_t bytes);
                        bool (*copyToSpace)(ID toSpace, void *to, const void *from, size_t bytes);
                };

                MemSpace(ID id = Default) : d(lookup(id)) { }

                void *alloc(size_t bytes, size_t align) {
                        return d->alloc(bytes, align);
                }

                void release(void *ptr) {
                        d->release(ptr);
                        return;
                }
                
                bool copy(void *to, const void *from, size_t bytes) {
                        return d->copy(to, from, bytes);
                }

                bool copyToSpace(ID toSpace, void *to, const void *from, size_t bytes) {
                        return d->copyToSpace(toSpace, to, from, bytes);
                }

        private:
                const Ops *d = nullptr;
                static const Ops *lookup(ID id);
};

} // namespace promeki

