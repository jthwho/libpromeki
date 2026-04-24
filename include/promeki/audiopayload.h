/**
 * @file      audiopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/mediapayload.h>
#include <promeki/audiodesc.h>
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
 * (@ref UncompressedAudioPayload and @ref CompressedAudioPayload)
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
                 * @brief Constructs an audio payload with the given descriptor.
                 *
                 * Plane list left empty; attach via @ref setData or via
                 * the two-argument constructor below.
                 */
                explicit AudioPayload(const AudioDesc &desc) : _desc(desc) { }

                /**
                 * @brief Constructs an audio payload with a descriptor and planes.
                 */
                AudioPayload(const AudioDesc &desc, const BufferView &data) :
                        MediaPayload(data), _desc(desc) { }

                /** @brief Returns @ref MediaPayloadKind::Audio. */
                const MediaPayloadKind &kind() const override { return MediaPayloadKind::Audio; }

                /** @brief Returns the audio descriptor. */
                const AudioDesc &desc() const { return _desc; }

                /** @brief Returns a mutable reference to the audio descriptor. */
                AudioDesc &desc() { return _desc; }

                /** @brief Replaces the audio descriptor. */
                void setDesc(const AudioDesc &d) { _desc = d; }

                AudioPayload(const AudioPayload &) = default;
                AudioPayload(AudioPayload &&) = default;
                AudioPayload &operator=(const AudioPayload &) = default;
                AudioPayload &operator=(AudioPayload &&) = default;

        private:
                AudioDesc _desc;
};

PROMEKI_NAMESPACE_END
