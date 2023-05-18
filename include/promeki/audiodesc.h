/*****************************************************************************
 * audiodesc.h
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
#include <promeki/metadata.h>
#include <promeki/string.h>
#include <promeki/system.h>

PROMEKI_NAMESPACE_BEGIN

class AudioDesc {
        public:
                static const int32_t MinS24 = -8388608;
                static const int32_t MaxS24 = 8388607;
                static const int32_t MinU24 = 0;
                static const int32_t MaxU24 = 16777215;

                template <typename IntegerType, IntegerType Min, IntegerType Max> 
                static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        constexpr float min = static_cast<float>(Min);
                        constexpr float max = static_cast<float>(Max);
                        return((static_cast<float>(value) - min) * 2.0f / (max - min)) - 1.0f;
                }

                template <typename IntegerType> 
                static float integerToFloat(IntegerType value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return integerToFloat<IntegerType,
                               std::numeric_limits<IntegerType>::min(),
                               std::numeric_limits<IntegerType>::max()>(value);
                }

                template <typename IntegerType, IntegerType Min, IntegerType Max> 
                static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const float min = static_cast<float>(Min);
                        const float max = static_cast<float>(Max);
                        if(value <= -1.0f) return Min;
                        else if(value >= 1.0f) return Max;
                        return static_cast<IntegerType>((value + 1.0f) * 0.5f * (max - min) + min);
                }

                template <typename IntegerType> 
                static IntegerType floatToInteger(float value) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        return floatToInteger<IntegerType, 
                               std::numeric_limits<IntegerType>::min(), 
                               std::numeric_limits<IntegerType>::max()>(value);
                }

                template <typename IntegerType, bool InputIsBigEndian> 
                static void samplesToFloat(float *out, const uint8_t *inbuf, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        const IntegerType *in = reinterpret_cast<const IntegerType *>(inbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                IntegerType val = *in++;
                                if constexpr (InputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = integerToFloat<IntegerType>(val);
                        }
                        return;
                }

                template <typename IntegerType, bool OutputIsBigEndian> 
                static void floatToSamples(uint8_t *outbuf, const float *in, size_t samples) {
                        static_assert(std::is_integral<IntegerType>::value, "IntegerType must be an integer.");
                        IntegerType *out = reinterpret_cast<IntegerType *>(outbuf);
                        for(size_t i = 0; i < samples; ++i) {
                                IntegerType val = floatToInteger<IntegerType>(*in++);
                                if constexpr (OutputIsBigEndian != System::isBigEndian()) System::swapEndian(val);
                                *out++ = val;
                        }
                        return;
                }

                struct Format {
                        int             id;
                        String          name;
                        String          desc;
                        size_t          bytesPerSample;
                        size_t          bitsPerSample;
                        bool            isSigned;
                        bool            isPlanar;
                        bool            isBigEndian;
                        void (*samplesToFloat)(float *out, const uint8_t *in, size_t samples);
                        void (*floatToSamples)(uint8_t *out, const float *in, size_t samples);
                };

                static const Format *lookupFormat(int id);

                // PCMI = PCM audio with interleaved channels
                // PCMP = PCM audio with planar channels
                enum DataType {
                        Invalid = 0,
                        PCMI_Float32LE,
                        PCMI_Float32BE,
                        PCMI_S8,
                        PCMI_U8,
                        PCMI_S16LE,
                        PCMI_U16LE,
                        PCMI_S16BE,
                        PCMI_U16BE,
                        PCMI_S24LE,
                        PCMI_U24LE,
                        PCMI_S24BE,
                        PCMI_U24BE,
                        PCMI_S32LE,
                        PCMI_U32LE,
                        PCMI_S32BE,
                        PCMI_U32BE
                };

                static constexpr DataType NativeType = System::isLittleEndian() ? PCMI_Float32LE : PCMI_Float32BE;

                AudioDesc() : d(new Data) { }
                AudioDesc(DataType dt, float sr, size_t ch) : d(new Data(dt, sr, ch)) { }

                bool isValid() const {
                        return d->isValid();
                }

                String toString() const {
                        return d->toString();
                }

                size_t bytesPerSample() const {
                        return d->format->bytesPerSample;
                }

                size_t bytesPerSampleStride() const {
                        return d->bytesPerSampleStride();
                }

                size_t channelBufferOffset(size_t chan, size_t bufferSamples) const {
                        return d->channelBufferOffset(chan, bufferSamples);
                }

                size_t bufferSize(size_t samples) const {
                        return d->bufferSize(samples);
                }

                DataType dataType() const {
                        return (DataType)d->dataType;
                }

                void setDataType(DataType val) {
                        d->dataType = val;
                        return;
                }

                float sampleRate() const {
                        return d->sampleRate;
                }

                void setSampleRate(float val) {
                        d->sampleRate = val;
                        return;
                }

                size_t channels() const {
                        return d->channels;
                }

                void setChannels(size_t val) {
                        d->channels = val;
                        return;
                }

                const Metadata &metadata() const {
                        return d->metadata;
                }

                Metadata &metadata() {
                        return d->metadata;
                }

                void samplesToFloat(float *out, const uint8_t *in, size_t samples) const {
                        d->samplesToFloat(out, in, samples);
                        return;
                }

                void floatToSamples(uint8_t *out, const float *in, size_t samples) const {
                        d->floatToSamples(out, in, samples);
                        return;
                }


        private:
                class Data : public SharedData {
                        public:
                                int                     dataType = 0;
                                float                   sampleRate = 0.0f;
                                size_t                  channels = 0;
                                Metadata                metadata;
                                const Format            *format = nullptr;

                                Data() : format(lookupFormat(Invalid)) {}
                                Data(int dt, float sr, size_t c) : 
                                        dataType(dt), sampleRate(sr), 
                                        channels(c), format(lookupFormat(dt)) 
                                {
                                        if(!isValid()) {
                                                dataType = Invalid;
                                                sampleRate = 0.0;
                                                channels = 0;
                                                format = lookupFormat(Invalid);
                                        }
                                }

                                bool isValid() const {
                                        return dataType != 0 && sampleRate > 0.0 && channels > 0;
                                }
                                
                                String toString() const {
                                        return String::sprintf("[%s %fHz %dc]", 
                                                format->name.cstr(), sampleRate, (int)channels);
                                }

                                size_t bytesPerSampleStride() const {
                                        return format->isPlanar ? 
                                                format->bytesPerSample : 
                                                format->bytesPerSample * channels;
                                }

                                size_t channelBufferOffset(size_t chan, size_t bufferSamples) const {
                                        return format->isPlanar ? 
                                                format->bytesPerSample * bufferSamples * chan :
                                                format->bytesPerSample * chan;
                                }

                                size_t bufferSize(size_t samples) const {
                                        return format->bytesPerSample * channels * samples;
                                }

                                void samplesToFloat(float *out, const uint8_t *in, size_t samples) const {
                                        format->samplesToFloat(out, in, samples * channels);
                                        return;
                                }

                                void floatToSamples(uint8_t *out, const float *in, size_t samples) const {
                                        format->floatToSamples(out, in, samples * channels);
                                        return;
                                }
                };

                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END


