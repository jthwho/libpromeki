/**
 * @file      audiofile.h
 * @copyright Howard Logic. All rights reserved.
 * 
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
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

                class Impl {
                        PROMEKI_SHARED(Impl)
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

                AudioFile() : d(SharedPtr<Impl>::create(InvalidOperation)) {};
                AudioFile(Impl *impl) : d(SharedPtr<Impl>::takeOwnership(impl)) {};

                bool isValid() const { return d->isValid(); }
                Operation operation() const { return d->operation(); }
                const String &filename() const { return d->filename(); }
                void setFilename(const String &val) { d.modify()->setFilename(val); return; }
                AudioDesc desc() const { return d->desc(); }
                void setDesc(const AudioDesc &val) { d.modify()->setDesc(val); return; }

                Error open() { return d.modify()->open(); }
                void close() { d.modify()->close(); return; }
                Error read(Audio &audio, size_t maxSamples) { return d.modify()->read(audio, maxSamples); }
                Error write(const Audio &audio) { return d.modify()->write(audio); }
                Error seekToSample(size_t sample) { return d.modify()->seekToSample(sample); }
                size_t sampleCount() const { return d->sampleCount(); }

        private:
                SharedPtr<Impl> d;
};

PROMEKI_NAMESPACE_END

