/**
 * @file      pcmsilencefiller.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstddef>
#include <promeki/audiodesc.h>
#include <promeki/buffer.h>
#include <promeki/error.h>
#include <promeki/namespace.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Reusable silence-payload buffer for PCM audio cadence-fill.
 * @ingroup proav
 *
 * Given an @ref AudioDesc and a target @c samplesPerPacket, builds
 * one @ref Buffer that contains exactly that many samples of
 * silence in the descriptor's wire format and returns it on demand.
 * The buffer is built once at construction and shared by every
 * caller of @ref payload — there is no per-call allocation, which
 * matters for the @ref AudioTxThread case that may emit thousands
 * of silence packets per second when the source has stalled.
 *
 * @par Silence definition
 * @c PcmSilenceFiller writes the format-correct silence value:
 * - Signed integer formats (@c PCMI_S16BE, @c PCMP_S24LE, etc.) and
 *   floats (@c PCMI_Float32LE, @c PCMP_Float32BE) — silence is
 *   all-zero bytes (IEEE 754 +0.0 is bit-exactly zero).
 * - Unsigned integer formats (@c PCMI_U8, @c PCMI_U16LE, etc.) —
 *   silence is the per-sample midpoint (@c 0x80 for u8,
 *   @c 0x8000 for u16, etc.).
 *
 * The midpoint write respects @c isBigEndian so the on-wire byte
 * pattern matches what a real-content packet of the same format
 * would look like at the same sample value.
 *
 * @par Lives next to @ref AudioBuffer
 * Both classes share PCM layout knowledge (@ref AudioFormat
 * predicates, @ref AudioDesc::bufferSize).  @c AudioTxThread uses
 * this filler when its @c PacketQueue is empty; future audio
 * output paths that need cadence-fill (e.g. an SDI / NDI output
 * that must keep emitting at sample rate even when the upstream
 * has paused) can reuse the same helper.
 *
 * @par Thread safety
 * Once constructed, the silence @ref Buffer is read-only; multiple
 * threads can call @ref payload concurrently.  Each call returns a
 * @c Buffer value — the underlying storage is shared through the
 * value-type CoW handle so no copy happens at the byte level.
 */
class PcmSilenceFiller {
        public:
                /// @brief Builds a silence filler for an empty
                ///        descriptor — @ref payload returns an
                ///        empty Buffer until the filler is
                ///        re-initialised via @ref reset.
                PcmSilenceFiller() = default;

                /**
                 * @brief Builds a filler producing
                 *        @p samplesPerPacket samples of silence in
                 *        @p desc 's wire format.
                 *
                 * @param desc             Audio descriptor of the
                 *                         wire format.  Must be
                 *                         valid; an invalid desc
                 *                         leaves the filler in the
                 *                         empty state and
                 *                         @ref payload returns an
                 *                         empty Buffer.
                 * @param samplesPerPacket Per-channel sample count
                 *                         to populate.  Zero is
                 *                         legal and results in an
                 *                         empty silence buffer.
                 */
                PcmSilenceFiller(const AudioDesc &desc, size_t samplesPerPacket);

                /**
                 * @brief Re-initialises the filler in place.
                 *
                 * Equivalent to assigning from a freshly-constructed
                 * @c PcmSilenceFiller.  Useful when the audio
                 * stream's wire format changes mid-flight (rare,
                 * but supported).
                 *
                 * @return @c Error::Ok on success, an error code
                 *         when the descriptor is invalid.
                 */
                Error reset(const AudioDesc &desc, size_t samplesPerPacket);

                /// @brief Returns the cached silence payload.
                ///
                /// The returned @c Buffer references the same
                /// storage every call — copies are refcount bumps,
                /// so safe to hand out to many concurrent emitters.
                /// Empty when the filler is in its default-
                /// constructed state.
                const Buffer &payload() const { return _payload; }

                /// @brief Returns the per-channel sample count
                ///        baked into the cached payload.
                size_t samplesPerPacket() const { return _samplesPerPacket; }

                /// @brief Returns the descriptor the cached payload
                ///        was built for.
                const AudioDesc &desc() const { return _desc; }

                /// @brief Returns the byte size of the cached
                ///        payload — short-hand for
                ///        @c payload().size().
                size_t size() const { return _payload.size(); }

        private:
                AudioDesc _desc;
                size_t    _samplesPerPacket = 0;
                Buffer    _payload;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
