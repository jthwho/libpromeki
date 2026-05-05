/**
 * @file      pcmaudiopayload.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder.
 */

#pragma once

#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/uniqueptr.h>
#include <promeki/audiopayload.h>
#include <promeki/variantlookup.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief PCM audio payload — linear PCM samples in the format
 *        described by the base @ref AudioPayload's @ref AudioDesc.
 * @ingroup proav
 *
 * The plane list on the base carries one @ref BufferView for
 * interleaved PCM, or one per channel for planar PCM.  Which
 * layout applies is determined by the descriptor's
 * @ref AudioFormat (@c AudioFormat::isPlanar).
 *
 * Each PCM audio payload represents a contiguous run of
 * @ref sampleCount samples per channel.  Every PCM payload is
 * trivially keyframe-able (decoding any sample does not require
 * any earlier sample), so @ref isKeyframe is overridden to always
 * return @c true.
 *
 * PcmAudioPayload is intentionally not @c final.
 *
 * @par Example — allocating from a descriptor
 * @code
 * AudioDesc desc(AudioFormat::PCMI_Float32LE, 48000, 2);
 * size_t samples = 1024;
 * auto buf = Buffer(desc.bufferSize(samples));
 * BufferView plane0(buf, 0, buf.size());
 * auto payload = PcmAudioPayload::Ptr::create(
 *         desc, samples, plane0);
 * @endcode
 *
 * @par Thread Safety
 * Conditionally thread-safe — same contract as @ref AudioPayload.
 */
class PcmAudioPayload : public AudioPayload {
        public:
                PROMEKI_MEDIAPAYLOAD_LOOKUP_DISPATCH(PcmAudioPayload)

                virtual PcmAudioPayload *_promeki_clone() const override { return new PcmAudioPayload(*this); }

                /** @brief Shared-pointer alias for PcmAudioPayload ownership. */
                using Ptr = SharedPtr<PcmAudioPayload, /*CopyOnWrite=*/true, PcmAudioPayload>;

                /** @brief List of shared pointers to PcmAudioPayload instances. */
                using PtrList = ::promeki::List<Ptr>;

                /** @brief Unique-ownership pointer to a PcmAudioPayload. */
                using UPtr = UniquePtr<PcmAudioPayload>;

                /** @brief Constructs an empty PCM audio payload. */
                PcmAudioPayload() = default;

                /**
                 * @brief Constructs a PCM audio payload with the given
                 *        descriptor and (optional) sample count.
                 */
                explicit PcmAudioPayload(const AudioDesc &desc, size_t sampleCount = 0)
                    : AudioPayload(desc, sampleCount) {}

                /**
                 * @brief Constructs a PCM audio payload with a descriptor,
                 *        sample count, and plane list.
                 */
                PcmAudioPayload(const AudioDesc &desc, size_t sampleCount, const BufferView &data)
                    : AudioPayload(desc, sampleCount, data) {}

                /**
                 * @brief Always returns @c false — this class only models
                 *        linear PCM payloads.
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
                 * @brief Converts this payload to a different PCM audio
                 *        format.
                 *
                 * Thin payload-native wrapper for audio-format conversion
                 * — runs the per-format PCM converter in place on this
                 * payload's samples and returns the result as a fresh
                 * @ref PcmAudioPayload.  Target format must be
                 * a PCM format; compressed targets go through an
                 * @ref AudioEncoder session.
                 *
                 * @param dstFormat The target PCM @ref AudioFormat.
                 * @return A fresh PCM payload in @p dstFormat, or a null
                 *         Ptr on failure.
                 */
                Ptr convert(const AudioFormat &dstFormat) const;

                /** @brief Stable FourCC for DataStream serialisation. */
                static constexpr FourCC kSubclassFourCC{'P', 'A', 'd', 'p'};

                uint32_t subclassFourCC() const override { return kSubclassFourCC.value(); }

                /** @copydoc MediaPayload::serialisePayload */
                void serialisePayload(DataStream &s) const override;

                /** @copydoc MediaPayload::deserialisePayload */
                void deserialisePayload(DataStream &s) override;

                PcmAudioPayload(const PcmAudioPayload &) = default;
                PcmAudioPayload(PcmAudioPayload &&) = default;
                PcmAudioPayload &operator=(const PcmAudioPayload &) = default;
                PcmAudioPayload &operator=(PcmAudioPayload &&) = default;
};

PROMEKI_NAMESPACE_END
