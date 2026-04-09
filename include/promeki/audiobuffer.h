/**
 * @file      audiobuffer.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/audiodesc.h>
#include <promeki/audio.h>
#include <promeki/buffer.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Ring-buffered audio sample FIFO with format conversion on push.
 * @ingroup proav
 *
 * AudioBuffer stores interleaved PCM samples in a fixed output format.
 * Callers push audio in any compatible input format (differing bit
 * depth, endianness, integer vs float are handled automatically) and
 * pop samples in the storage format. The buffer is designed as the
 * efficient bridge between an Audio producer that works in one format
 * (e.g. native float at the video frame rate) and a consumer that
 * wants a different framing or format (e.g. a container writer that
 * writes fixed-size chunks of s16 little-endian).
 *
 * @par Format conversion
 *
 * When @c inputFormat() differs from @c format() only in @c DataType
 * (bit depth / endian / float vs int), @c push() converts on the fly
 * via AudioDesc's @c samplesToFloat / @c floatToSamples helpers.
 *
 * When the input and output @b sample @b rates or @b channel @b counts
 * differ, @c push() returns @c Error::NotSupported. The resampler /
 * channel-map hook will land in a follow-up — this class is
 * pre-plumbed for it.
 *
 * @par Capacity and ownership
 *
 * The buffer is sized via @c reserve() (or constructor). Pushes that
 * would exceed capacity return @c Error::NoSpace — the caller is
 * expected to size the buffer for their expected peak residency. The
 * class is move-only; copying a FIFO is almost always a mistake.
 *
 * @par Example
 * @code
 * AudioBuffer fifo(AudioDesc(AudioDesc::PCMI_S16LE, 48000, 2));
 * fifo.reserve(48000);                    // 1 second of headroom
 * fifo.setInputFormat(AudioDesc::NativeType, 48000, 2);
 *
 * // Producer side
 * Audio chunk(nativeDesc, 1602);
 * // ... fill chunk ...
 * fifo.push(chunk);
 *
 * // Consumer side
 * Audio slice(outputDesc, 1600);
 * size_t got = fifo.pop(slice, 1600);
 * // got is the actual number of samples written to `slice`.
 * @endcode
 */
class AudioBuffer {
        public:
                /** @brief Default-constructs an invalid AudioBuffer with no format. */
                AudioBuffer() = default;

                /** @brief Constructs an AudioBuffer with the given storage format. */
                explicit AudioBuffer(const AudioDesc &format);

                /** @brief Constructs an AudioBuffer with storage format and reserved capacity. */
                AudioBuffer(const AudioDesc &format, size_t capacity);

                AudioBuffer(const AudioBuffer &) = delete;
                AudioBuffer &operator=(const AudioBuffer &) = delete;

                /** @brief Move constructor. */
                AudioBuffer(AudioBuffer &&other) noexcept;

                /** @brief Move assignment. */
                AudioBuffer &operator=(AudioBuffer &&other) noexcept;

                /** @brief Destructor. */
                ~AudioBuffer() = default;

                /** @brief Returns true if a valid storage format is set. */
                bool isValid() const { return _format.isValid(); }

                /** @brief Returns the storage (output) format. */
                const AudioDesc &format() const { return _format; }

                /**
                 * @brief Sets the storage format. Clears any buffered samples.
                 * @param format The new storage format.
                 */
                void setFormat(const AudioDesc &format);

                /** @brief Returns the expected input format for @c push(). */
                const AudioDesc &inputFormat() const { return _inputFormat; }

                /**
                 * @brief Sets the expected input format for @c push().
                 *
                 * If @p input differs from @c format() only in @c DataType,
                 * @c push() converts on-the-fly via @c samplesToFloat /
                 * @c floatToSamples. If the sample rate or channel count
                 * differs, @c push() returns @c NotSupported (resampler /
                 * channel-map hook TBD).
                 */
                void setInputFormat(const AudioDesc &input);

                /**
                 * @brief Ensures capacity for at least @p samples samples.
                 *
                 * If the current capacity is already sufficient this is a
                 * no-op. Growing the buffer linearizes any current contents
                 * (moving head to index 0).
                 */
                Error reserve(size_t samples);

                /** @brief Returns the current capacity in samples. */
                size_t capacity() const { return _capacity; }

                /** @brief Returns the number of samples currently buffered. */
                size_t available() const { return _count; }

                /** @brief Returns the free capacity in samples. */
                size_t free() const { return _capacity - _count; }

                /** @brief Returns true if no samples are buffered. */
                bool isEmpty() const { return _count == 0; }

                /** @brief Returns true if the buffer is full. */
                bool isFull() const { return _count >= _capacity; }

                /** @brief Clears all buffered samples. */
                void clear();

                /**
                 * @brief Pushes @p audio's samples into the buffer.
                 * @return Error::Ok on success, NoSpace if the buffer would
                 *         overflow, NotSupported if @p audio has a
                 *         mismatched sample rate or channel count, or
                 *         InvalidArgument if the format is invalid.
                 */
                Error push(const Audio &audio);

                /**
                 * @brief Pushes interleaved raw samples into the buffer.
                 *
                 * @param data       Pointer to @p samples × bytes-per-sample bytes.
                 * @param samples    Number of samples (not bytes) in @p data.
                 * @param srcFormat  Format of @p data.
                 * @return Error::Ok, NoSpace, NotSupported, or InvalidArgument.
                 */
                Error push(const void *data, size_t samples, const AudioDesc &srcFormat);

                /**
                 * @brief Pops up to @p samples samples into @p audio.
                 *
                 * The destination @p audio must have a descriptor matching
                 * @c format() and @c maxSamples() >= @p samples. On return,
                 * @p audio's sample count is set to the actual number popped.
                 *
                 * @return The number of samples actually popped.
                 */
                size_t pop(Audio &audio, size_t samples);

                /**
                 * @brief Pops up to @p samples samples into a raw buffer.
                 *
                 * @param dst     Destination buffer, must be at least
                 *                @p samples × bytes-per-sample bytes.
                 * @param samples Number of samples to pop.
                 * @return The number of samples actually popped.
                 */
                size_t pop(void *dst, size_t samples);

                /**
                 * @brief Peeks at the next @p samples samples without consuming.
                 */
                size_t peek(void *dst, size_t samples) const;

                /**
                 * @brief Drops the next @p samples samples without copying.
                 * @return The number of samples actually dropped.
                 */
                size_t drop(size_t samples);

        private:
                /** @brief Bytes per sample frame (all channels combined). */
                size_t bytesPerSample() const;

                /** @brief Writes @p samples samples (already in storage format) starting at _tail. */
                void   writeBytesAtTail(const uint8_t *data, size_t samples);

                /** @brief Reads @p samples samples from @p startSample into @p dst. */
                void   readBytesFromHead(uint8_t *dst, size_t samples, size_t skip) const;

                AudioDesc _format;
                AudioDesc _inputFormat;
                Buffer    _storage;
                size_t    _capacity = 0;
                size_t    _head = 0;      ///< Next sample index to pop (mod _capacity).
                size_t    _tail = 0;      ///< Next sample index to push (mod _capacity).
                size_t    _count = 0;     ///< Currently buffered samples.
};

PROMEKI_NAMESPACE_END
