/**
 * @file      medianode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/medianode.h>

PROMEKI_NAMESPACE_BEGIN

MediaNode::MediaNode(ObjectBase *parent) : ObjectBase(parent) {
}

MediaNode::~MediaNode() {
}

MediaPort::Ptr MediaNode::inputPort(int index) const {
        if(index < 0 || index >= (int)_inputPorts.size()) return MediaPort::Ptr();
        return _inputPorts[index];
}

MediaPort::Ptr MediaNode::outputPort(int index) const {
        if(index < 0 || index >= (int)_outputPorts.size()) return MediaPort::Ptr();
        return _outputPorts[index];
}

MediaPort::Ptr MediaNode::inputPort(const String &name) const {
        for(const auto &port : _inputPorts) {
                if(port->name() == name) return port;
        }
        return MediaPort::Ptr();
}

MediaPort::Ptr MediaNode::outputPort(const String &name) const {
        for(const auto &port : _outputPorts) {
                if(port->name() == name) return port;
        }
        return MediaPort::Ptr();
}

void MediaNode::enqueueInput(Frame::Ptr frame) {
        _inputQueue.push(std::move(frame));
        int depth = _inputQueue.size();
        std::lock_guard<std::mutex> lock(_statsMutex);
        if(depth > _peakQueueDepth) _peakQueueDepth = depth;
        return;
}

Error MediaNode::configure() {
        if(_state != Idle) return Error(Error::Invalid);
        setState(Configured);
        return Error(Error::Ok);
}

Error MediaNode::start() {
        if(_state != Configured) return Error(Error::Invalid);
        setState(Running);
        return Error(Error::Ok);
}

void MediaNode::stop() {
        setState(Idle);
        return;
}

void MediaNode::starvation() {
        return;
}

Map<String, Variant> MediaNode::properties() const {
        Map<String, Variant> ret;
        ret.insert("name", Variant(_name));
        return ret;
}

Error MediaNode::setProperty(const String &name, const Variant &value) {
        if(name == "name") {
                _name = value.get<String>();
                return Error(Error::Ok);
        }
        return Error(Error::Invalid);
}

Variant MediaNode::property(const String &name) const {
        Map<String, Variant> props = properties();
        if(props.contains(name)) return props[name];
        return Variant();
}

Frame::Ptr MediaNode::dequeueInput() {
        Frame::Ptr frame;
        _inputQueue.popOrFail(frame);
        return frame;
}

void MediaNode::deliverOutput(int portIndex, Frame::Ptr frame) {
        MediaPort::Ptr port = outputPort(portIndex);
        if(!port.isValid()) return;
        for(auto &link : _outgoingLinks) {
                if(link->sourceNode() == this &&
                   link->source()->name() == port->name()) {
                        link->deliver(frame);
                }
        }
        return;
}

void MediaNode::deliverOutput(Frame::Ptr frame) {
        for(auto &link : _outgoingLinks) {
                link->deliver(frame);
        }
        return;
}

void MediaNode::addInputPort(MediaPort::Ptr port) {
        port.modify()->setNode(this);
        port.modify()->setConnected(false);
        _inputPorts.pushToBack(std::move(port));
        return;
}

void MediaNode::addOutputPort(MediaPort::Ptr port) {
        port.modify()->setNode(this);
        port.modify()->setConnected(false);
        _outputPorts.pushToBack(std::move(port));
        return;
}

void MediaNode::setState(State state) {
        if(_state == state) return;
        _state = state;
        stateChangedSignal.emit(state);
        return;
}

NodeStats MediaNode::stats() const {
        std::lock_guard<std::mutex> lock(_statsMutex);
        NodeStats s;
        s.processCount = _processCount;
        s.starvationCount = _starvationCount;
        s.lastProcessDuration = _lastProcessDuration;
        s.avgProcessDuration = _avgProcessDuration;
        s.peakProcessDuration = _peakProcessDuration;
        s.currentQueueDepth = _inputQueue.size();
        s.peakQueueDepth = _peakQueueDepth;
        return s;
}

void MediaNode::resetStats() {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _processCount = 0;
        _starvationCount = 0;
        _lastProcessDuration = 0.0;
        _avgProcessDuration = 0.0;
        _peakProcessDuration = 0.0;
        _peakQueueDepth = 0;
        return;
}

Map<String, Variant> MediaNode::extendedStats() const {
        return Map<String, Variant>();
}

void MediaNode::recordProcessTiming(double duration) {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _processCount++;
        _lastProcessDuration = duration;
        if(duration > _peakProcessDuration) _peakProcessDuration = duration;
        // Exponential moving average with alpha = 0.1
        if(_processCount == 1) {
                _avgProcessDuration = duration;
        } else {
                _avgProcessDuration = 0.9 * _avgProcessDuration + 0.1 * duration;
        }
        return;
}

void MediaNode::recordStarvation() {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _starvationCount++;
        return;
}

void MediaNode::emitMessage(Severity severity, const String &message, uint64_t frameNumber) {
        NodeMessage msg;
        msg.severity = severity;
        msg.message = message;
        msg.frameNumber = frameNumber;
        msg.timestamp = TimeStamp::now();
        msg.node = this;
        messageEmittedSignal.emit(msg);
        return;
}

void MediaNode::emitWarning(const String &message) {
        emitMessage(Severity::Warning, message);
        return;
}

void MediaNode::emitError(const String &message) {
        emitMessage(Severity::Error, message);
        setState(ErrorState);
        errorOccurredSignal.emit(Error(Error::IOError));
        return;
}

Map<String, std::function<MediaNode *()>> &MediaNode::nodeRegistry() {
        static Map<String, std::function<MediaNode *()>> registry;
        return registry;
}

void MediaNode::registerNodeType(const String &typeName, std::function<MediaNode *()> factory) {
        nodeRegistry().insert(typeName, std::move(factory));
        return;
}

MediaNode *MediaNode::createNode(const String &typeName) {
        auto &registry = nodeRegistry();
        if(!registry.contains(typeName)) return nullptr;
        return registry[typeName]();
}

List<String> MediaNode::registeredNodeTypes() {
        List<String> ret;
        for(const auto &[key, value] : nodeRegistry()) {
                ret.pushToBack(key);
        }
        return ret;
}

PROMEKI_NAMESPACE_END
