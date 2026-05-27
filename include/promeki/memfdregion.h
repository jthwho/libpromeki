/**
 * @file      memfdregion.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_CORE
#include <cstddef>
#include <cstdint>
#include <promeki/namespace.h>
#include <promeki/string.h>
#include <promeki/error.h>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Linux memfd-backed region with sealed CoW clone views.
 * @ingroup util
 *
 * @ref MemfdRegion owns a single anonymous file descriptor created
 * via @c memfd_create with @c MFD_ALLOW_SEALING.  It models a
 * three-phase lifecycle:
 *
 * 1. **Producer phase** (between construction and first @ref seal).
 *    A single @ref producerView (mapped @c MAP_SHARED|PROT_WRITE) is
 *    available for population.  The producer view is owned by the
 *    region itself.
 * 2. **Seal transition** (the @ref seal call).  The producer view is
 *    munmapped, optionally a first @c MAP_PRIVATE clone is allocated
 *    atomically (for callers that need pointer stability across the
 *    transition), and @c F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW are
 *    added.  The transition is one-way and idempotent.
 * 3. **Sealed phase** (after @ref seal succeeds).  Callers can
 *    request as many @ref cloneView (caller-owned @c MAP_PRIVATE)
 *    and @ref readOnlyView (caller-owned @c MAP_SHARED|PROT_READ)
 *    mappings as they like.  Each clone is an independent CoW
 *    snapshot — writes through one clone never propagate to siblings
 *    or to the underlying file pages, so each clone's resident-set
 *    only grows by the pages it dirties.
 *
 * @par Why pre-seal cloning is forbidden
 * If a caller mapped a @c MAP_PRIVATE view alongside a still-active
 * @c MAP_SHARED producer view, kernel semantics propagate producer
 * writes into the clone for any pages the clone has not yet CoW'd —
 * almost never what the caller wants and a footgun in the public
 * API.  @ref cloneView and @ref readOnlyView therefore both return
 * @c null + @ref Error::NotReady before @ref seal.  The single case
 * that genuinely needs an atomic seal-then-clone (a sole holder
 * maintaining pointer stability across the seal boundary) is served
 * by the @c outFirstClone parameter on @ref seal itself.
 *
 * @par Failure modes
 * Construction failure (@c memfd_create or @c ftruncate fail) leaves
 * the region invalid — @ref isValid is false and every accessor
 * returns null / an error.  A @ref seal that fails partway (typically
 * @c EBUSY on @c F_ADD_SEALS, or @c ENOMEM on the atomic clone
 * allocation) transitions the region to a permanent dead state with
 * the same observable surface: @ref isValid becomes false; every
 * subsequent @ref producerView / @ref cloneView / @ref readOnlyView
 * call returns null + @ref Error::Invalid; another @ref seal returns
 * @ref Error::Invalid.  Production callers should treat any seal
 * error as an "abandon the region and re-allocate" trigger.
 *
 * @par Standalone uses
 * @ref MemfdRegion is useful on its own as a CoW snapshot primitive
 * (the @c MemfdBufferImpl backend is one consumer), as a broadcast
 * read-only IPC primitive (seal, hand @ref fd to peers via
 * @c SCM_RIGHTS, peers map their own @c MAP_SHARED|PROT_READ view),
 * or as a snapshot scratch for live tracing / log dumps.
 *
 * @par Linux-only
 * @ref MemfdRegion is implemented only when @c PROMEKI_ENABLE_MEMFD
 * is set at build time (Linux + a kernel new enough to expose
 * @c memfd_create with @c MFD_ALLOW_SEALING).  On other platforms
 * the class compiles to a stub that always reports invalid; callers
 * are expected to fall back to a non-CoW backend.
 *
 * @par Thread Safety
 * Distinct instances are independently safe.  Concurrent calls on a
 * single @ref MemfdRegion are not synchronised internally — the
 * region is intended to be driven by a single producer through the
 * seal transition, after which the sealed handle can be queried
 * concurrently to vend caller-owned clone / read-only views.
 */
class MemfdRegion {
        public:
                /**
                 * @brief Returns true when memfd-backed regions are usable on this build.
                 *
                 * False when @c PROMEKI_ENABLE_MEMFD is unset at compile time.
                 * Callers can branch on this to select a non-CoW fallback.
                 */
                static bool isSupported();

                /** @brief Constructs an invalid region (no fd, no mapping). */
                MemfdRegion();

                /**
                 * @brief Constructs a region of @p bytes (rounded up to page size).
                 *
                 * Allocates the memfd via @c memfd_create with
                 * @c MFD_CLOEXEC|MFD_ALLOW_SEALING and sizes it via
                 * @c ftruncate.  On failure (@c ENOSYS, @c EMFILE,
                 * @c ENFILE, etc.) the region is constructed invalid;
                 * @ref isValid returns false and every accessor returns
                 * null / an error.
                 *
                 * @param bytes      Requested size in bytes.  Rounded up
                 *                   to a page boundary; the rounded value
                 *                   is returned by @ref size.
                 * @param debugName  Human-readable name for the memfd
                 *                   (visible via @c /proc/<pid>/fd
                 *                   listings; useful in tracing).
                 */
                explicit MemfdRegion(size_t bytes, const String &debugName = String("anonymous"));

                /** @brief Closes the fd and releases the producer view if still mapped. */
                ~MemfdRegion();

                MemfdRegion(const MemfdRegion &)            = delete;
                MemfdRegion &operator=(const MemfdRegion &) = delete;

