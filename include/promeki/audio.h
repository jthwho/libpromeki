/** 
 * @file audio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the source root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/shareddata.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Object to hold some number of audio samples
 * @ingroup DataObject
 *
 * This object is meant to hold some number of audio samples.  It
 * uses the SharedData and wrapper scheme to ensure fast shallow
 * copying of data and copy-on-write semantics.
 */
class Audio {
        public:
                /**
                 * @brief Constructs an invalid Audio object */
                Audio() : d(new Data) {}

                /**
                 * @brief Constructs an Audio object for given AudioDesc and samples.
                 */
                Audio(const AudioDesc &desc, size_t samples, 
                      const MemSpace &ms = MemSpace::Default) : 
                        d(new Data(desc, samples, ms)) {}

                /**
                 * @brief Returns true if the object is valid */
                bool isValid() const {
                        return d->isValid();
                }

                /**
                 * @brief Returns true if the object is in the native float32 format for this system
                 */
                bool isNative() const {
                        return d->desc.isNative();
                }

                /**
                 * @brief Returns the AudioDesc that describes the audio contained in this object
                 */
                const AudioDesc &desc() const {
                        return d->desc;
                }

                /**
                 * @brief Returns the number of samples, irrespective of the channels
                 */
                size_t samples() const {
                        return d->samples;
                }

                /**
                 * @brief Returns the maximum number of samples this object can contain
                 */
                size_t maxSamples() const {
                        return d->maxSamples;
                }

                /**
                 * @brief Returns the number number of audio samples times the number of channels
                 */
                size_t frames() const {
                        return d->samples * d->desc.channels();
                }

                /**
                 * @brief Returns a const reference to the buffer that holds the audio data
                 */
                const Buffer &buffer() const {
                        return d->buffer;
                }

                /**
                 * @brief Returns a reference to the buffer that holds the audio data
                 */
                Buffer &buffer() {
                        return d->buffer;
                }

                /**
                 * @brief Zeros out all the audio data
                 */
                void zero() const {
                        d->buffer.fill(0);
                        return;
                }

                /**
                 * @brief Resizes the audio samples to a value between 0 and maxSamples()
                 * @return True if successful, false if out of range.
                 */
                bool resize(size_t val) {
                        return d->resize(val);
                }

                /**
                 * @brief Returns the audio data pointer in the template format given
                 */
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



