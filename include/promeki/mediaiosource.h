/**
 * @file      mediaiosource.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/error.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/framecount.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaioport.h>
#include <promeki/mediaio.h>
#include <promeki/mediaioreadcache.h>

PROMEKI_NAMESPACE_BEGIN

class MediaIO;
class MediaIOPortGroup;
class MediaIOCommand;
class MediaIORequest;

/**
 * @brief Read-side port of a @ref MediaIO.
 * @ingroup mediaio_user
 *
 * A @ref MediaIOSource produces frames out of a @ref MediaIO instance.
 * It carries the per-port read API — @c readFrame, the per-port read
 * queue, format negotiation (@ref proposeOutput), and the per-port
 * @c frameReady signal.  Step / current-frame / seek state lives on
 * the source's @ref MediaIOPortGroup, so multi-source groups stay in
 * lockstep by definition; the methods on this class are convenience
 * forwarders to the group.
 *
 * Sources are created by the backend during open and parented
 * to their @ref MediaIOPortGroup, which is in turn parented to the
 * owning @ref MediaIO.
 *
 * @par Default name
 * If the @p name argument is empty the constructor synthesizes
 * @c "src{index}" (e.g. @c "src0", @c "src1") so every source has a
 * stable, human-readable identifier even when the backend has nothing
 * specific to call it.
 *
 * @par Port groups
 * All sources in a group share the group's clock, step,
 * current-frame, and seek state.  The multi-source "atomic
 * distribute one frame per source per @ref MediaIOCommandRead" path
 * is reserved for a future phase — today @ref MediaIOCommandRead
 * carries a single @c Frame::Ptr targeted at one source within the
 * group, and sources advance their queues one read at a time.
 * Sources in single-port groups behave independently.
 */
class MediaIOSource : public MediaIOPort {
                PROMEKI_OBJECT(MediaIOSource, MediaIOPort)
                friend class MediaIO;
                friend class MediaIOPortGroup;
        public:
                /**
                 * @brief Constructs a source port and binds it to @p group.
                 *
                 * @param group The port group this source belongs to;
                 *              must be non-null.
                 * @param index Per-type source index assigned by the
                 *              creating @ref MediaIO.
                 * @param name  Optional human-readable port name.  If
                 *              empty, defaults to @c "src{index}".
                 */
                MediaIOSource(MediaIOPortGroup *group, int index, const String &name = String());

                /** @brief Destructor. */
                ~MediaIOSource() override;

                /** @brief Always returns @c MediaIOPort::Source. */
                Role role() const override { return MediaIOPort::Source; }

                // ---- Capacity / status ----

                /**
                 * @brief True when the next @ref readFrame would return immediately.
                 *
                 * Forwards to @ref MediaIOReadCache::isHeadReady —
                 * matches the @c frameReady signal contract: when
                 * @c true, a subsequent @ref readFrame returns a
                 * request whose @c wait(0) resolves without blocking.
                 */
                bool frameAvailable() const;

                /** @brief Returns the number of cmds the cache currently holds. */
                int readyReads() const;

                /**
                 * @brief Returns the number of read commands in flight on this group.
                 *
                 * Forwards to the owning @ref MediaIOPortGroup —
                 * pending-read accounting is per-group so paired
                 * multi-source groups roll up to one tick per group.
                 * In a single-source group this is indistinguishable
                 * from a per-source count.
                 */
                int pendingReads() const;

                /**
                 * @brief Returns the number of read commands the source keeps in flight.
                 *
                 * Default is set by the backend during open and may be
                 * overridden per-source via @ref setPrefetchDepth.
                 * Forwards to the source's @ref MediaIOReadCache.
                 */
                int prefetchDepth() const { return _readCache.depth(); }

                /**
                 * @brief Sets the number of read commands the source keeps in flight.
                 *
                 * Larger values mask backend latency at the cost of
                 * memory / live-source freshness.  Each @ref readFrame
                 * call tops up the cache to this many outstanding
                 * commands before returning.
                 */
                void setPrefetchDepth(int n);

                // ---- Format negotiation ----

