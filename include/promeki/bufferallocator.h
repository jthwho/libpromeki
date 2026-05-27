/**
 * @file      bufferallocator.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <promeki/namespace.h>
#include <promeki/sharedptr.h>
#include <promeki/string.h>
#include <promeki/buffer.h>

PROMEKI_NAMESPACE_BEGIN

#if PROMEKI_ENABLE_PROAV
class ImageDesc;
class AudioDesc;
#endif

/**
 * @brief Buffer-placement seam — an injectable "where does this Buffer live?" callback.
 * @ingroup util
 *
 * @c BufferAllocator is the abstract core seam through which any
 * library subsystem that needs to delegate buffer placement (which
 * @ref MemSpace, what alignment, whether to draw from a pool) does
 * so without coupling to a specific backend.  It exposes three
 * primitive allocators — per-plane video, per-chunk audio, and
 * generic raw bytes — each of which a subclass may override to apply
 * its own placement policy.
 *
 * Subsystems that wrap a hardware backend (NDI, NVDEC, DeckLink,
 * RTP TX/RX) provide their own subclass that returns Buffers in the
 * @ref MemSpace the backend prefers (pinned host, NUMA-local, device-
 * resident, page-aligned pool).  Generic code calls the same three
 * methods regardless of backend; the policy decision belongs to the
 * code that knows about the hardware.
 *
 * @par Default singleton
 * @ref defaultAllocator returns a process-wide stateless allocator
 * that routes every call through @c Buffer(bytes, align,
 * MemSpace::Default).  Code paths that haven't been wired through a
 * specific backend's allocator fall through to this default and get
 * the same heap-backed allocation behaviour they had before the
 * allocator framework existed.
 *
 * @par Thread Safety
 * Implementations must be thread-safe — allocators are called from
 * prefetch workers, write strands, CSC threads, and downstream
 * pipelines concurrently.  The default implementation is trivially
 * safe (stateless).  Subclasses that hold pools take their own
 * synchronization.
 *
 * @par Lifetime
 * Pooled backends recycle Buffers back to a pool when the
 * corresponding @ref Buffer drops; the pool's lifetime must outlive
 * every vended Buffer.  Concrete backend @ref BufferImpl subclasses
 * that need this hold a @c SharedPtr to their pool / allocator
 * state, so the @c SharedPtr countdown anchors the allocator to the
 * outstanding Buffers automatically — no explicit "release" hook is
 * required on the allocator itself.
 *
 * @par Failure modes
 * If an allocator cannot satisfy a request (pool exhausted, OS
 * allocation failure), it returns an invalid @ref Buffer (or null
 * @c Ptr for the full-payload helpers in
 * @ref MediaIOAllocator).  Callers handle invalid returns the same
 * way they handle a failed @c Buffer(size) today.  No exceptions.
 */
class BufferAllocator {
        public:
                PROMEKI_SHARED_BASE(BufferAllocator)

                /** @brief Shared-pointer alias.  CoW disabled — the allocator is referenced, never cloned. */
                using Ptr = SharedPtr<BufferAllocator, /*CopyOnWrite=*/false>;

                BufferAllocator() = default;
                virtual ~BufferAllocator() = default;

                BufferAllocator(const BufferAllocator &)            = delete;
                BufferAllocator &operator=(const BufferAllocator &) = delete;

                /**
                 * @brief Returns a diagnostic name for this allocator.
                 *
                 * Appears in logs and tracing so operators can see
                 * which allocator vended a given Buffer.  Subclasses
                 * override to return their own name.
                 */
                virtual String name() const = 0;

                /**
                 * @brief Allocates a single video plane Buffer.
                 *
                 * The caller has already decoded @p desc and knows
                 * which plane index they need; the allocator decides
                 * which @ref MemSpace and what alignment best suits
                 * its backend.  Default implementations call through
                 * to @ref allocateBytes with @c desc.pixelFormat()
                 * .planeSize(planeIndex, desc) — a backend that has
                 * no plane-specific placement policy can leave this
                 * inherited path alone.
                 *
                 * Methods are @c const because the public API of an
                 * allocator is "vend a Buffer"; pool-backed
                 * implementations declare their internal pool /
                 * mutex / counters as @c mutable.  This lets
                 * @c SharedPtr<BufferAllocator, false>::operator->
                 * call them without forcing every call site to use
                 * @c modify().
                 */
#if PROMEKI_ENABLE_PROAV
                virtual Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const = 0;

                /**
                 * @brief Allocates a single audio chunk Buffer.
                 *
                 * @param desc    Describes the sample format, channel
                 *                count, and (for planar formats) the
                 *                per-channel layout.
                 * @param samples Number of samples per channel.
                 * @return A Buffer of @c desc.bufferSize(samples)
                 *         bytes, with @c logicalSize set to the same
                 *         value (audio chunks are never under-filled).
                 */
                virtual Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) const = 0;
#endif // PROMEKI_ENABLE_PROAV

                /**
                 * @brief Allocates a generic byte buffer.
                 *
                 * Used for metadata, codec extra-data, packet payloads,
                 * and anything else that doesn't fit the per-plane /
                 * per-chunk shape.  An @p align of 0 means "use the
                 * @ref Buffer default alignment for the backend's
                 * MemSpace."
                 */
                virtual Buffer allocateBytes(size_t bytes, size_t align = 0) const = 0;

                /**
                 * @brief Returns the process-wide default allocator.
                 *
                 * Stateless; routes every method through
                 * @c Buffer(bytes, align, MemSpace::Default).  Always
                 * returns the same instance (Meyers' singleton —
                 * race-free, leak-free, init-order-safe).  Call sites
                 * that haven't been wired through a specific backend
                 * use this as their fallthrough.
                 */
                static Ptr defaultAllocator();
};

/**
 * @brief Concrete default @ref BufferAllocator — heap-backed, stateless.
 * @ingroup util
 *
 * Returns @ref Buffer instances allocated through the standard
 * @c Buffer(bytes, align, MemSpace::Default) constructor.  Used as
 * the @ref BufferAllocator::defaultAllocator singleton.
 *
 * Stateless; trivially thread-safe; cheap to instantiate (no
 * allocations of its own).  Backends derive their own allocator
 * from @ref BufferAllocator (or @c MediaIOAllocator at the proav
 * layer) rather than subclassing this class.
 */
class DefaultBufferAllocator : public BufferAllocator {
        public:
                PROMEKI_SHARED_DERIVED(DefaultBufferAllocator)

                DefaultBufferAllocator() = default;
                ~DefaultBufferAllocator() override = default;

                String name() const override;
#if PROMEKI_ENABLE_PROAV
                Buffer allocateVideoPlane(const ImageDesc &desc, int planeIndex) const override;
                Buffer allocateAudioChunk(const AudioDesc &desc, size_t samples) const override;
#endif
                Buffer allocateBytes(size_t bytes, size_t align = 0) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
