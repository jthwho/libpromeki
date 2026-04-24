/**
 * @file      uncompressedaudiopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/uniqueptr.h>
#include <promeki/audiopayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Uncompressed audio payload — PCM samples in the format
 *        described by the base @ref AudioPayload's @ref AudioDesc.
 * @ingroup proav
 *
 * The plane list on the base carries one @ref BufferView for
 * interleaved PCM, or one per channel for planar PCM.  Which
 * layout applies is determined by the descriptor's
 * @ref AudioFormat (@c AudioFormat::isPlanar).
 *
 * Each uncompressed audio payload represents a contiguous run of
 * @ref sampleCount samples per channel.  Every PCM payload is
 * trivially keyframe-able (decoding any sample does not require
 * any earlier sample), so @ref isKeyframe is overridden to always
 * return @c true.
 *
 * UncompressedAudioPayload is intentionally not @c final.
 *
 * @par Example — allocating from a descriptor
 * @code
 * AudioDesc desc(AudioFormat::PCMI_Float32LE, 48000, 2);
 * size_t samples = 1024;
 * auto buf = Buffer::Ptr::create(desc.bufferSize(samples));
 * BufferView plane0(buf, 0, buf->size());
 * auto payload = UncompressedAudioPayload::Ptr::create(
 *         desc, samples, plane0);
 * @endcode
 */
class UncompressedAudioPayload : public AudioPayload {
        public:
                virtual UncompressedAudioPayload *_promeki_clone() const override {
                        return new UncompressedAudioPayload(*this);
                }

                /** @brief Shared-pointer alias for UncompressedAudioPayload ownership. */
                using Ptr = SharedPtr<UncompressedAudioPayload, /*CopyOnWrite=*/true, UncompressedAudioPayload>;

                /** @brief List of shared pointers to UncompressedAudioPayload instances. */
                using PtrList = promeki::List<Ptr>;

                /** @brief Unique-ownership pointer to an UncompressedAudioPayload. */
                using UPtr = UniquePtr<UncompressedAudioPayload>;

                /** @brief Constructs an empty uncompressed audio payload. */
                UncompressedAudioPayload() = default;

                /**
                 * @brief Constructs an uncompressed audio payload with the
                 *        given descriptor and (optional) sample count.
                 */
                explicit UncompressedAudioPayload(const AudioDesc &desc,
                                                  size_t sampleCount = 0) :
                        AudioPayload(desc), _sampleCount(sampleCount) { }

                /**
                 * @brief Constructs an uncompressed audio payload with a
                 *        descriptor, sample count, and plane list.
                 */
                UncompressedAudioPayload(const AudioDesc &desc,
                                         size_t sampleCount,
                                         const BufferView &data) :
                        AudioPayload(desc, data), _sampleCount(sampleCount) { }

                /**
                 * @brief Always returns @c false — this class only models
                 *        uncompressed PCM payloads.
                 */
                bool isCompressed() const override { return false; }

                /**
                 * @brief Trivially true — PCM has no decode dependencies
                 *        between samples.
                 */
                bool isKeyframe() const override { return true; }

                /**
                 * @brief Trivially true — cutting before any PCM payload
                 *        leaves downstream consumers in a coherent state.
                 */
                bool isSafeCutPoint() const override { return true; }

                /**
                 * @brief Returns the number of samples per channel carried
                 *        in this payload.
                 */
                size_t sampleCount() const { return _sampleCount; }

                /** @brief Sets the number of samples per channel. */
                void setSampleCount(size_t n) { _sampleCount = n; }

                /**
                 * @brief Converts this payload to a different PCM audio
                 *        format.
                 *
                 * Thin payload-native wrapper for audio-format conversion
                 * — runs the per-format PCM converter in place on this
                 * payload's samples and returns the result as a fresh
                 * @ref UncompressedAudioPayload.  Target format must be
                 * a PCM format; compressed targets go through an
                 * @ref AudioEncoder session.
                 *
                 * @param dstFormat The target PCM @ref AudioFormat.
                 * @return A fresh uncompressed payload in @p dstFormat,
                 *         or a null Ptr on failure.
                 */
                Ptr convert(const AudioFormat &dstFormat) const;

                /** @brief Stable FourCC for DataStream serialisation. */
                static constexpr FourCC kSubclassFourCC{'U','A','d','p'};

                uint32_t subclassFourCC() const override {
                        return kSubclassFourCC.value();
                }

                /** @copydoc MediaPayload::serialisePayload */
                void serialisePayload(DataStream &s) const override;

                /** @copydoc MediaPayload::deserialisePayload */
                void deserialisePayload(DataStream &s) override;

                UncompressedAudioPayload(const UncompressedAudioPayload &) = default;
                UncompressedAudioPayload(UncompressedAudioPayload &&) = default;
                UncompressedAudioPayload &operator=(const UncompressedAudioPayload &) = default;
                UncompressedAudioPayload &operator=(UncompressedAudioPayload &&) = default;

        private:
                size_t _sampleCount = 0;
};

PROMEKI_NAMESPACE_END
