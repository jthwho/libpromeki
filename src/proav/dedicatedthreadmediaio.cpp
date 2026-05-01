/**
 * @file      dedicatedthreadmediaio.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/dedicatedthreadmediaio.h>

#include <promeki/mediaiostats.h>

PROMEKI_NAMESPACE_BEGIN

DedicatedThreadMediaIO::DedicatedThreadMediaIO(ObjectBase *parent)
        : CommandMediaIO(parent), _worker(this) {
        _worker.start();
}

DedicatedThreadMediaIO::~DedicatedThreadMediaIO() {
        // Signal shutdown and drain.  Any cmds remaining in the queue
        // resolve as Error::Cancelled so callers' wait()/then() unblock.
        {
                Mutex::Locker l(_mutex);
                _shutdown.setValue(true);
                _cond.wakeAll();
        }
        (void)_worker.wait();
}

bool DedicatedThreadMediaIO::isIdle() const {
        Mutex::Locker l(_mutex);
        return _queue.isEmpty() && _urgentQueue.isEmpty() && !_busy.value();
}

void DedicatedThreadMediaIO::submit(MediaIOCommand::Ptr cmd) {
        QueueEntry entry;
        entry.cmd = cmd;
        entry.submitTime = TimeStamp::now();
        Mutex::Locker l(_mutex);
        if (cmd->urgent) {
                _urgentQueue.pushToBack(std::move(entry));
        } else {
                _queue.pushToBack(std::move(entry));
        }
        _cond.wakeOne();
}

void DedicatedThreadMediaIO::workerMain() {
        for (;;) {
                QueueEntry entry;
                {
                        Mutex::Locker l(_mutex);
                        _cond.wait(_mutex, [this] {
                                return _shutdown.value() || !_urgentQueue.isEmpty() || !_queue.isEmpty();
                        });
                        if (_shutdown.value() && _urgentQueue.isEmpty() && _queue.isEmpty()) {
                                return;
                        }
                        // Urgent queue takes precedence so stats / cancel
                        // polls don't sit behind a deep backlog of
                        // read/write work.
                        if (!_urgentQueue.isEmpty()) {
                                entry = std::move(_urgentQueue[0]);
                                _urgentQueue.remove(0);
                        } else {
                                entry = std::move(_queue[0]);
                                _queue.remove(0);
                        }
                        _busy.setValue(true);
                }
                MediaIOCommand::Ptr cmd = entry.cmd;
                const TimeStamp     dispatchTime = TimeStamp::now();
                MediaIOCommand     *raw = cmd.modify();
                raw->stats.set(MediaIOStats::QueueWaitDuration, dispatchTime - entry.submitTime);
                if (cmd->cancelled.value() || _shutdown.value()) {
                        raw->result = Error::Cancelled;
                } else {
                        raw->result = dispatch(cmd);
                }
                const TimeStamp endTime = TimeStamp::now();
                raw->stats.set(MediaIOStats::ExecuteDuration, endTime - dispatchTime);
                completeCommand(cmd);
                {
                        Mutex::Locker l(_mutex);
                        _busy.setValue(false);
                        // Wake anyone polling isIdle() that may have
                        // been blocked on a transient busy state.
                        _cond.wakeAll();
                }
        }
}

PROMEKI_NAMESPACE_END
