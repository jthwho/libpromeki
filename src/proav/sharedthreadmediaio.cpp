/**
 * @file      sharedthreadmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sharedthreadmediaio.h>

#include <typeinfo>

#include <promeki/mediaiostats.h>
#include <promeki/system.h>
#include <promeki/threadpool.h>
#include <promeki/timestamp.h>

PROMEKI_NAMESPACE_BEGIN

ThreadPool &SharedThreadMediaIO::pool() {
        // Local static so the destructor runs at process exit and joins
        // the worker threads; otherwise each std::thread's shared state
        // is "definitely lost" under valgrind because the threads stay
        // alive past main().
        struct PoolHolder {
                        ThreadPool tp;
                        PoolHolder() {
                                tp.setNamePrefix("media");
                                tp.setName("media");
                        }
        };
        static PoolHolder h;
        return h.tp;
}

SharedThreadMediaIO::SharedThreadMediaIO(ObjectBase *parent) : CommandMediaIO(parent) {}

SharedThreadMediaIO::~SharedThreadMediaIO() {
        // Drain the strand before any subclass state tears down.  The
        // Strand destructor would do this on its own, but we want it
        // to happen before our derived class's members are destroyed
        // — otherwise an in-flight executeCmd could touch already-
        // destroyed backend state.
        _strand.waitForIdle();
}

bool SharedThreadMediaIO::isIdle() const {
        return !_strand.isBusy();
}

void SharedThreadMediaIO::cancelPendingWork() {
        _strand.cancelPending();
}

void SharedThreadMediaIO::refreshStrandWorkTag() {
        // typeid(*this) resolves to the most-derived type once
        // construction completes, which is what we want for
        // attribution.  demangleSymbol caches results so subsequent
        // calls are an unordered_map lookup.
        String cls = System::demangleSymbol(typeid(*this).name());
        // Strip the promeki:: namespace prefix so reports stay
        // terse — "RtpMediaIO[name]" rather than
        // "promeki::RtpMediaIO[name]".  The prefix is universal
        // across the library so dropping it never aliases.
        static const String kNamespacePrefix("promeki::");
        if (cls.startsWith(kNamespacePrefix)) {
                cls = cls.mid(kNamespacePrefix.length());
        }
        const String label = name();
        const String tagName = label.isEmpty() ? cls : (cls + "[" + label + "]");
        if (tagName != _lastWorkTagName) {
                _lastWorkTagName = tagName;
                _strand.setWorkTag(ThreadPool::WorkTag(tagName));
        }
}

void SharedThreadMediaIO::submit(MediaIOCommand::Ptr cmd) {
        // Refresh the strand's work tag so per-instance pool-time
        // accounting tracks rename via MediaConfig::Name without
        // callers having to plumb anything explicitly.  No-op when
        // the tag hasn't changed (string compare against the cached
        // value).
        refreshStrandWorkTag();

        // Cancellation contract (devplan §Cancellation):
        //   • not yet dispatched → short-circuit with Error::Cancelled
        //     (handled inside the runner, before dispatch runs).
        //   • dispatched and executing → late cancel ignored for this
        //     shared-pool strategy; cmd runs to completion.
        //
        // Per-command telemetry: capture queue-wait + execute intervals
        // around the dispatch hook so every request reports accurate
        // timing.  TimeStamp uses steady_clock so deltas stay
        // monotonic regardless of system-clock adjustments.
        const TimeStamp submitTime = TimeStamp::now();
        auto            runner = [this, cmd, submitTime]() mutable {
                const TimeStamp dispatchTime = TimeStamp::now();
                MediaIOCommand *raw = cmd.modify();
                raw->stats.set(MediaIOStats::QueueWaitDuration, dispatchTime - submitTime);
                if (cmd->cancelled.value()) {
                        raw->result = Error::Cancelled;
                } else {
                        raw->result = dispatch(cmd);
                }
                const TimeStamp endTime = TimeStamp::now();
                raw->stats.set(MediaIOStats::ExecuteDuration, endTime - dispatchTime);
                completeCommand(cmd);
        };
        auto onCancel = [this, cmd, submitTime]() mutable {
                // Strand-level cancel (e.g. cancelPending across the
                // strand).  Resolve the request with Error::Cancelled
                // so wait() / then() unblock cleanly.
                const TimeStamp cancelTime = TimeStamp::now();
                MediaIOCommand *raw = cmd.modify();
                raw->stats.set(MediaIOStats::QueueWaitDuration, cancelTime - submitTime);
                raw->result = Error::Cancelled;
                completeCommand(cmd);
        };
        if (cmd->urgent) {
                _strand.submitUrgent(std::move(runner), std::move(onCancel));
        } else {
                _strand.submit(std::move(runner), std::move(onCancel));
        }
}

PROMEKI_NAMESPACE_END
