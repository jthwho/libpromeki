/**
 * @file      memfdbufferimpl.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>
#include <promeki/hostbufferimpl.h>
#include <promeki/memfdregion.h>
#include <promeki/mutex.h>
#include <promeki/sharedptr.h>

PROMEKI_NAMESPACE_BEGIN

#if PROMEKI_ENABLE_MEMFD
/**
 * @brief BufferImpl backed by a sealed @ref MemfdRegion (page-level CoW).
 * @ingroup util
 *
 * Backs @ref MemSpace::SystemCow.  In its **producer phase** (the
 * lifetime span between construction and the first @ref seal), the
 * impl exposes a @c MAP_SHARED|PROT_WRITE producer view of an
 * underlying anonymous memfd; writes through @c data() / @ref fill /
 * @ref copyFromHost go to file pages and are visible to every Buffer
 * handle that shares this impl.
 *
 * In its **CoW phase** (after a successful seal), the producer view
 * is gone.  Every clone created via @ref _promeki_clone gets its own
 * @c MAP_PRIVATE view of the same sealed region — writes through any
 * one view CoW pages privately into per-clone anonymous backing.
 * Two cloned impls may diverge byte-for-byte while still sharing
 * physical memory for any pages neither has dirtied.  This is the
 * mechanism that makes per-frame TPG burn-in cheap (the cached
 * background payload is sealed once; per-frame detach pays only for
 * the burn-in band's pages, not the full frame).
 *
 * @par Trigger paths into seal()
 * - **Explicit:** call sites that have a clear "populate done" point
 *   call @c Buffer::seal() / @c BufferView::seal() once on the
 *   producer thread.  This is the recommended pattern when one
 *   exists — flame graphs get a deterministic transition.
 * - **Implicit:** @ref _promeki_clone auto-seals as a safety net so
 *   the clone is always valid even if the caller forgot to seal.
 *
 * Both paths reach the same @ref seal body and are idempotent.
 *
 * @par Failure modes
 * Construction failure (memfd_create unavailable, fd-table exhausted)
 * leaves @c _hostPtr null and @c canClone false; @c Buffer::data()
 * returns null; @c Buffer::ensureExclusive is a no-op.  A @ref seal
 * that fails partway latches the same dead state — production
 * callers should treat any @c seal error as a re-allocate trigger.
 *
 * @par Concurrency contract
 * The seal transition rewrites the inherited @c _hostPtr under
 * @c _sealMutex.  Concurrent reads of @c data() on sibling Buffer
 * handles during @c seal() / @c ensureExclusive are unsafe — readers
 * don't take the mutex.  Use @ref Buffer::isCowBacked to branch on
 * the stricter contract from generic code.  TPG and the planned
 * consumers satisfy this trivially: populate happens on the open
 * strand, @c seal() runs once, and from then on per-frame
 * @c ensureExclusive happens on a single (different) strand.
 *
 * @par Linux-only
 * Compiled only when @c PROMEKI_ENABLE_MEMFD is set.  Non-Linux
 * builds route @c MemSpace::SystemCow to @ref HostBufferImpl
 * (correctness preserved, no CoW optimisation; @c isCowBacked
 * returns false there).
 */
class MemfdBufferImpl : public HostMappedBufferImpl {
        public:
                /**
                 * @brief Allocates a memfd-backed region of @p bytes.
                 *
                 * The constructor opens a @ref MemfdRegion of the
                 * requested size, lazily mmaps the producer view, and
                 * publishes its base address as the inherited
                 * @c _hostPtr.  On any failure (memfd unavailable,
                 * mmap failure, fd-table exhausted) the impl is left
                 * with a null @c _hostPtr; @ref canClone reports
                 * false; subsequent @c Buffer operations behave as
                 * if the buffer were invalid.
                 */
                MemfdBufferImpl(const MemSpace &ms, size_t bytes, size_t align);

                /** @brief Releases the producer / clone view and the underlying region. */
                ~MemfdBufferImpl() override;

                bool   canClone() const override;
                bool   isCowBacked() const override { return true; }
                Error  seal() const override;
                size_t residentBytes() const override;

                /**
                 * @brief Deep-copy clone for @c Buffer::ensureExclusive.
                 *
                 * Auto-seals if the caller hasn't.  The sibling impl
                 * shares the same @ref MemfdRegion (so the fd is
                 * not duplicated) and maps its own @c MAP_PRIVATE
                 * view via @c MemfdRegion::cloneView().  The sibling
                 * is born sealed; further seal calls on it are
                 * idempotent.
                 */
                MemfdBufferImpl *_promeki_clone() const override;

        private:
                /**
                 * @brief Constructs a sibling clone that shares an existing region.
                 *
                 * Used by @ref _promeki_clone — the sibling acquires
                 * its own @c MAP_PRIVATE view via
                 * @c MemfdRegion::cloneView() and is born sealed.
                 */
                MemfdBufferImpl(const MemfdBufferImpl &source, void *cloneView);

                using RegionPtr = SharedPtr<MemfdRegion, false>;

                RegionPtr      _region;
                mutable Mutex  _sealMutex;
                mutable bool   _sealed = false;
                mutable bool   _dead   = false;
                // Set to true at sibling-clone construction time.
                // ensureExclusive() implies the caller intends to
                // write through the new clone; subsequent detaches
                // from this clone must therefore preserve any
                // such writes (the kernel's MAP_PRIVATE-from-file
                // semantics would silently drop them).  The post-
                // seal source (and the producer-phase impl) stay
                // _dirty=false so the first detach from a clean
                // cached payload still uses the cheap kernel-CoW
                // path — the SystemCow optimisation hinges on
                // exactly that case.
                bool           _dirty  = false;
};

#endif // PROMEKI_ENABLE_MEMFD
PROMEKI_NAMESPACE_END
