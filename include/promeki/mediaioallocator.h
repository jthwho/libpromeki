/**
 * @file      mediaioallocator.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_PROAV
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/bufferallocator.h>
#include <promeki/uncompressedvideopayload.h>
#include <promeki/pcmaudiopayload.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Proav extension to @ref BufferAllocator with payload-shaped helpers.
 * @ingroup proav
 *
 * Inherits @ref BufferAllocator's per-plane / per-chunk / per-byte
 * primitives and adds full-payload allocators for the proav payload
 * types (@ref UncompressedVideoPayload, @ref PcmAudioPayload).  Every
 * @ref MediaIO vends a @c MediaIOAllocator::Ptr — the backend's
 * placement policy is the single point of injection through which
 * any frame allocated on its behalf flows.
 *
 * @par Default behaviour
 * The base implementations of @ref allocateVideoPayload /
 * @ref allocateAudioPayload assemble the full payload from the
 * inherited per-plane / per-chunk primitives.  A backend that has
 * a structural reason to allocate the whole payload as one unit
 * (NVDEC's single contiguous device alloc covering all planes,
 * say) can override the full-payload methods directly; the per-
 * plane methods will then never be called for that payload class.
 *
 * @par Routing
 * The default implementations of the per-plane / per-chunk / per-
 * byte primitives in @ref MediaIOAllocator route through
 * @c BufferAllocator::defaultAllocator() for the actual allocation.
 * Subclasses override one or both layers depending on whether the
 * backend's policy is per-plane (NDI: pinned host) or whole-payload
 * (NVDEC: single device alloc).
 *
 * @par Default singleton
 * @ref defaultAllocator returns a process-wide stateless allocator
 * that defers per-plane / per-chunk allocations to
 * @c BufferAllocator::defaultAllocator() — same behaviour as
 * before the allocator framework existed.
 *
 * @par Threading and lifetime
 * Same contract as @ref BufferAllocator — implementations must be
 * thread-safe; pooled backends anchor their pool's lifetime through
 * @c SharedPtr from the vended Buffers.
 */
class MediaIOAllocator : public BufferAllocator {
        public:
                PROMEKI_SHARED_DERIVED(MediaIOAllocator)

                /** @brief Shared-pointer alias. */
                using Ptr = SharedPtr<MediaIOAllocator, /*CopyOnWrite=*/false>;

                MediaIOAllocator() = default;
                ~MediaIOAllocator() override = default;

                String name() const override;
                Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override;
                Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) const override;
                Buffer allocateBytes(size_t bytes, size_t align = 0) const override;

                /**
                 * @brief Allocates a full uncompressed video payload.
                 *
                 * The default implementation walks every plane index
                 * in @p desc and calls @ref allocateVideoPlane for
                 * each, assembling the result into a @ref BufferView.
                 * Backends override this method when they need to
                 * allocate every plane as one contiguous block —
                 * NVDEC's single device allocation is the canonical
                 * case.  Returns a null @c Ptr on failure (any plane
                 * allocation invalid → the whole payload is invalid).
                 */
                virtual UncompressedVideoPayload::Ptr allocateVideoPayload(const ImageDesc &desc) const;

                /**
                 * @brief Allocates a full PCM audio payload.
                 *
                 * The default implementation calls
                 * @ref allocateAudioChunk for the requested sample
                 * count and wraps it in a @ref BufferView.  Backends
                 * override when they have a structural reason to
                 * shape the payload differently (e.g. ALSA's per-
                 * period chunking).
                 */
                virtual PcmAudioPayload::Ptr allocateAudioPayload(const AudioDesc &desc, size_t samples) const;

                /**
                 * @brief Returns the process-wide default
                 *        @ref MediaIOAllocator instance.
                 *
                 * Stateless; routes every primitive through
                 * @c BufferAllocator::defaultAllocator().  Always
                 * returns the same instance (Meyers' singleton).
                 */
                static Ptr defaultAllocator();
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_PROAV
