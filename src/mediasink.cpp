/**
 * @file      mediasink.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/mediasink.h>
#include <promeki/proav/mediasource.h>
#include <promeki/proav/medianode.h>
#include <promeki/core/benchmark.h>

PROMEKI_NAMESPACE_BEGIN

void MediaSink::push(Frame::Ptr frame) const {
        if(frame.isValid() && frame->benchmark().isValid() && _node != nullptr) {
                Benchmark::Id queuedId(_node->name() + ".Queued");
                frame.modify()->benchmark().modify()->stamp(queuedId);
        }
        _queue.push(std::move(frame));
        if(_node != nullptr) _node->wake();
        return;
}

bool MediaSink::popOrFail(Frame::Ptr &frame) const {
        bool ok = _queue.popOrFail(frame);
        if(ok) notifySources();
        return ok;
}

bool MediaSink::canAcceptFrame() const {
        return _queue.size() < (size_t)_maxQueueDepth;
}

void MediaSink::addConnectedSource(MediaSource *source) const {
        if(!_connectedSources.contains(source)) {
                _connectedSources.pushToBack(source);
        }
        return;
}

void MediaSink::removeConnectedSource(MediaSource *source) const {
        _connectedSources.removeFirst(source);
        return;
}

void MediaSink::notifySources() const {
        for(auto *source : _connectedSources) {
                MediaNode *n = source->node();
                if(n != nullptr) n->wake();
        }
        return;
}

PROMEKI_NAMESPACE_END
