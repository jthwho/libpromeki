/**
 * @file      mediaplay/pipeline.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 *
 * Signal-driven `source -> [converter] -> sink(s)` pump used by
 * mediaplay.  Runs entirely off MediaIO signals on the main-thread
 * EventLoop; no dedicated pumper thread.
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
 * @brief Signal-driven media pump with optional Converter stage.
 *
 * Connects to the source's @c frameReadySignal and drains frames
 * on the main-thread EventLoop.  Every frame that comes out of the
 * source is optionally routed through a blocking
 * @c writeFrame+readFrame round-trip on the Converter, then
 * fanned out to every sink via @c writeFrame(frame, false).  Sink
 * @c frameWantedSignal callbacks call @c writesAccepted() to
 * reopen the drain once back-pressure eases; sink
 * @c writeErrorSignal callbacks finalize the pipeline with a
 * non-zero exit code.
 *
 * Shutdown semantics:
 *  - EOF / frame-count reached → converter pipeline is drained
 *    to the sinks before @c finish(0) (clean).
 *  - Source or sink error → @c finish(1) (not clean).
 *  - External quit → `app.exec()` returns with the shouldQuit rc;
 *    @c finishedCleanly() stays @c false so the caller cancels
 *    pending sink work instead of letting it drain.
 */
class Pipeline : public ObjectBase {
        PROMEKI_OBJECT(Pipeline, ObjectBase)
        public:
                Pipeline(MediaIO *source,
                         MediaIO *converter,
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

                PROMEKI_SLOT(onSourceFrameReady);
                PROMEKI_SLOT(onConverterFrameReady);
                PROMEKI_SLOT(onWriteErrorPosted);

        private:
                void reportWriteError(Error err);
                void reportSinkFrameWanted();
                void reportConverterFrameWanted();
                void onSinkFrameWantedPosted();
                void onConverterFrameWantedPosted();

                void drainSource();
                void drainConverter();
                bool fanOutToSinks(const Frame::Ptr &frame);
                bool sinksCanAccept() const;
                void finish(int rc);

                MediaIO            *_source = nullptr;
                MediaIO            *_converter = nullptr;
                List<Sink>          _sinks;
                int64_t             _frameCountLimit = 0;
                uint64_t            _framesPumped = 0;
                bool                _finished = false;
                bool                _cleanFinish = false;
                bool                _sourceAtEnd = false;
                EventLoop          *_mainLoop = nullptr;
                Atomic<bool>        _writeErrorPending{false};
                Error               _writeError;
};

} // namespace mediaplay
