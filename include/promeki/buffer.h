/*****************************************************************************
 * buffer.h
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

#include <promeki/shareddata.h>
#include <promeki/memspace.h>

namespace promeki {

class Buffer {
        public:
                const static size_t DefaultAlign = 4096; // FIXME: Use the system page size

                class Data : public SharedData {
                        public:
                                MemSpace        ms;
                                void            *data           = nullptr;
                                size_t          size            = 0;
                                size_t          align           = 0;
                                bool            owned           = true;

                                Data() = default;
                                
                                Data(void *p, size_t s, size_t an, bool own, const MemSpace &m) :
                                        ms(m), data(p), size(s), align(an), owned(own) { }

                                Data(size_t sz, size_t an, const MemSpace &m) : ms(m), size(sz), align(an) { 
                                        data = ms.alloc(size, align);
                                }

                                ~Data() {
                                        if(owned) ms.release(data);
                                }

                                bool fill(char value) const {
                                        return ms.set(data, size, value);
                                }
                };

                Buffer() : d(new Data()) { }
                Buffer(size_t sz, size_t an = DefaultAlign, const MemSpace &ms = MemSpace::Default) :
                        d(new Data(sz, an, ms)) { }
                Buffer(void *p, size_t sz, size_t an = 0, bool own = false, const MemSpace &ms = MemSpace::Default) :
                        d(new Data(p, sz, an, own, ms)) { }


                bool isValid() const { return d->data != nullptr; }
                void *data() const { return d->data; }
                size_t size() const { return d->size; }
                size_t align() const { return d->align; }
                const MemSpace &memSpace() const { return d->ms; }

                void setOwnershipEnabled(bool val) {
                        d->owned = val;
                        return;
                }

                bool fill(char value) const {
                        return d->fill(value);
                }

        private:
                ExplicitSharedDataPtr<Data> d;

};

} // namespace promeki