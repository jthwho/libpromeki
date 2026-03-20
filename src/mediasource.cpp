/**
 * @file      mediasource.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/mediasource.h>

PROMEKI_NAMESPACE_BEGIN

MediaSource::~MediaSource() {
        disconnectAll();
}

void MediaSource::connect(MediaSink::Ptr sink) const {
        if(!sink.isValid()) return;
        _connectedSinks.pushToBack(sink);
        sink->addConnectedSource(const_cast<MediaSource *>(this));
        return;
}

void MediaSource::disconnect(MediaSink::Ptr sink) const {
        if(!sink.isValid()) return;
        for(size_t i = 0; i < _connectedSinks.size(); i++) {
                if(_connectedSinks[i]->name() == sink->name() &&
                   _connectedSinks[i]->node() == sink->node()) {
                        _connectedSinks[i]->removeConnectedSource(const_cast<MediaSource *>(this));
                        _connectedSinks.remove(i);
                        return;
                }
        }
        return;
}

void MediaSource::disconnectAll() const {
        for(auto &sink : _connectedSinks) {
                sink->removeConnectedSource(const_cast<MediaSource *>(this));
        }
        _connectedSinks.clear();
        return;
}

void MediaSource::deliver(Frame::Ptr frame) const {
        for(const auto &sink : _connectedSinks) {
                sink->push(frame);
        }
        return;
}

bool MediaSource::sinksReadyForFrame() const {
        if(_connectedSinks.isEmpty()) return false;
        for(const auto &sink : _connectedSinks) {
                if(!sink->canAcceptFrame()) return false;
        }
        return true;
}

PROMEKI_NAMESPACE_END
