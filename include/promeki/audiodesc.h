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
                struct Format {
                        int             id;
                        String          name;
                        String          desc;
                        size_t          bytesPerSample;
                        size_t          bitsPerSample;
                        bool            isSigned;
                        bool            isPlanar;
                        bool            isBigEndian;
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
                };

                SharedDataPtr<Data> d;
};

PROMEKI_NAMESPACE_END