                /**
                 * @brief Negotiates an achievable output format with the backend.
                 *
                 * Forwards to the backend task's @c proposeOutput
                 * virtual.  The backend examines @p requested and
                 * returns @p achievable — what it could actually
                 * produce given that request.
                 */
                Error proposeOutput(const MediaDesc &requested, MediaDesc *achievable) const;

                // ---- Read API ----

                /**
                 * @brief Reads the next frame from the source.
                 *
                 * Vends a request bound to the next prefetched read
                 * from the source's @ref MediaIOReadCache.  Always
                 * returns immediately with a valid request — the
                 * underlying cmd may already have completed (cache
                 * hit) or may still be in flight (cache miss).
                 * Callers retrieve the typed payload via
                 * @c req.commandAs<MediaIOCommandRead>() once the
                 * request resolves:
                 *
                 * @code
                 * MediaIORequest req = source->readFrame();
                 * Error err = req.wait();
                 * if (err.isOk()) {
                 *     Frame::Ptr frame = req.commandAs<MediaIOCommandRead>()->frame;
                 *     // ... use frame ...
                 * }
                 * @endcode
                 *
                 * @par EOF semantics
                 * Once a read returns @c Error::EndOfFile, the group
                 * latches end-of-stream and stops issuing further
                 * prefetch reads.  Subsequent calls keep returning
                 * @c Error::EndOfFile (without going down to the
                 * backend) until the user calls
                 * @ref MediaIOPortGroup::seekToFrame or
                 * @c MediaIO::close to reset the state.
                 *
                 * @par Pre-open / closed
                 * Returns an immediately-resolved @c Error::NotOpen
                 * request when the owning @ref MediaIO is not open.
                 * Returns @c Error::Invalid when the source is
                 * detached from its @ref MediaIO or group.
                 *
                 * @par Cancellation
                 * Already-vended requests resolve with
                 * @c Error::Cancelled when @ref cancelPending or
                 * @ref MediaIORequest::cancel is invoked before the
                 * underlying cmd dispatches.
                 */
                MediaIORequest readFrame();

                /**
                 * @brief Cancels all pending reads and discards queued results.
                 *
                 * Drains the cache and marks every queued cmd
                 * cancelled.  Already-vended requests resolve with
                 * @c Error::Cancelled.
                 *
                 * @return The total number of cmds dropped from the
                 *         cache.
                 */
                size_t cancelPending();

                // ---- Navigation (forwards to the port group) ----

                /** @brief Returns @c group()->step(). */
                int step() const;

                /** @brief Forwards to @c group()->setStep(). */
                void setStep(int val);

                /** @brief Returns @c group()->currentFrame(). */
                FrameNumber currentFrame() const;

                /** @brief Forwards to @c group()->seekToFrame(). */
                MediaIORequest seekToFrame(const FrameNumber &frameNumber,
                                           MediaIOSeekMode mode = MediaIO_SeekDefault);

                // ---- Signal ----

                /**
                 * @brief Emitted on the transition "next read is ready."
                 * @signal
                 *
                 * The contract is "if you call @ref readFrame right
                 * now, the returned request is already resolved."
                 * Concretely the signal fires on every transition
                 * from "head of the cache not ready" to "head of
                 * the cache ready" — this includes the first read
                 * that finishes after a fresh prefetch as well as a
                 * second read landing while the first is still
                 * unconsumed.  After the consumer pops via
                 * @ref readFrame the cache re-evaluates the new
                 * head and re-fires the signal if that one is
                 * already complete too.
                 *
                 * EOF and error completions count: a synthetic EOS
                 * pushed during close, or a real backend error,
                 * surfaces through the same edge as a real frame
                 * delivery so the consumer can observe the outcome
                 * by simply calling @ref readFrame and waiting on
                 * the request.
                 */
                PROMEKI_SIGNAL(frameReady);

        private:
                MediaIOReadCache _readCache{this};
                // True once a caller has explicitly set the prefetch
                // depth via @ref setPrefetchDepth — silences the
                // post-open re-apply of the backend's
                // @c defaultPrefetchDepth so the user override sticks.
                bool             _prefetchDepthExplicit = false;
};

PROMEKI_NAMESPACE_END
