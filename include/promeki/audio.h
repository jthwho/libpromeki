/**
 * @file      audio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <optional>
#include <type_traits>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Object to hold some number of audio samples.
 * @ingroup proav
 *
 * This object is meant to hold some number of audio samples
 * described by an AudioDesc. The sample data is stored in a
 * shared Buffer. When shared ownership is needed, use Audio::Ptr.
 *
 * @par Compressed audio
 * An Audio object can also carry a compressed bitstream such as
 * AAC, Opus, or MP3. A compressed Audio uses an AudioDesc whose
 * @c codecFourCC() is non-zero (and whose @c dataType() is typically
 * @c Invalid since the encoded stream has no single bit depth). The
 * raw encoded bytes live in the object's single buffer. Use
 * @c isCompressed() to test, @c compressedSize() to get the byte
 * count, and @c Audio::fromBuffer() to adopt an existing
 * @c Buffer::Ptr without copying.
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
                 * @brief Zero-copy factory: adopts @p buffer as the audio's data.
                 *
                 * Used for both compressed bitstreams and PCM where the
                 * caller already has the bytes in a @c Buffer::Ptr (e.g.
                 * from @c File::readBulk). For compressed audio,
                 * @p desc must have a non-zero @c codecFourCC(); for
                 * PCM audio, the buffer size must be a whole multiple
                 * of the PCM frame size (channels × bytesPerSample).
                 *
                 * @param buffer The buffer to adopt.
                 * @param desc   The audio format descriptor (compressed or PCM).
                 * @return A valid @c Audio sharing @p buffer, or an
                 *         invalid @c Audio on failure.
                 */
                static Audio fromBuffer(const Buffer::Ptr &buffer, const AudioDesc &desc);

                /**
                 * @brief Creates a compressed audio object from pre-encoded data.
                 *
                 * Allocates a buffer and copies @p data into it. Mirror
                 * of @c Image::fromCompressedData for audio. Prefer
                 * @c fromBuffer() when the data is already in a
                 * @c Buffer::Ptr to avoid the copy.
                 */
                static Audio fromCompressedData(const void *data, size_t size,
                                                const AudioDesc &desc);

                /**
                 * @brief Returns true if the object is valid.
                 * @return true if the audio descriptor is valid.
                 */
                bool isValid() const {
                        return _desc.isValid();
                }

                /** @brief Returns true if this Audio carries a compressed bitstream. */
                bool isCompressed() const { return _desc.isCompressed(); }

                /**
                 * @brief Returns the number of bytes in the compressed bitstream.
                 * @return The encoded byte count, or 0 if this is PCM audio.
                 */
                size_t compressedSize() const {
                        if(!isCompressed() || !_buffer.isValid()) return 0;
                        return _buffer->size();
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
                 * @brief Returns a const reference to the audio metadata.
                 * @return The metadata.
                 */
                const Metadata &metadata() const {
                        return _desc.metadata();
                }

                /**
                 * @brief Returns a mutable reference to the audio metadata.
                 * @return The metadata.
                 */
                Metadata &metadata() {
                        return _desc.metadata();
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

                /**
                 * @brief Resolves a single template key against this audio buffer's structure.
                 *
                 * Mirrors @ref Image::resolveTemplateKey — used by
                 * @ref makeString and by enclosing containers
                 * (@ref Frame::resolveTemplateKey for the
                 * @c Audio[N].xxx subscript syntax) to render a single
                 * @c {Key[:spec]} placeholder.  Returns a value when
                 * the key names something this audio object can
                 * describe (a registered metadata key, or one of the
                 * @c "@"-prefixed pseudo keys listed in
                 * @ref makeString) and @c std::nullopt otherwise.
                 *
                 * @param key  The placeholder key (no braces, no colon).
                 * @param spec The format spec (may be empty).
                 */
                std::optional<String> resolveTemplateKey(const String &key, const String &spec) const;

                /**
                 * @brief Substitutes @c {Key[:spec]} placeholders against this audio buffer.
                 *
                 * Delegates to @ref Metadata::format for direct
                 * metadata keys.  Adds an introspection layer that
                 * resolves @c "@"-prefixed pseudo keys describing the
                 * audio buffer's own structure.
                 *
                 * Recognised pseudo keys:
                 *
                 *  - @c \@SampleRate — sample rate in Hz (float).
                 *  - @c \@Channels — channel count (uint32).
                 *  - @c \@DataType — short data-type name (e.g. @c "PCMI_S16LE").
                 *  - @c \@Samples — current sample count (uint64).
                 *  - @c \@MaxSamples — buffer capacity in samples (uint64).
                 *  - @c \@Frames — total sample frames (samples × channels) (uint64).
                 *  - @c \@BytesPerSample — single-sample size in bytes (uint64).
                 *  - @c \@IsValid — bool.
                 *  - @c \@IsCompressed — bool.
                 *  - @c \@IsNative — bool (true if format matches host native float).
                 *  - @c \@CompressedSize — encoded byte count, or 0 (uint64).
                 *  - @c \@CodecFourCC — four-character codec code, or empty (string).
                 *
                 * @par Example
                 * @code
                 * Audio aud(AudioDesc(48000, 2, AudioDesc::Float32), 1024);
                 * String s = aud.makeString("{@SampleRate}Hz {@Channels}ch x {@Samples}");
                 * // "48000Hz 2ch x 1024"
                 * @endcode
                 *
                 * @tparam Resolver Callable returning @c std::optional<String>.  Pass
                 *                  @c nullptr to disable the user fallback.
                 * @param tmpl     Template string with @c {Key[:spec]} placeholders.
                 * @param resolver Optional fallback resolver consulted for keys that
                 *                 are neither @c "@"-prefixed pseudo keys nor present
                 *                 in @ref metadata.
                 * @param err      Optional error output.
                 */
                template <typename Resolver>
                String makeString(const String &tmpl, Resolver &&resolver, Error *err = nullptr) const {
                        return _desc.metadata().format(tmpl,
                                [this, &resolver](const String &key, const String &spec) -> std::optional<String> {
                                        if(!key.isEmpty() && key.cstr()[0] == '@') {
                                                auto v = resolvePseudoKey(key, spec);
                                                if(v.has_value()) return v;
                                        }
                                        if constexpr (!std::is_same_v<std::decay_t<Resolver>, std::nullptr_t>) {
                                                return resolver(key, spec);
                                        }
                                        return std::nullopt;
                                }, err);
                }

                /** @brief Convenience overload of @ref makeString with no fallback resolver. */
                String makeString(const String &tmpl, Error *err = nullptr) const {
                        return makeString(tmpl, nullptr, err);
                }

        private:
                Buffer::Ptr     _buffer;
                AudioDesc       _desc;
                size_t          _samples = 0;
                size_t          _maxSamples = 0;

                bool allocate(const MemSpace &ms);
                std::optional<String> resolvePseudoKey(const String &key, const String &spec) const;
};

PROMEKI_NAMESPACE_END
