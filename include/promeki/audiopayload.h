/**
 * @file      audiopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/mediapayload.h>
#include <promeki/audiodesc.h>
#include <promeki/duration.h>
#include <promeki/list.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Abstract base for any audio payload — compressed or
 *        uncompressed.
 * @ingroup proav
 *
 * AudioPayload binds a @ref MediaPayload's plane list to an
 * @ref AudioDesc that describes the format, sample rate, channel
 * count, and per-stream metadata.  The descriptor is the single
 * source of truth for "what is this audio?"; concrete leaves
 * (@ref PcmAudioPayload and @ref CompressedAudioPayload)
 * extend this base with whatever per-payload state only makes
 * sense for their side of the compressed / uncompressed split.
 *
 * For compressed audio the descriptor's @ref AudioFormat is a
 * compressed entry (Opus, AAC, MP3, FLAC, …); for uncompressed
 * audio it is a PCM format.  @c AudioFormat::isCompressed answers
 * the split question for either side, which is why
 * @ref AudioPayload is free to coexist with both kinds of
 * descriptor without a separate codec-identity field.
 *
 * The plane list covers both interleaved and planar PCM, and
 * compressed bitstream packets:
 *
 * - interleaved PCM — one @ref BufferView over a contiguous
 *   buffer;
 * - planar PCM — one @ref BufferView per channel (either
 *   separately allocated or views into one shared allocation);
 * - compressed bitstream — one @ref BufferView covering the
 *   encoded access unit.
 *
 * AudioPayload is abstract — @ref _promeki_clone and @ref kind
 * remain pure-virtual so the concrete leaves must supply a
 * covariant clone.  Subclassing concrete leaves for codec-specific
 * specializations is an intentional extension point.
 */
class AudioPayload : public MediaPayload {
        PROMEKI_SHARED_ABSTRACT(AudioPayload)
        public:
                /** @brief Shared-pointer alias for AudioPayload ownership. */
                using Ptr = SharedPtr<AudioPayload, /*CopyOnWrite=*/true, AudioPayload>;

                /** @brief List of shared pointers to AudioPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Constructs an empty audio payload. */
                AudioPayload() = default;

                /**
                 * @brief Constructs an audio payload with the given
                 *        descriptor and (optional) sample count.
                 */
                explicit AudioPayload(const AudioDesc &desc,
                                      size_t sampleCount = 0) :
                        _desc(desc), _sampleCount(sampleCount) { }

                /**
                 * @brief Constructs an audio payload with a descriptor,
                 *        sample count, and plane list.
                 */
                AudioPayload(const AudioDesc &desc,
                             size_t sampleCount,
                             const BufferView &data) :
                        MediaPayload(data), _desc(desc),
                        _sampleCount(sampleCount) { }

                /** @brief Returns @ref MediaPayloadKind::Audio. */
                const MediaPayloadKind &kind() const override { return MediaPayloadKind::Audio; }

                /** @brief Returns the audio descriptor. */
                const AudioDesc &desc() const { return _desc; }

                /** @brief Returns a mutable reference to the audio descriptor. */
                AudioDesc &desc() { return _desc; }

                /** @brief Replaces the audio descriptor. */
                void setDesc(const AudioDesc &d) { _desc = d; }

                /**
                 * @brief Returns the number of samples per channel
                 *        carried in this payload.
                 *
                 * For PCM, this is literally the number of linear
                 * samples stored in each plane.  For compressed
                 * audio, this is the number of decoded samples the
                 * encoded access unit represents — a property of the
                 * packet that cannot be recovered from the encoded
                 * bytes alone and which the producer records here
                 * (e.g. 960 for a 20 ms Opus packet at 48 kHz, 1024
                 * for AAC-LC, 1152 for MP3 Layer III).  Every audio
                 * payload carries this value because every block
                 * codec of interest emits a well-defined decoded
                 * sample count per packet.
                 */
                size_t sampleCount() const { return _sampleCount; }

                /** @brief Sets the number of samples per channel. */
                void setSampleCount(size_t n) { _sampleCount = n; }

                /**
                 * @brief Returns the wall-clock duration spanned by
                 *        the payload, computed as @c sampleCount /
                 *        @c desc().sampleRate().
                 */
                Duration duration() const override {
                        return Duration::fromSamples(
                                static_cast<int64_t>(_sampleCount),
                                _desc.sampleRate());
                }

                /**
                 * @brief Audio payloads always support a duration —
                 *        always returns @c true.
                 *
                 * @ref hasDuration is a type-level predicate that
                 * answers "is a duration meaningful for this payload
                 * kind?" rather than "has one been assigned?"  Audio
                 * payloads carry an intrinsic @c sampleCount /
                 * @c sampleRate pair from which @ref duration is
                 * always computable, so the answer is unconditionally
                 * yes — a zero duration simply reflects a zero
                 * sample count.
                 */
                bool hasDuration() const override { return true; }

                /**
                 * @brief Forwards to the descriptor's metadata.  @sa
                 *        @ref MediaPayload::metadata.
                 */
                const Metadata &metadata() const override { return _desc.metadata(); }

                /** @copydoc metadata() const */
                Metadata &metadata() override { return _desc.metadata(); }

                AudioPayload(const AudioPayload &) = default;
                AudioPayload(AudioPayload &&) = default;
                AudioPayload &operator=(const AudioPayload &) = default;
                AudioPayload &operator=(AudioPayload &&) = default;

        private:
                AudioDesc _desc;
                size_t    _sampleCount = 0;
};

PROMEKI_NAMESPACE_END
