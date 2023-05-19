/*****************************************************************************
 * audio.h
 * May 17, 2023
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
#include <promeki/shareddata.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

class Audio {
        public:
                Audio() : d(new Data) {}
                Audio(const AudioDesc &desc, size_t samples, 
                      const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(desc, samples, ms)) {}

                bool isValid() const {
                        return d->isValid();
                }

                bool isNative() const {
                        return d->desc.isNative();
                }

                const AudioDesc &desc() const {
                        return d->desc;
                }

                size_t samples() const {
                        return d->samples;
                }

                size_t maxSamples() const {
                        return d->maxSamples;
                }

                size_t frames() const {
                        return d->samples * d->desc.channels();
                }

                const Buffer &buffer() const {
                        return d->buffer;
                }

                Buffer &buffer() {
                        return d->buffer;
                }

                void zero() const {
                        d->buffer.fill(0);
                        return;
                }

                bool resize(size_t val) {
                        return d->resize(val);
                }

                template <typename T> T *data() const {
                        return reinterpret_cast<T *>(d->buffer.data());
                }

        private:
                class Data : public SharedData {
                        public:
                                Buffer                  buffer;
                                AudioDesc               desc;
                                size_t                  samples = 0;
                                size_t                  maxSamples = 0;

                                Data() = default;
                                Data(const AudioDesc &d, size_t s, const MemSpace &ms) : 
                                desc(d), samples(s), maxSamples(s) {
                                        allocate(ms);
                                }

                                bool isValid() const {
                                        return desc.isValid();
                                }

                                bool resize(size_t val) {
                                        if(val > maxSamples) return false;
                                        samples = val;
                                        return true;
                                }

                        private:
                                bool allocate(const MemSpace &ms);
                };

                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END


