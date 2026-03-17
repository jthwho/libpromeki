/**
 * @file      proav/audio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/core/namespace.h>
#include <promeki/core/sharedptr.h>
#include <promeki/proav/audiodesc.h>
#include <promeki/core/buffer.h>
#include <promeki/core/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Object to hold some number of audio samples.
 * @ingroup proav_media
 *
 * This object is meant to hold some number of audio samples
 * described by an AudioDesc. The sample data is stored in a
 * shared Buffer. When shared ownership is needed, use Audio::Ptr.
 *
 * @par Example
 * @code
 * AudioDesc desc(48000, 2, AudioDesc::Float32);
 * Audio audio(desc, 1024);  // 1024 samples
 *
 * // Shared ownership for pipeline passing
 * Audio::Ptr shared = Audio::Ptr::create(desc, 1024);
 * @endcode
 */
class Audio {
        PROMEKI_SHARED_FINAL(Audio)
        public:
                /** @brief Shared pointer type for Audio. */
                using Ptr = SharedPtr<Audio>;

                /** @brief List of Audio values. */
                using List = promeki::List<Audio>;

                /** @brief List of shared Audio pointers. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an invalid Audio object. */
                Audio() = default;

                /**
                 * @brief Constructs an Audio object for the given descriptor and sample count.
                 * @param desc    Audio descriptor specifying format, sample rate, and channels.
                 * @param samples Number of samples to allocate.
                 * @param ms      Memory space to allocate the buffer from.
                 */
                Audio(const AudioDesc &desc, size_t samples,
                      const MemSpace &ms = MemSpace::Default);

                /**
                 * @brief Returns true if the object is valid.
                 * @return true if the audio descriptor is valid.
                 */
                bool isValid() const {
                        return _desc.isValid();
                }

                /**
                 * @brief Returns true if the audio is in the native float32 format for this system.
                 * @return true if the format is native float32.
                 */
                bool isNative() const {
                        return _desc.isNative();
                }

                /**
                 * @brief Returns the AudioDesc that describes the audio contained in this object.
                 * @return A const reference to the AudioDesc.
                 */
                const AudioDesc &desc() const {
                        return _desc;
                }

                /**
                 * @brief Returns the number of samples, irrespective of the channel count.
                 * @return The number of samples.
                 */
                size_t samples() const {
                        return _samples;
                }

                /**
                 * @brief Returns the maximum number of samples this object can contain.
                 * @return The maximum sample count.
                 */
                size_t maxSamples() const {
                        return _maxSamples;
                }

                /**
                 * @brief Returns the total number of sample frames (samples times channels).
                 * @return The frame count.
                 */
                size_t frames() const {
                        return _samples * _desc.channels();
                }

                /**
                 * @brief Returns a const reference to the buffer that holds the audio data.
                 * @return A const reference to the Buffer shared pointer.
                 */
                const Buffer::Ptr &buffer() const {
                        return _buffer;
                }

                /**
                 * @brief Returns a mutable reference to the buffer shared pointer.
                 * @return A mutable reference to the Buffer shared pointer.
                 */
                Buffer::Ptr &buffer() {
                        return _buffer;
                }

                /** @brief Zeros out all the audio data in the buffer. */
                void zero() const {
                        _buffer->fill(0);
                        return;
                }

                /**
                 * @brief Converts this Audio to a different sample format.
                 *
                 * Returns a new Audio object with the same sample rate, channel count,
                 * and sample count but with audio data converted to the target format.
                 * When the source is native float, this is a single-pass conversion.
                 *
                 * @param format The target AudioDesc::DataType.
                 * @return A new Audio object in the target format, or an invalid Audio on failure.
                 */
                Audio convertTo(AudioDesc::DataType format) const;

                /**
                 * @brief Resizes the sample count to a value between 0 and maxSamples().
                 * @param val The new sample count.
                 * @return true if successful, false if val exceeds maxSamples().
                 */
                bool resize(size_t val) {
                        if(val > _maxSamples) return false;
                        _samples = val;
                        return true;
                }

                /**
                 * @brief Returns the audio data pointer cast to the given type.
                 * @tparam T The sample data type to cast to (e.g. float, int16_t).
                 * @return A typed pointer to the audio sample data.
                 */
                template <typename T> T *data() const {
                        return reinterpret_cast<T *>(_buffer->data());
                }

        private:
                Buffer::Ptr     _buffer;
                AudioDesc       _desc;
                size_t          _samples = 0;
                size_t          _maxSamples = 0;

                bool allocate(const MemSpace &ms);
};

PROMEKI_NAMESPACE_END
