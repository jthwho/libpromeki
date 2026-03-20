/**
 * @file      medianode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/medianode.h>
#include <promeki/proav/medianodeconfig.h>

PROMEKI_NAMESPACE_BEGIN

// ---- BuildResult ----

bool BuildResult::isOk() const {
        for(const auto &e : entries) {
                if(e.severity == Severity::Error || e.severity == Severity::Fatal) return false;
        }
        return true;
}

bool BuildResult::isError() const {
        return !isOk();
}

void BuildResult::addInfo(const String &msg) {
        entries.pushToBack({Severity::Info, msg});
        return;
}

void BuildResult::addWarning(const String &msg) {
        entries.pushToBack({Severity::Warning, msg});
        return;
}

void BuildResult::addError(const String &msg) {
        entries.pushToBack({Severity::Error, msg});
        return;
}

// ---- MediaNodeThread ----

// Internal Thread subclass that runs a MediaNode's processing loop.
class MediaNodeThread : public Thread {
        public:
                MediaNodeThread(MediaNode *node) : _node(node) { }
        protected:
                void run() override { _node->threadEntry(); return; }
        private:
                MediaNode *_node;
};

// ---- MediaNode ----

MediaNode::MediaNode(ObjectBase *parent) : ObjectBase(parent) {
}

MediaNode::~MediaNode() {
        delete _thread;
}

MediaSink::Ptr MediaNode::sink(int index) const {
        if(index < 0 || index >= (int)_sinks.size()) return MediaSink::Ptr();
        return _sinks[index];
}

MediaSource::Ptr MediaNode::source(int index) const {
        if(index < 0 || index >= (int)_sources.size()) return MediaSource::Ptr();
        return _sources[index];
}

MediaSink::Ptr MediaNode::sink(const String &name) const {
        for(const auto &s : _sinks) {
                if(s->name() == name) return s;
        }
        return MediaSink::Ptr();
}

MediaSource::Ptr MediaNode::source(const String &name) const {
        for(const auto &s : _sources) {
                if(s->name() == name) return s;
        }
        return MediaSource::Ptr();
}

void MediaNode::setThread(Thread *thread) {
        delete _thread;
        _thread = thread;
        return;
}

void MediaNode::wake() {
        _workCv.wakeAll();
        return;
}

bool MediaNode::hasInput() const {
        for(const auto &s : _sinks) {
                if(s->queueSize() > 0) return true;
        }
        return false;
}

bool MediaNode::canOutput() const {
        for(int i = 0; i < (int)_sources.size(); i++) {
                if(!canOutput(i)) return false;
        }
        return true;
}

bool MediaNode::canOutput(int sourceIndex) const {
        MediaSource::Ptr src = source(sourceIndex);
        if(!src.isValid()) return false;
        return src->sinksReadyForFrame();
}

bool MediaNode::hasWork() const {
        if(_sinks.isEmpty()) {
                // Source node: can produce if outputs can accept
                return canOutput();
        }
        return hasInput() && canOutput();
}

Error MediaNode::waitForWork(unsigned int timeoutMs) {
        if(_state != Running) return Error(Error::Stopped);
        Mutex::Locker lock(_workMutex);
        Error err = _workCv.wait(_workMutex,
                [this] { return hasWork() || _state != Running; }, timeoutMs);
        if(err != Error::Ok) return err;
        if(_state != Running) return Error(Error::Stopped);
        return Error(Error::Ok);
}

Error MediaNode::start() {
        if(_state != Configured) return Error(Error::Invalid);
        setState(Running);
        if(_thread == nullptr) {
                _thread = new MediaNodeThread(this);
        }
        _thread->start();
        return Error(Error::Ok);
}

void MediaNode::stop() {
        setState(Idle);
        wake();
        if(_thread != nullptr) {
                _thread->wait();
                delete _thread;
                _thread = nullptr;
        }
        cleanup();
        return;
}

void MediaNode::cleanup() {
        return;
}

Frame::Ptr MediaNode::dequeueInput(int sinkIndex) {
        MediaSink::Ptr s = sink(sinkIndex);
        if(!s.isValid()) return Frame::Ptr();
        Frame::Ptr frame;
        if(!s->popOrFail(frame)) return Frame::Ptr();
        return frame;
}

Frame::Ptr MediaNode::dequeueInput(const String &sinkName) {
        for(int i = 0; i < (int)_sinks.size(); i++) {
                if(_sinks[i]->name() == sinkName) {
                        return dequeueInput(i);
                }
        }
        return Frame::Ptr();
}

Frame::Ptr MediaNode::dequeueInput() {
        for(int i = 0; i < (int)_sinks.size(); i++) {
                if(_sinks[i]->queueSize() > 0) {
                        return dequeueInput(i);
                }
        }
        return Frame::Ptr();
}

void MediaNode::threadEntry() {
        while(_state == Running) {
                Error err = waitForWork();
                if(err != Error::Ok) break;
                process();
        }
        return;
}

int MediaNode::aggregateQueueDepth() const {
        int total = 0;
        for(const auto &s : _sinks) {
                total += (int)s->queueSize();
        }
        return total;
}

void MediaNode::deliverOutput(int sourceIndex, Frame::Ptr frame) {
        MediaSource::Ptr src = source(sourceIndex);
        if(!src.isValid()) return;
        src->deliver(std::move(frame));
        return;
}

void MediaNode::deliverOutput(Frame::Ptr frame) {
        for(const auto &src : _sources) {
                src->deliver(frame);
        }
        return;
}

void MediaNode::addSink(MediaSink::Ptr sink) {
        sink->setNode(this);
        _sinks.pushToBack(std::move(sink));
        return;
}

void MediaNode::addSource(MediaSource::Ptr source) {
        source->setNode(this);
        _sources.pushToBack(std::move(source));
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
        s.lastProcessDuration = _lastProcessDuration;
        s.avgProcessDuration = _avgProcessDuration;
        s.peakProcessDuration = _peakProcessDuration;
        s.currentQueueDepth = const_cast<MediaNode *>(this)->aggregateQueueDepth();
        s.peakQueueDepth = _peakQueueDepth;
        return s;
}

void MediaNode::resetStats() {
        std::lock_guard<std::mutex> lock(_statsMutex);
        _processCount = 0;
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
        if(_processCount == 1) {
                _avgProcessDuration = duration;
        } else {
                _avgProcessDuration = 0.9 * _avgProcessDuration + 0.1 * duration;
        }
        int depth = aggregateQueueDepth();
        if(depth > _peakQueueDepth) _peakQueueDepth = depth;
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
