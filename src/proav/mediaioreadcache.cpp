/**
 * @file      mediaioreadcache.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/mediaioreadcache.h>
#include <promeki/mediaiosource.h>
#include <promeki/mediaioportgroup.h>
#include <promeki/mediaio.h>

PROMEKI_NAMESPACE_BEGIN

MediaIOReadCache::MediaIOReadCache(MediaIOSource *source) : _source(source) {}

MediaIOReadCache::~MediaIOReadCache() = default;

void MediaIOReadCache::setDepth(int n) {
        Mutex::Locker l(_mutex);
        _depth = n < 1 ? 1 : n;
}

int MediaIOReadCache::depth() const {
        Mutex::Locker l(_mutex);
        return _depth;
}

int MediaIOReadCache::count() const {
        Mutex::Locker l(_mutex);
        return static_cast<int>(_queue.size());
}

bool MediaIOReadCache::isEmpty() const {
        Mutex::Locker l(_mutex);
        return _queue.isEmpty();
}

bool MediaIOReadCache::isHeadReady() const {
        Mutex::Locker l(_mutex);
        if (_queue.isEmpty()) return false;
        return _queue[0]->isCompleted();
}

MediaIORequest MediaIOReadCache::readFrame() {
        MediaIOCommand::Ptr head;
        bool                shouldEmit = false;
        bool                vendedInFlight = false;
        {
                Mutex::Locker l(_mutex);
                if (_queue.isEmpty()) {
                        // Nothing prefetched — submit one now and use
                        // it as the head.  Callers are guaranteed a
                        // valid request even on a cold cache.
                        head = submitOneLocked();
                        if (head.isNull()) {
                                // Submit failed (no MediaIO / group);
                                // fall back to an Invalid resolved
                                // request so callers see the error.
                                return MediaIORequest::resolved(Error::Invalid);
                        }
                        _queue.remove(0);
                } else {
                        head = _queue[0];
                        _queue.remove(0);
                }
                // Vending the head clears the armed flag — callers
                // observed the ready transition implicitly by
                // receiving the request.  Re-evaluate against the
                // new head so back-to-back ready cmds re-arm and
                // re-fire frameReady for the next one.
                _headReadyArmed = false;
                vendedInFlight = !head->isCompleted();
                // Depth is the total in-flight budget — including
                // the cmd we just vended.  When the vended cmd is
                // still in flight, we top up to depth-1 so that
                // outstanding cmds total depth.  When the vended
                // cmd was already complete, the strand has spare
                // capacity and we can refill the queue to depth.
                // depth=1 with a hot strand therefore means "at
                // most one cmd in flight at a time" — the consumer
                // gets a chance to yield to its EventLoop between
                // frames instead of an unending hot prefetch.
                const int target = vendedInFlight ? (_depth > 0 ? _depth - 1 : 0) : _depth;
                while (static_cast<int>(_queue.size()) < target) {
                        MediaIOCommand::Ptr cmd = submitOneLocked();
                        if (cmd.isNull()) break;
                }
                shouldEmit = checkArmedLocked();
        }
        if (shouldEmit && _source != nullptr) {
                _source->frameReadySignal.emit();
        }
        return MediaIORequest(head);
}

size_t MediaIOReadCache::cancelAll() {
        List<MediaIOCommand::Ptr> dropped;
        {
                Mutex::Locker l(_mutex);
                _queue.swap(dropped);
                _headReadyArmed = false;
        }
        // Mark every dropped cmd cancelled OUTSIDE the lock — the
        // strand worker may be racing one of them and will read the
        // flag without holding our mutex.  Already-completed cmds
        // tolerate the redundant flag flip (Atomic store is idempotent
        // for our purposes).  Pending-count accounting is intentionally
        // NOT touched here: every dropped cmd is still owned by the
        // strand (either running, queued, or about to fire its
        // onCancel hook) and will route through
        // @ref MediaIO::completeCommand exactly once, which is where
        // the matching decrement lives.
        for (size_t i = 0; i < dropped.size(); ++i) {
                if (dropped[i].isValid()) dropped[i].modify()->cancelled.setValue(true);
        }
        return dropped.size();
}

void MediaIOReadCache::pushSyntheticResult(Error err) {
        bool shouldEmit = false;
        {
                Mutex::Locker l(_mutex);
                auto         *cmdEos = new MediaIOCommandRead();
                cmdEos->result = err;
                MediaIOCommand::Ptr p = MediaIOCommand::Ptr::takeOwnership(cmdEos);
                // Mark completed so the vended request resolves
                // immediately when popped.  Synthetic cmds never went
                // through the strand and never trigger
                // onCommandCompleted, so the armed-flag update has to
                // happen inline here.
                p.modify()->markCompleted();
                _queue.pushToBack(p);
                shouldEmit = checkArmedLocked();
        }
        if (shouldEmit && _source != nullptr) {
                _source->frameReadySignal.emit();
        }
}

void MediaIOReadCache::onCommandCompleted() {
        // Re-evaluate the armed flag against the current head and
        // emit the @c frameReady edge when the head transitioned to
        // ready.  Pending-count accounting lives in
        // @ref MediaIO::completeCommand so the decrement runs exactly
        // once per cmd regardless of which source's cache we were
        // routed to here.
        bool shouldEmit = false;
        {
                Mutex::Locker l(_mutex);
                shouldEmit = checkArmedLocked();
        }
        if (shouldEmit && _source != nullptr) {
                _source->frameReadySignal.emit();
        }
}

MediaIOCommand::Ptr MediaIOReadCache::submitOneLocked() {
        if (_source == nullptr) return MediaIOCommand::Ptr();
        MediaIO          *io = _source->mediaIO();
        MediaIOPortGroup *g = _source->group();
        if (io == nullptr || g == nullptr) return MediaIOCommand::Ptr();
        // Don't issue fresh prefetch against a closed (or in-the-process-
        // of-closing) MediaIO.  Vending a synthetic EOS leaves the cache
        // empty with the strand still alive briefly; without this gate
        // the next readFrame would re-fill the cache with reads that
        // can only ever resolve as NotOpen, wasting strand work and
        // hiding the real EOS termination from polling consumers.
        if (!io->isOpen() || io->isClosing()) return MediaIOCommand::Ptr();
        // Once the group has latched end-of-stream there is nothing
        // useful to read — keep the cache empty so readFrame's EOF
        // branch (in MediaIOSource) is the one consumers observe.
        if (g->atEnd()) return MediaIOCommand::Ptr();

        auto *cmdRead = new MediaIOCommandRead();
        cmdRead->step = g->_step;
        cmdRead->group = g;
        MediaIOCommand::Ptr cmd = MediaIOCommand::Ptr::takeOwnership(cmdRead);

        // Reserve the strand slot before posting so concurrent
        // pendingReads() observers see the increment as soon as
        // the cmd is enqueued.  The matching decrement happens in
        // onCommandCompleted (or cancelAll for queued-but-never-
        // dispatched cmds).
        g->_pendingReadCount.fetchAndAdd(1);
        _queue.pushToBack(cmd);
        // submit() returns after posting to the strand — no
        // synchronous dispatch happens here, so it is safe to call
        // while holding the cache mutex.  The strand worker will try
        // to acquire _mutex from onCommandCompleted, but that's
        // routed through MediaIO::completeCommand on a worker thread
        // and will block briefly until we release.  Topology
        // unrelated to our caller is fine.
        io->submit(cmd);
        return cmd;
}

bool MediaIOReadCache::checkArmedLocked() {
        if (_headReadyArmed) return false;
        if (_queue.isEmpty()) return false;
        if (!_queue[0]->isCompleted()) return false;
        _headReadyArmed = true;
        return true;
}

PROMEKI_NAMESPACE_END
