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
                template <typename IntegerType> static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const float min_value = static_cast<float>(std::numeric_limits<IntegerType>::min());
                        const float max_value = static_cast<float>(std::numeric_limits<IntegerType>::max());
                        if (value <= -1.0f) {
                                return std::numeric_limits<IntegerType>::min();
                        } else if (value >= 1.0f) {
                                return std::numeric_limits<IntegerType>::max();
                        } else {
                                return static_cast<IntegerType>((value + 1.0f) * 0.5f * (max_value - min_value) + min_value);
                        }
                }

                template <typename IntegerType> static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const float min_value = static_cast<float>(std::numeric_limits<IntegerType>::min());
                        const float max_value = static_cast<float>(std::numeric_limits<IntegerType>::max());
                        return ((static_cast<float>(value) - min_value) * 2.0f / (max_value - min_value)) - 1.0f;
                }


                template<size_t InputSampleSize, size_t OutputSampleSize, bool InputBigEndian, bool OutputBigEndian> 
                void repackSamples(const uint8_t* input, uint8_t* output, size_t numSamples) {
                        static_assert(InputSampleSize > 0 && OutputSampleSize > 0, "Invalid input or output sample size");
                        const bool endianFlip = InputBigEndian != OutputBigEndian;
                        for (size_t i = 0; i < numSamples; ++i) {
                                if (endianFlip) {
                                        #pragma unroll
                                        for (size_t j = 0; j < OutputSampleSize; ++j) {
                                                const uint8_t *flipInput = input + InputSampleSize - 1;
                                                if (j < InputSampleSize) {
                                                        *output++ = *flipInput--;
                                                } else {
                                                        *output++ = 0;
                                                }
                                        }
                                        input += InputSampleSize;
                                } else {
                                        #pragma unroll
                                        for (size_t j = 0; j < OutputSampleSize; ++j) {
                                                if (j < InputSampleSize) {
                                                        *output++ = *input++;
                                                } else {
                                                        *output++ = 0;
                                                }
                                        }
                                }
                        }
                }

                Audio() : d(new Data) {}
                Audio(const AudioDesc &desc, size_t samples, 
                      const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(desc, samples, ms)) {}

                bool isValid() const {
                        return d->isValid();
                }

                const AudioDesc &desc() const {
                        return d->desc;
                }

                size_t samples() const {
                        return d->samples;
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

        private:
                class Data : public SharedData {
                        public:
                                Buffer                  buffer;
                                AudioDesc               desc;
                                size_t                  samples = 0;

                                Data() = default;
                                Data(const AudioDesc &d, size_t s, const MemSpace &ms) : desc(d), samples(s) {}

                                bool isValid() const {
                                        return desc.isValid();
                                }

                        private:
                                bool allocate(const MemSpace &ms);
                };

                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END


