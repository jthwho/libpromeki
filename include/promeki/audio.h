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
#include <promeki/audiopacket.h>
#include <promeki/stringlist.h>
#include <promeki/variantlookup.h>

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
 * AAC, Opus, or MP3.  A compressed Audio uses an @ref AudioDesc whose
 * @ref AudioFormat is one of the compressed entries (e.g.
 * @c AudioFormat::Opus) — @ref AudioFormat::isCompressed is true for
 * those.  The raw encoded bytes live in the object's single buffer.
 * Use @c isCompressed() to test, @c compressedSize() to get the byte
 * count, and @c Audio::fromBuffer() to adopt an existing
 * @c Buffer::Ptr without copying.
 *
 * @par Example
 * @code
 * AudioDesc desc(AudioFormat(AudioFormat::PCMI_Float32LE), 48000, 2);
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
                 * from @c File::readBulk).  For compressed audio,
                 * @p desc must use a compressed @ref AudioFormat (one
                 * where @c AudioFormat::isCompressed is true); for PCM
                 * audio, the buffer size must be a whole multiple of
                 * the PCM frame size (channels × bytesPerSample).
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
                 * @brief Returns true when stopping the stream before this
                 *        audio unit leaves the decoded output intact.
                 *
                 * Uncompressed PCM is always a safe cut.  A compressed
                 * unit is safe when the codec's
                 * @ref AudioCodec::packetIndependence is
                 * @ref AudioCodec::PacketIndependenceEvery (every packet
                 * decodes standalone — e.g. Opus); otherwise the unit
                 * carries no safe cut point — audio packets do not use
                 * per-packet keyframe flags, so codecs whose
                 * independence is @c PacketIndependenceKeyframe cannot
                 * be safely cut from an @c Audio-level query.  A codec
                 * with @ref AudioCodec::PacketIndependenceInvalid
                 * returns @c false — we have no evidence the stream can
                 * be safely truncated here.
                 *
                 * Used by @ref Frame::isSafeCutPoint and by
                 * @ref MediaPipeline to honour a pipeline-wide frame
                 * count without leaving an audio packet that won't
                 * decode on its own.
                 *
                 * @return @c true when stopping before this audio unit
                 *         is safe, @c false when prior packets would be
                 *         needed to decode it.
                 */
                bool isSafeCutPoint() const;

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
                 * @brief Returns the compressed bitstream packet attached to this Audio.
                 *
                 * Populated only for compressed audio (AAC, Opus, and
                 * other bitstream codecs) — the encoded bytes live in
                 * the attached packet, and this Audio object carries
                 * the descriptor / timing metadata.  Null for
                 * uncompressed PCM audio.
                 */
                const AudioPacket::Ptr &packet() const {
                        return _packet;
                }

                /**
                 * @brief Replaces the attached compressed-bitstream packet.
                 *
                 * Pass a null @ref AudioPacket::Ptr to clear.
                 */
                void setPacket(AudioPacket::Ptr pkt) {
                        _packet = std::move(pkt);
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
                 * @param format The target @ref AudioFormat.  Must be a
                 *               PCM format — compressed formats are not
                 *               valid conversion targets.
                 * @return A new Audio object in the target format, or an
                 *         invalid Audio on failure.
                 */
                Audio convert(const AudioFormat &format) const;

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
                 * @brief Returns a human-readable multi-line dump of
                 *        this audio buffer's structure and contents.
                 *
                 * Emits the scalar-key block registered with
                 * @c VariantLookup<Audio> (sample rate, channels,
                 * data type, sample / frame counts, compression
                 * flags), the backing @ref Buffer summary, metadata
                 * entries via @ref Metadata::dump, and a single line
                 * about the attached @ref AudioPacket when present.
                 *
                 * @param indent Leading whitespace to prefix every line.
                 * @return A @ref StringList (one entry per line).
                 */
                StringList dump(const String &indent = String()) const;

        private:
                Buffer::Ptr       _buffer;
                AudioDesc         _desc;
                size_t            _samples = 0;
                size_t            _maxSamples = 0;
                AudioPacket::Ptr  _packet;

                bool allocate(const MemSpace &ms);
};

PROMEKI_NAMESPACE_END
