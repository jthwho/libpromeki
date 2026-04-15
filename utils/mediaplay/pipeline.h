/**
 * @file      mediaplay/pipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Signal-driven `source -> stage[0] -> stage[1] -> ... -> sink(s)` pump
 * used by mediaplay.  Runs entirely off MediaIO signals on the
 * main-thread EventLoop; no dedicated pumper thread.  Supports any
 * number of intermediate MediaIO stages (converter, video encoder,
 * video decoder, …) chained back-to-back between the source and the
 * fan-out of sinks.
 */

#pragma once

#include <promeki/atomic.h>
#include <promeki/error.h>
#include <promeki/eventloop.h>
#include <promeki/list.h>
#include <promeki/mediaio.h>
#include <promeki/objectbase.h>
#include <promeki/string.h>

namespace mediaplay {

// The PROMEKI_OBJECT / PROMEKI_SLOT macros expand to unqualified
// references against library types (Slot, SlotMeta, SignalMeta,
// MetaInfo, ObjectBase, ...).  Bringing the promeki namespace in
// here scopes the `using` to this namespace only — it does not
// leak out to files that include this header at file scope.
using namespace promeki;

/**
 * @brief One output attached to the pipeline.
 *
 * Populated by main() after each `--out` has been built.
 */
struct Sink {
        MediaIO     *io    = nullptr;
        String       name;
        bool         isFile = false;   ///< True for backends opened via createForFileWrite.
        String       path;             ///< File path for logging.
};

/**
 * @brief Signal-driven media pump with N intermediate stages.
 *
 * Connects to the source's @c frameReadySignal and drains frames on
 * the main-thread EventLoop.  Every frame that comes out of the
 * source walks through the configured chain of intermediate
 * @c MediaIO stages (zero or more) before being fanned out to every
 * sink via @c writeFrame(frame, false).
 *
 * The pipeline is modelled as a single @ref _chain vector:
 *
 *   @ref _chain[0]       — the source
 *   @ref _chain[1..N]    — N intermediate stages, in order
 *
 * The stage at index @c i reads from @ref _chain[i] and writes to
 * @ref _chain[i+1] when @c i+1 is in range, otherwise to every sink.
 * Back-pressure: each @c frameWantedSignal call reopens the drain on
 * the @em upstream side of the signalling stage.
 *
 * Shutdown semantics:
 *  - EOF / frame-count reached → stages are drained to the sinks
 *    before @c finish(0) (clean).
 *  - Any write error → @c finish(1) (not clean).
 *  - External quit → `app.exec()` returns with the shouldQuit rc;
 *    @c finishedCleanly() stays @c false so the caller cancels
 *    pending sink work instead of letting it drain.
 */
class Pipeline : public ObjectBase {
        PROMEKI_OBJECT(Pipeline, ObjectBase)
        public:
                Pipeline(MediaIO *source,
                         List<MediaIO *> stages,
                         List<Sink> sinks,
                         int64_t frameCountLimit,
                         ObjectBase *parent = nullptr);

                /// @brief Primes the drain loop via a single synthetic pump.
                void start();

                /// @brief Total frames that made it past every sink's writeFrame submit.
                uint64_t framesPumped() const { return _framesPumped; }

                /// @brief True only if finish() was called with rc==0.
                bool finishedCleanly() const { return _finished && _cleanFinish; }

                const List<Sink> &sinks() const { return _sinks; }
                List<Sink> &sinks() { return _sinks; }

                PROMEKI_SLOT(onWriteErrorPosted);

        private:
                void reportWriteError(Error err);
                void reportFrameWanted(size_t stageIdx);
                void onFrameWantedPosted(size_t stageIdx);

                // Drain the stage at index @p idx in @ref _chain.  When
                // @c idx is @c _chain.size()-1 the output is fanned out
                // to every sink; otherwise it is written to
                // @ref _chain[idx+1].  Returns silently on back-pressure.
                void drainChain(size_t idx);

                bool fanOutToSinks(const Frame::Ptr &frame);
                bool sinksCanAccept() const;
                void finish(int rc);

                // Returns true when @p idx has no unconsumed work left
                // (its own input exhausted and its output queue drained).
                bool chainTailEmpty(size_t idx) const;

                // Source is always chain[0]; any intermediate stages
                // follow.  Keeping them in one contiguous list makes
                // the drain loop symmetric — drainChain(i) walks
                // chain[i] and writes to chain[i+1] or sinks.
                List<MediaIO *>     _chain;

                // Per-stage EOF bookkeeping.  _chainDone[i] is set when
                // chain[i] is known to have exhausted its upstream AND
                // its own pending output queue is empty — at which
                // point chain[i+1] (or the sinks) can stop waiting for
                // more input from it.  _chainDone[0] tracks the source
                // hitting EOF.
                // Uses `int` (not `bool`) because promeki::List<T>
                // wraps std::vector<T> and std::vector<bool>'s proxy
                // references don't play well with `_chainDone[i] = ...`.
                List<int>           _chainDone;

                List<Sink>          _sinks;
                int64_t             _frameCountLimit = 0;
                uint64_t            _framesPumped = 0;
                bool                _finished = false;
                bool                _cleanFinish = false;
                EventLoop          *_mainLoop = nullptr;
                Atomic<bool>        _writeErrorPending{false};
                Error               _writeError;
};

} // namespace mediaplay
