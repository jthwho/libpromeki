/**
 * @file      mediaioportconnection.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/eventloop.h>
#include <promeki/frame.h>
#include <promeki/mediaiocommand.h>
#include <promeki/mediaioportconnection.h>
#include <promeki/mediaiorequest.h>
#include <promeki/mediaiosink.h>
#include <promeki/mediaiosource.h>
#include <promeki/objectbase.tpp>

PROMEKI_NAMESPACE_BEGIN

MediaIOPortConnection::MediaIOPortConnection(MediaIOSource *source, MediaIOSink *sink, ObjectBase *parent)
    : ObjectBase(parent), _source(source) {
        if (sink != nullptr) {
                SinkState ss;
                ss.sink = sink;
                _sinks.pushToBack(ss);
        }
}

MediaIOPortConnection::MediaIOPortConnection(MediaIOSource *source, ObjectBase *parent)
    : ObjectBase(parent), _source(source) {}

MediaIOPortConnection::~MediaIOPortConnection() {
        if (_running) stop();
}

MediaIOSink *MediaIOPortConnection::sink() const {
        return _sinks.isEmpty() ? nullptr : _sinks[0].sink;
}

promeki::List<MediaIOSink *> MediaIOPortConnection::sinks() const {
        promeki::List<MediaIOSink *> out;
        for (size_t i = 0; i < _sinks.size(); ++i) out.pushToBack(_sinks[i].sink);
        return out;
}

Error MediaIOPortConnection::addSink(MediaIOSink *sink, FrameCount frameLimit) {
        if (sink == nullptr) return Error::Invalid;
        if (_running) return Error::Busy;
        SinkState ss;
        ss.sink = sink;
        ss.frameLimit = frameLimit;
        _sinks.pushToBack(ss);
        return Error::Ok;
}

int64_t MediaIOPortConnection::framesWritten(MediaIOSink *sink) const {
        const SinkState *ss = findSinkState(sink);
        return ss == nullptr ? 0 : ss->framesWritten;
}

bool MediaIOPortConnection::sinkStopped(MediaIOSink *sink) const {
        const SinkState *ss = findSinkState(sink);
        if (ss == nullptr) return true;
        return ss->stopped || ss->doneByLimit;
}

MediaIOPortConnection::SinkState *MediaIOPortConnection::findSinkState(MediaIOSink *sink) {
        for (size_t i = 0; i < _sinks.size(); ++i) {
                if (_sinks[i].sink == sink) return &_sinks[i];
        }
        return nullptr;
}

const MediaIOPortConnection::SinkState *MediaIOPortConnection::findSinkState(MediaIOSink *sink) const {
        for (size_t i = 0; i < _sinks.size(); ++i) {
                if (_sinks[i].sink == sink) return &_sinks[i];
        }
        return nullptr;
}

Error MediaIOPortConnection::start() {
        if (_source == nullptr) return Error::Invalid;
        if (_sinks.isEmpty()) return Error::Invalid;
        if (_running) return Error::Ok;
        _running = true;
        _upstreamDone = false;
        _allSinksDoneEmitted = false;

        // Source-side wakeup: a fresh frame on the source kicks the
        // pump regardless of which sink most recently freed up.  Sink
        // capacity reopening is observed via per-sink frameWanted
        // signals.  Both routes funnel through the same coalescing
        // scheduler so a burst of strand-completion signals does not
        // saturate the EventLoop with one pump callable per signal.
        _source->frameReadySignal.connect([this]() { schedulePump(); }, this);

        for (size_t i = 0; i < _sinks.size(); ++i) {
                MediaIOSink *snk = _sinks[i].sink;
                if (snk == nullptr) continue;
                snk->frameWantedSignal.connect([this]() { schedulePump(); }, this);
                // Async strand-side write errors funnel into the same
                // sinkError surface as synchronous writeFrame failures
                // so consumers have a single signal to listen on.
                snk->writeErrorSignal.connect([this, snk](Error e) { onSinkWriteError(snk, e); }, this);
        }

        // Prime: there might already be frames queued on the source,
        // or the sinks might already be hungry.  Either way, an
        // initial pump kicks the read prefetch.
        pump();
        return Error::Ok;
}

void MediaIOPortConnection::schedulePump() {
        // Coalesce: only one pump callable is queued on the loop at
        // a time.  The flag is reset at the start of pump() so the
        // *next* signal after pump returns can queue another pass.
        bool expected = false;
        if (!_pumpScheduled.compareAndSwap(expected, true)) return;
        EventLoop *loop = EventLoop::current();
        if (loop != nullptr) {
                loop->postCallable([this]() { pump(); });
        } else {
                // No loop on this thread — fall back to a synchronous
                // call.  This typically only happens when a test
                // wires the connection up on a thread that hasn't
                // installed a loop, which is itself a misuse, but
                // the synchronous path keeps behavior coherent.
                _pumpScheduled.setValue(false);
                pump();
        }
}

void MediaIOPortConnection::stop() {
        if (!_running) return;
        _running = false;
        if (_source != nullptr) _source->frameReadySignal.disconnectFromObject(this);
        for (size_t i = 0; i < _sinks.size(); ++i) {
                MediaIOSink *snk = _sinks[i].sink;
                if (snk == nullptr) continue;
                snk->frameWantedSignal.disconnectFromObject(this);
                snk->writeErrorSignal.disconnectFromObject(this);
        }
        // Drop the held read so a stop()'d connection does not keep
        // the underlying cmd alive past the source's lifetime.  The
        // cmd itself is still owned by the strand and resolves
        // normally regardless.
        _pendingRead = MediaIORequest();
        stoppedSignal.emit();
}

void MediaIOPortConnection::onSinkWriteError(MediaIOSink *sink, Error err) {
        if (!_running) return;
        // TryAgain is normal back-pressure feedback; ignore it.
        if (err == Error::TryAgain) return;
        SinkState *ss = findSinkState(sink);
        if (ss == nullptr) return;
        if (ss->stopped) return;
        ss->stopped = true;
        sinkErrorSignal.emit(sink, err);
        maybeEmitAllSinksDone();
}

bool MediaIOPortConnection::everyActiveSinkAcceptsWrite() const {
        bool anyActive = false;
        for (size_t i = 0; i < _sinks.size(); ++i) {
                const SinkState &ss = _sinks[i];
                if (ss.sink == nullptr) continue;
                if (ss.stopped || ss.doneByLimit) continue;
                anyActive = true;
                if (ss.sink->writesAccepted() <= 0) return false;
        }
        return anyActive;
}

void MediaIOPortConnection::maybeEmitAllSinksDone() {
        if (_allSinksDoneEmitted) return;
        for (size_t i = 0; i < _sinks.size(); ++i) {
                const SinkState &ss = _sinks[i];
                if (ss.sink == nullptr) continue;
                if (!(ss.stopped || ss.doneByLimit)) return;
        }
        _allSinksDoneEmitted = true;
        allSinksDoneSignal.emit();
}

void MediaIOPortConnection::pump() {
        // Clear the scheduled flag at entry so the very next signal
        // (which may fire while pump is still running) queues a
        // fresh pump invocation rather than getting coalesced into
        // this one.
        _pumpScheduled.setValue(false);
        if (!_running) return;
        if (_source == nullptr) return;
        // Once the source has reached EOF, further readFrame calls
        // will surface @c Error::NotOpen as the source's strand winds
        // down — that is expected close-cascade plumbing rather than
        // an operational error.  Bail out so we don't translate it
        // into a connection-level errorOccurred.
        if (_upstreamDone) return;

        // Pure signal-driven model: pump never blocks the
        // EventLoop thread and never reschedules itself
        // unconditionally.  Every yield path has a corresponding
        // signal that re-fires pump:
        //
        //   - sink back-pressure (writesAccepted() <= 0) →
        //     @c MediaIOSink::frameWantedSignal fires from
        //     @c MediaIO::completeCommand on the strand worker
        //     after a write drains, re-enabling pump.
        //   - read in flight (request not yet ready) → an
        //     @c MediaIORequest::then continuation re-fires pump
        //     when the strand worker resolves the cmd.  Routing
        //     through @ref schedulePump coalesces with overlapping
        //     wakeups (e.g. the cache armed-edge below).
        //   - read resolved with @c TryAgain or a null frame
        //     (transform-style upstream's output queue is
        //     transiently empty) →
        //     @c MediaIOSource::frameReadySignal fires when the
        //     cache head transitions to ready, which happens as
        //     soon as a fresh prefetch completes with real data
        //     (driven by the upstream connection writing into the
        //     transform's sink).
        //   - sync write returns @c TryAgain → next
        //     @c frameWantedSignal from that sink re-fires pump.
        //   - async write completion → @c frameWantedSignal
        //     (success) or @c writeErrorSignal (failure) fires
        //     from the strand worker, both connected in
        //     @ref start.
        //
        // Single-frame-per-call: the pump dispatches at most one
        // frame per invocation and yields back to the EventLoop.

        // Back-pressure gate.  If every sink has stopped, fall
        // through into drain-only mode so the upstream EOF/error
        // path still surfaces to consumers.  If at least one sink
        // is active but back-pressured, wait for the next
        // @c frameWanted.
        if (!_allSinksDoneEmitted) {
                bool anyActive = false;
                bool blocked = false;
                for (size_t i = 0; i < _sinks.size(); ++i) {
                        const SinkState &ss = _sinks[i];
                        if (ss.sink == nullptr) continue;
                        if (ss.stopped || ss.doneByLimit) continue;
                        anyActive = true;
                        if (ss.sink->writesAccepted() <= 0) {
                                blocked = true;
                                break;
                        }
                }
                if (!anyActive) {
                        maybeEmitAllSinksDone();
                        // Fall through into drain-only mode.
                } else if (blocked) {
                        return;
                }
        }

        if (!_pendingRead.isValid()) {
                _pendingRead = _source->readFrame();
        }
        if (!_pendingRead.isReady()) {
                // Read still in flight.  Attach a continuation so
                // pump runs on this EventLoop the moment the
                // request resolves; this is the only reliable
                // signal that the held cmd is done — the cache's
                // @c frameReady contract concerns the cache head
                // and may never fire for a cmd we already vended.
                // Route through @ref schedulePump so the wakeup
                // coalesces with any other signals racing for the
                // same loop turn.
                //
                // Note: do NOT call @ref MediaIORequest::wait(0)
                // here.  @c 0 is the documented "wait indefinitely"
                // sentinel in @ref MediaIOCommand::waitForCompletion,
                // not a non-blocking poll, so it would deadlock the
                // EventLoop thread waiting on a cmd whose completion
                // path may itself need this loop to dispatch.
                MediaIORequest holder = _pendingRead;
                holder.then([this](Error) { schedulePump(); });
                return;
        }
        // Already resolved — @ref MediaIORequest::wait takes the
        // fast path in @ref MediaIOCommand::waitForCompletion and
        // returns immediately.
        Error      rerr = _pendingRead.wait();
        Frame::Ptr frame;
        if (rerr.isOk()) {
                if (const auto *cr = _pendingRead.commandAs<MediaIOCommandRead>()) {
                        frame = cr->frame;
                }
        }
        // Consumed (or about to be classified as EOF/error) —
        // clear so the next pump invocation grabs a fresh request.
        _pendingRead = MediaIORequest();
        // Both @c EndOfFile and @c NotOpen surface to the connection
        // as a clean "no more frames from this upstream" signal.
        // @c EndOfFile is the trailing synthetic that
        // @ref MediaIO::completeCommand pushes when a Close cmd
        // resolves; @c NotOpen surfaces from
        // @ref MediaIOSource::readFrame when @c io->isClosing() is
        // already latched but the synthetic EOS has not yet been
        // pushed (the strand has the Close queued behind in-flight
        // work).  Without this branch a cascade that races the
        // strand here would surface @c NotOpen as a generic error,
        // stop the connection, and starve the downstream cascade
        // of the @c upstreamDone edge it needs to propagate close.
        if (rerr == Error::EndOfFile || rerr == Error::NotOpen) {
                if (!_upstreamDone) {
                        _upstreamDone = true;
                        upstreamDoneSignal.emit();
                }
                return;
        }
        if (rerr == Error::TryAgain) {
                // Transform-style upstreams (CSC, SRC, FrameSync,
                // VideoEncoder, …) return @c Error::TryAgain
                // when their internal output queue is transiently
                // empty.  Yield without rescheduling — the
                // upstream's next successful Write will re-fire
                // @ref MediaIOSource::frameReadySignal on this
                // source (see @ref MediaIO::completeCommand for
                // the Write case), which kicks pump.  Reposting
                // pump here would tight-loop through the strand
                // round-trip until the upstream produces, and at
                // strand throughput rates (~ μs) that starves
                // every other connection on the EventLoop —
                // including the upstream pump that would have
                // produced the input we are waiting for.
                return;
        }
        if (rerr.isError()) {
                errorOccurredSignal.emit(rerr);
                _running = false;
                return;
        }

        // Read succeeded but produced no frame (transient
        // producer hiccup).  Same logic as the @c TryAgain path:
        // wait for the next @c frameReady rather than
        // tight-looping.
        if (!frame.isValid()) {
                return;
        }

        if (!_allSinksDoneEmitted) {
                bool deliveredAny = false;
                for (size_t i = 0; i < _sinks.size(); ++i) {
                        SinkState &ss = _sinks[i];
                        if (ss.sink == nullptr) continue;
                        if (ss.stopped || ss.doneByLimit) continue;

                        // Per-sink frame-count cap.  Honoured at
                        // the next safe cut point on or after the
                        // limit-th write so the GOP / audio packet
                        // boundary containing the cap stays
                        // complete.  The cap frame itself is
                        // dropped for this sink and the sink stops
                        // accepting further writes.
                        if (ss.frameLimit.isFinite() && !ss.frameLimit.isEmpty() &&
                            ss.framesWritten >= ss.frameLimit.value() && frame->isSafeCutPoint()) {
                                ss.doneByLimit = true;
                                sinkLimitReachedSignal.emit(ss.sink);
                                continue;
                        }

                        // Submit the write.  Only the
                        // synchronously-resolved cases need
                        // inline inspection:
                        //   - @c TryAgain → capacity dropped to
                        //     zero between the back-pressure
                        //     gate above and this submit; bail
                        //     this round, the sink's
                        //     @c frameWanted will re-fire pump.
                        //   - other error → mark the sink
                        //     stopped and surface via
                        //     @c sinkErrorSignal.
                        // For pending (in-flight) writes, no
                        // inline wait or @c then() is needed:
                        // @c MediaIO::completeCommand emits
                        // @c frameWanted on success or
                        // @c writeError on failure, both
                        // connected in @ref start, so the strand
                        // resolution naturally drives pump.
                        MediaIORequest writeReq = ss.sink->writeFrame(frame);
                        if (writeReq.isReady()) {
                                Error werr = writeReq.wait();
                                if (werr == Error::TryAgain) {
                                        return;
                                }
                                if (werr.isError()) {
                                        ss.stopped = true;
                                        sinkErrorSignal.emit(ss.sink, werr);
                                        continue;
                                }
                        }
                        ss.framesWritten++;
                        deliveredAny = true;
                }

                if (deliveredAny) _framesTransferred.fetchAndAdd(1);

                // A delivery round may have stopped the last
                // active sink (limit + error combos); check
                // before yielding.
                maybeEmitAllSinksDone();
        }

        // No explicit re-pump scheduling.  Subsequent pumps are
        // fired by:
        //   - @c frameWanted from any sink — after a write drains.
        //   - @c frameReady from the source — after the cache head
        //     transitions to ready.
        //   - @c .then() on the held @c _pendingRead — when the
        //     in-flight read resolves (only attached in the
        //     early-return branch above).
}

PROMEKI_NAMESPACE_END
