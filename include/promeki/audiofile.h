/*****************************************************************************
 * audiofile.h
 * May 18, 2023
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
#include <promeki/string.h>
#include <promeki/error.h>
#include <promeki/audio.h>

PROMEKI_NAMESPACE_BEGIN

class AudioFile {
        public:
                enum Operation {
                        InvalidOperation = 0,
                        Reader,
                        Writer
                };

                class Impl : public SharedData {
                        public:
                                Impl(Operation op) : _operation(op) {}
                                virtual ~Impl();

                                bool isValid() const {
                                        return _operation != InvalidOperation;
                                }

                                Operation operation() const {
                                        return _operation;
                                }

                                const String &filename() const {
                                        return _filename;
                                }

                                void setFilename(const String &val) {
                                        _filename = val;
                                        return;
                                }

                                AudioDesc desc() const {
                                        return _desc;
                                }

                                void setDesc(const AudioDesc &val) {
                                        _desc = val;
                                        return;
                                }

                                virtual Error open();
                                virtual void close();
                                virtual Error read(Audio &audio, size_t maxSamples);
                                virtual Error write(const Audio &audio);
                                virtual Error seekToSample(size_t sample);
                                virtual size_t sampleCount() const;

                        protected:
                                Operation       _operation;
                                String          _filename;
                                AudioDesc       _desc;
                };

                static AudioFile createForOperation(Operation op, const String &filename);
                static AudioFile createReader(const String &filename) { return createForOperation(Reader, filename); }
                static AudioFile createWriter(const String &filename) { return createForOperation(Writer, filename); }

                AudioFile() : d(new Impl(InvalidOperation)) {};
                AudioFile(Impl *impl) : d(impl) {};

                bool isValid() const { return d->isValid(); }
                Operation operation() const { return d->operation(); }
                const String &filename() const { return d->filename(); }
                void setFilename(const String &val) { d->setFilename(val); return; }
                AudioDesc desc() const { return d->desc(); }
                void setDesc(const AudioDesc &val) { d->setDesc(val); return; }
                
                Error open() { return d->open(); }
                void close() { d->close(); return; }
                Error read(Audio &audio, size_t maxSamples) { return d->read(audio, maxSamples); }
                Error write(const Audio &audio) { return d->write(audio); }
                Error seekToSample(size_t sample) { return d->seekToSample(sample); }
                size_t sampleCount() const { return d->sampleCount(); }

        private:
                SharedDataPtr<Impl> d;
};

PROMEKI_NAMESPACE_END

