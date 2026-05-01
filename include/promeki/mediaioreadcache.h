/**
 * @file      mediaioreadcache.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/list.h>
#include <promeki/mutex.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaiorequest.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIOSource;

/**
 * @brief Producer-side prefetch buffer for @ref MediaIOSource reads.
 * @ingroup mediaio_backend
 *
 * A @ref MediaIOReadCache owns the "keep N reads in flight" policy for
 * a single source.  It maintains a queue of @ref MediaIOCommandRead
 * shared pointers — some in-flight on the strand, some already
 * completed and waiting to be vended — and serves one read per
 * @ref MediaIOSource::readFrame call by handing back a request bound
 * to the head of that queue.
 *
 * @par Invariants
 *  - The queue holds at most @ref depth() commands at any time.
 *  - Vended requests stay valid forever (they share ownership of the
 *    cmd via @ref MediaIOCommand::Ptr).  Dropping a not-yet-completed
 *    request silently discards the produced frame; the strand still
 *    runs the cmd to completion.
 *  - After every @ref readFrame call the cache tops up to
 *    @ref depth() so the prefetch invariant is restored before
 *    returning.
 *
 * @par frameReady semantics
 * @ref MediaIOSource::frameReady fires on the transition "head of
 * the cache becomes ready."  Concretely, the cache emits the signal
 * exactly once between any pair of @ref readFrame pops; once a head
 * is "armed" it stays armed until @ref readFrame consumes it (at
 * which point the cache re-checks the new head and re-arms if it is
 * already complete).  Polling consumers can therefore wait on
 * @c frameReady and trust that a subsequent @ref readFrame will
 * resolve without blocking.
 *
 * @par Threading
 * Public methods are safe to call from any thread.  Internally a
 * @ref Mutex serializes queue mutation; @ref onCommandCompleted is
 * called from the strand worker via @ref MediaIO::completeCommand,
 * while @ref readFrame is called from the user thread.
 *
 * @par Lifetime
 * The cache is owned by its @ref MediaIOSource as a value member, so
 * the back-pointer to the source is stable for the life of the cache.
 * Callers must drain the strand (e.g. by closing the
 * @ref MediaIO) before destroying the source — the
 * @ref MediaIO destructor handles this with @c _strand.waitForIdle().
 */
class MediaIOReadCache {
        public:
                /**
                 * @brief Constructs a cache bound to @p source.
                 *
                 * The @p source's owning @ref MediaIO and
                 * @ref MediaIOPortGroup must already be wired before
                 * the cache is asked to submit anything (typically
                 * the case because the cache is a value member of
                 * @p source).
                 */
                explicit MediaIOReadCache(MediaIOSource *source);

                /** @brief Destructor.  Caller is responsible for draining the strand first. */
                ~MediaIOReadCache();

                MediaIOReadCache(const MediaIOReadCache &) = delete;
                MediaIOReadCache &operator=(const MediaIOReadCache &) = delete;

                /**
                 * @brief Sets the target prefetch depth.
                 *
                 * Clamped to a minimum of 1.  The new depth applies
                 * on the next @ref readFrame top-up; the cache does
                 * not retroactively shrink an over-full queue (which
                 * can happen briefly if the depth is lowered while
                 * reads are in flight).
                 */
                void setDepth(int n);

                /** @brief Returns the configured prefetch depth. */
                int depth() const;

                /**
                 * @brief Returns the current number of cmds the cache holds.
                 *
                 * Sum of in-flight + completed commands queued for
                 * future vending.  Matches the number of slots the
                 * cache is currently using.
                 */
                int count() const;

                /** @brief True when @ref count is zero. */
                bool isEmpty() const;

                /**
                 * @brief True when the head cmd has completed.
                 *
                 * A subsequent @ref readFrame call is guaranteed to
                 * return a request whose @c wait(0) resolves
                 * immediately with the head's outcome.
                 */
                bool isHeadReady() const;

                /**
                 * @brief Vends the next read.
                 *
                 * Returns a request bound to the head of the queue.
                 * If the queue is empty, submits a fresh
                 * @ref MediaIOCommandRead first.  Tops up the queue
                 * to @ref depth before returning so the next caller
                 * sees a primed prefetch.
                 *
                 * The returned request's @c wait will block when the
                 * head was still in flight (and resolves when the
                 * strand worker completes the cmd) or fire
                 * immediately when the head was already complete
                 * (cache hit).  Either way the cmd's typed payload
                 * (@ref MediaIOCommandRead::frame, @c currentFrame,
                 * descriptor-change flags) is reachable via
                 * @c req.commandAs<MediaIOCommandRead>() once the
                 * request resolves.
                 */
                MediaIORequest readFrame();

                /**
                 * @brief Drains the queue and cancels every held cmd.
                 *
                 * Marks every queued cmd cancelled (so the strand
                 * worker short-circuits if it has not yet started)
                 * and clears the queue.  Already-vended requests are
                 * unaffected — their underlying cmd still completes
                 * (with @c Error::Cancelled when the cancel raced
                 * dispatch) and the request resolves normally.
                 *
                 * @return The number of cmds dropped from the queue.
                 */
                size_t cancelAll();

                /**
                 * @brief Pushes an already-completed synthetic read result.
                 *
                 * Used by @ref MediaIO::completeCommand on the close
                 * path to deliver a trailing EOS to consumers.  The
                 * synthetic cmd carries @p err in its
                 * @ref MediaIOCommand::result and is marked completed
                 * so @ref readFrame returns it as an immediately-
                 * resolved request.  The frameReady armed flag is
                 * re-evaluated after the push so polling consumers
                 * observe the synthetic result through the same
                 * mechanism as a live read.
                 */
                void pushSyntheticResult(Error err);

                /**
                 * @brief Notifies the cache that some held cmd just completed.
                 *
                 * Called from @ref MediaIO::completeCommand on the
                 * strand worker thread after the per-Read cache
                 * writes have run but before the cmd's
                 * @ref MediaIOCommand::markCompleted fires.  Drives
                 * the @ref MediaIOSource::frameReady edge-detection.
                 *
                 * Safe to call when the completed cmd is no longer
                 * in the queue (for example, after @ref cancelAll
                 * dropped it but before the strand worker noticed).
                 * In that case the cache simply re-evaluates the
                 * armed flag against the current head.
                 */
                void onCommandCompleted();

        private:
                MediaIOCommand::Ptr submitOneLocked();
                bool                checkArmedLocked();

                MediaIOSource              *_source = nullptr;
                List<MediaIOCommand::Ptr>   _queue;
                int                         _depth = 1;
                bool                        _headReadyArmed = false;
                mutable Mutex               _mutex;
};

PROMEKI_NAMESPACE_END