                /** @brief Move constructor — transfers fd and mapping ownership. */
                MemfdRegion(MemfdRegion &&other) noexcept;

                /** @brief Move assignment — closes @c *this then transfers ownership. */
                MemfdRegion &operator=(MemfdRegion &&other) noexcept;

                /**
                 * @brief Returns true when the region holds a valid fd and is not in a dead state.
                 *
                 * False before construction succeeds and after a @ref seal
                 * failure.  Callers can use this as a single predicate
                 * for "should I keep using this region".
                 */
                bool isValid() const;

                /** @brief Returns true once the region has been sealed. */
                bool isSealed() const;

                /** @brief Returns the rounded-up size in bytes. */
                size_t size() const { return _size; }

                /**
                 * @brief Returns the raw memfd file descriptor, or -1 if invalid.
                 *
                 * Useful for advanced uses — passing the fd to peers via
                 * @c SCM_RIGHTS, sending its content via @c sendfile, etc.
                 * The region retains ownership; do not close.
                 */
                int fd() const { return _fd; }

                /**
                 * @brief Returns the producer mapping (@c MAP_SHARED|PROT_WRITE), or null.
                 *
                 * Lazily mmaps on first call and caches the result for
                 * subsequent calls.  Returns null after the first
                 * successful @ref seal, or any time the region is
                 * invalid / dead.  The producer view is owned by the
                 * region — do not @ref releaseView it.
                 */
                void *producerView();

                /**
                 * @brief Transitions the region into the sealed (CoW-clone) phase.
                 *
                 * Idempotent — once sealed, subsequent calls return
                 * @ref Error::Ok and leave @p outFirstClone unchanged.
                 *
                 * The non-idempotent first call:
                 *   1. munmaps the cached producer view;
                 *   2. if @p outFirstClone is non-null, atomically
                 *      allocates a fresh @c MAP_PRIVATE clone (the
                 *      producer view is gone and no other clones exist
                 *      yet, so the new clone is a stable snapshot of
                 *      the producer's pre-seal content) and writes its
                 *      address there;
                 *   3. applies @c F_SEAL_WRITE|F_SEAL_SHRINK|F_SEAL_GROW
                 *      via @c F_ADD_SEALS.
                 *
                 * Failure transitions the region to a permanent dead
                 * state — see the class-level "Failure modes" section.
                 *
                 * @param outFirstClone Optional out-parameter for the
                 *                      atomic clone.  Pass null to seal
                 *                      without taking a clone (the
                 *                      read-only-IPC pattern).
                 * @return @c Error::Ok on success, otherwise an error
                 *         code describing the failure.
                 */
                Error seal(void **outFirstClone = nullptr);

                /**
                 * @brief Returns a fresh @c MAP_PRIVATE clone of the sealed region, or null.
                 *
                 * Each call returns a new virtual address; clones are
                 * independent (writes through one are CoW'd into that
                 * clone's per-page anonymous backing and never reach
                 * siblings).  The caller owns the returned mapping and
                 * must release it via @ref releaseView.
                 *
                 * @param err Optional out-parameter receiving the error
                 *            code on failure.  Set to
                 *            @ref Error::NotReady when the region is
                 *            valid but not yet sealed; @ref Error::Invalid
                 *            when the region is invalid or dead.
                 * @return The clone's base address, or null on error.
                 */
                void *cloneView(Error *err = nullptr);

                /**
                 * @brief Returns a @c MAP_SHARED|PROT_READ view of the sealed region, or null.
                 *
                 * Multiple read-only views over the same region share
                 * physical pages with the source — a cheap way for
                 * many readers to inspect a single producer's output.
                 * The caller owns the returned mapping and must release
                 * it via @ref releaseView.
                 *
                 * @param err Optional out-parameter receiving the error
                 *            code on failure.  Same encoding as
                 *            @ref cloneView.
                 * @return The view's base address, or null on error.
                 */
                void *readOnlyView(Error *err = nullptr);

                /**
                 * @brief Releases a caller-owned clone or read-only view.
                 *
                 * @c releaseView(nullptr) is a defensive no-op that
                 * returns @ref Error::Invalid — most libpromeki APIs
                 * crash on null inputs, but unwind paths frequently
                 * forward already-released pointers, and turning that
                 * into a crash is worse than a tolerated invalid.
                 *
                 * @param p Mapping address previously returned by
                 *          @ref cloneView or @ref readOnlyView.
                 * @return @c Error::Ok on success.
                 */
                Error releaseView(void *p);

                /**
                 * @brief Forwards @c madvise to the producer mapping (pre-seal only).
                 *
                 * Useful for performance hints such as @c MADV_HUGEPAGE
                 * on large frame allocations.  Returns @ref Error::NotReady
                 * once the producer view has been retired by @ref seal,
                 * @ref Error::NotSupported on builds without memfd
                 * support.
                 */
                Error adviseProducer(int madviseFlag);

                /**
                 * @brief Forwards @c madvise to a caller-owned view.
                 *
                 * @param p           A clone or read-only view returned
                 *                    by @ref cloneView / @ref readOnlyView.
                 * @param madviseFlag One of the @c MADV_* flags.
                 */
                Error adviseView(void *p, int madviseFlag);

        private:
                int    _fd;          // -1 when not held
                size_t _size;        // rounded-up
                void  *_producer;    // cached MAP_SHARED|PROT_WRITE; null pre-mmap or post-seal
                bool   _sealed;
                bool   _dead;        // permanent failure latched after a failed seal()

                void closeAndReset() noexcept;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_CORE
