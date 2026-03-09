/**
 * @file      buffer.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <cstring>
#include <promeki/namespace.h>
#include <promeki/list.h>
#include <promeki/sharedptr.h>
#include <promeki/memspace.h>

PROMEKI_NAMESPACE_BEGIN

class Buffer {
        public:
                static size_t getPageSize();
                static const size_t DefaultAlign;
                
                using List = promeki::List<Buffer>;

                Buffer() : d(SharedPtr<Data, false>::create()) { }
                Buffer(size_t sz, size_t an = DefaultAlign, const MemSpace &ms = MemSpace::Default) :
                        d(SharedPtr<Data, false>::create(sz, an, ms)) { }
                Buffer(void *p, size_t sz, size_t an = 0, bool own = false, const MemSpace &ms = MemSpace::Default) :
                        d(SharedPtr<Data, false>::create(p, sz, an, own, ms)) { }


                bool isValid() const { return d->data != nullptr; }
                void *data() const { return d->data; }
                size_t size() const { return d->size; }
                size_t align() const { return d->align; }
                const MemSpace &memSpace() const { return d->ms; }

                // Shifts the value returned by data() by the given number of bytes.
                // Be careful when using this to make sure you've allocated enough
                // memory to account of the shift size.
                void shiftData(size_t bytes) {
                        d.modify()->data = static_cast<void *>(static_cast<uint8_t *>(d.modify()->data) + bytes);
                        return;
                }
                void setOwnershipEnabled(bool val) {
                        d.modify()->owned = val;
                        return;
                }

                bool fill(char value) const {
                        return d->fill(value);
                }

                int referenceCount() const { return d.referenceCount(); }

        private:
                class Data {
                        PROMEKI_SHARED_FINAL(Data)
                        public:
                                MemSpace        ms;
                                void            *data           = nullptr;
                                void            *odata          = nullptr;
                                size_t          size            = 0;
                                size_t          align           = 0;
                                bool            owned           = true;

                                Data() = default;

                                Data(void *p, size_t s, size_t an, bool own, const MemSpace &m) :
                                        ms(m), data(p), odata(p), size(s), align(an), owned(own) { }

                                Data(size_t sz, size_t an, const MemSpace &m) : ms(m), size(sz), align(an) {
                                        odata = data = ms.alloc(size, align);
                                }

                                Data(const Data &o) : ms(o.ms), size(o.size), align(o.align), owned(true) {
                                        if(o.data != nullptr) {
                                                odata = data = ms.alloc(size, align);
                                                size_t shift = static_cast<uint8_t *>(o.data) - static_cast<uint8_t *>(o.odata);
                                                std::memcpy(odata, o.odata, size + shift);
                                                data = static_cast<uint8_t *>(odata) + shift;
                                        }
                                }

                                ~Data() {
                                        if(owned) ms.release(odata);
                                }

                                bool fill(char value) const {
                                        return ms.set(data, size, value);
                                }
                };

                SharedPtr<Data, false> d;

};

PROMEKI_NAMESPACE_END

