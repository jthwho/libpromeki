/**
 * @file      mediapipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/proav/mediapipeline.h>

PROMEKI_NAMESPACE_BEGIN

MediaPipeline::MediaPipeline(ObjectBase *parent) : ObjectBase(parent) {
}

MediaPipeline::~MediaPipeline() {
        if(_state == Running || _state == Paused) {
                stop();
        }
        delete _ownedPool;
}

Error MediaPipeline::start() {
        if(_state != Stopped) return Error(Error::Invalid);
        setState(Starting);

        // Validate the graph
        Error err = _graph.validate();
        if(err != Error::Ok) {
                setState(ErrorState);
                errorOccurredSignal.emit(err);
                return err;
        }

        // Get processing order
        List<MediaNode *> order = _graph.topologicalSort();
        if(order.isEmpty()) {
                setState(ErrorState);
                Error cycleErr(Error::Invalid);
                errorOccurredSignal.emit(cycleErr);
                return cycleErr;
        }

        // Configure all nodes in topological order
        for(auto *node : order) {
                err = node->configure();
                if(err != Error::Ok) {
                        setState(ErrorState);
                        errorOccurredSignal.emit(err);
                        return err;
                }
        }

        // Start all nodes
        for(auto *node : order) {
                err = node->start();
                if(err != Error::Ok) {
                        // Stop any already-started nodes
                        for(auto *n : order) {
                                if(n == node) break;
                                n->stop();
                        }
                        setState(ErrorState);
                        errorOccurredSignal.emit(err);
                        return err;
                }
        }

        // Ensure we have a thread pool
        if(_threadPool == nullptr) {
                _ownedPool = new ThreadPool(4);
                _threadPool = _ownedPool;
        }

        _running = true;
        _paused = false;
        setState(Running);
        startedSignal.emit();
        return Error(Error::Ok);
}

Error MediaPipeline::stop() {
        if(_state != Running && _state != Paused) return Error(Error::Invalid);
        setState(Stopping);

        _running = false;
        _paused = false;

        // Wait for any in-flight processing to finish
        if(_threadPool != nullptr) {
                _threadPool->waitForDone();
        }

        // Stop all nodes in reverse topological order
        List<MediaNode *> order = _graph.topologicalSort();
        for(int i = (int)order.size() - 1; i >= 0; i--) {
                order[i]->stop();
        }

        // Clean up owned pool
        if(_ownedPool != nullptr) {
                if(_threadPool == _ownedPool) _threadPool = nullptr;
                delete _ownedPool;
                _ownedPool = nullptr;
        }
        _threadPool = nullptr;

        setState(Stopped);
        stoppedSignal.emit();
        return Error(Error::Ok);
}

Error MediaPipeline::pause() {
        if(_state != Running) return Error(Error::Invalid);
        _paused = true;
        setState(Paused);
        return Error(Error::Ok);
}

Error MediaPipeline::resume() {
        if(_state != Paused) return Error(Error::Invalid);
        _paused = false;
        setState(Running);
        return Error(Error::Ok);
}

void MediaPipeline::setState(State state) {
        if(_state == state) return;
        _state = state;
        stateChangedSignal.emit(state);
        return;
}

PROMEKI_NAMESPACE_END
