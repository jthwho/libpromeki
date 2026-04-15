/**
 * @file      mediaplay/pipeline.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include "pipeline.h"

#include <cstdio>

#include <promeki/application.h>
#include <promeki/frame.h>

using namespace promeki;

namespace mediaplay {

Pipeline::Pipeline(MediaIO *source,
                   List<MediaIO *> stages,
                   List<Sink> sinks,
                   int64_t frameCountLimit,
                   ObjectBase *parent)
        : ObjectBase(parent),
          _sinks(std::move(sinks)),
          _frameCountLimit(frameCountLimit),
          _mainLoop(EventLoop::current())
{
        _chain.pushToBack(source);
        for(MediaIO *s : stages) _chain.pushToBack(s);
        _chainDone.resize(_chain.size());
        for(size_t i = 0; i < _chainDone.size(); ++i) _chainDone[i] = false;

        // Source (_chain[0]): frameReady → drain source (index 0).
        _chain[0]->frameReadySignal.connect(
                [this]() { drainChain(0); }, this);

        // Intermediate stages (_chain[1..N]): frameReady drains THIS
        // stage; frameWanted re-opens the drain on the upstream
        // stage so we feed it more data; writeError aborts.
        for(size_t i = 1; i < _chain.size(); ++i) {
                MediaIO *io = _chain[i];
                io->frameReadySignal.connect(
                        [this, i]() { drainChain(i); }, this);
                io->frameWantedSignal.connect(
                        [this, i]() { reportFrameWanted(i - 1); }, this);
                io->writeErrorSignal.connect(
                        [this](Error err) { reportWriteError(err); }, this);
        }

        // Sinks: a completed sink write frees back-pressure on the
        // final chain element, so re-drain chain[N-1] when any sink
        // reports frameWanted.
        const size_t lastIdx = _chain.size() - 1;
        for(size_t i = 0; i < _sinks.size(); ++i) {
                MediaIO *sinkIO = _sinks[i].io;
                sinkIO->writeErrorSignal.connect(
                        [this](Error err) { reportWriteError(err); }, this);
                sinkIO->frameWantedSignal.connect(
                        [this, lastIdx]() { reportFrameWanted(lastIdx); }, this);
        }
}

void Pipeline::start() {
        for(size_t i = 0; i < _chain.size(); ++i) drainChain(i);
}

void Pipeline::reportWriteError(Error err) {
        _writeErrorPending.setValue(true);
        _writeError = err;
        if(_mainLoop != nullptr) {
                _mainLoop->postCallable([this]() {
                        onWriteErrorPosted();
                });
        }
}

void Pipeline::reportFrameWanted(size_t stageIdx) {
        if(_mainLoop != nullptr) {
                _mainLoop->postCallable([this, stageIdx]() {
                        onFrameWantedPosted(stageIdx);
                });
        }
}

void Pipeline::onFrameWantedPosted(size_t stageIdx) {
        if(stageIdx < _chain.size()) drainChain(stageIdx);
}

bool Pipeline::fanOutToSinks(const Frame::Ptr &frame) {
        for(size_t i = 0; i < _sinks.size(); ++i) {
                _sinks[i].io->writeFrame(frame, false);
        }
        _framesPumped++;
        return true;
}

bool Pipeline::sinksCanAccept() const {
        for(size_t i = 0; i < _sinks.size(); ++i) {
                if(_sinks[i].io->writesAccepted() <= 0) return false;
        }
        return true;
}

bool Pipeline::chainTailEmpty(size_t idx) const {
        if(idx >= _chain.size()) return true;
        MediaIO *io = _chain[idx];
        // writesAccepted() == writeDepth() means every write-pipeline
        // slot is free, which — for our midstream tasks — is the
        // signal that their internal output FIFO is empty too.
        return io->writesAccepted() >= io->writeDepth();
}

void Pipeline::drainChain(size_t idx) {
        if(_finished) return;
        if(idx >= _chain.size()) return;
        MediaIO *reader = _chain[idx];
        const bool isLast = (idx + 1 == _chain.size());
        MediaIO *writer = isLast ? nullptr : _chain[idx + 1];

        while(true) {
                if(_frameCountLimit > 0 &&
                   _framesPumped >=
                       static_cast<uint64_t>(_frameCountLimit)) {
                        finish(0);
                        return;
                }

                // Back-pressure gate: ask the downstream stage (or
                // every sink if we're at the tail) whether it can
                // accept another frame.  When the answer is no we
                // yield; the corresponding frameWantedSignal handler
                // will reopen this drain once a slot frees up.
                if(isLast) {
                        if(!sinksCanAccept()) return;
                } else {
                        if(writer->writesAccepted() <= 0) return;
                }

                Frame::Ptr frame;
                Error err = reader->readFrame(frame, false);
                if(err == Error::TryAgain) {
                        // This reader has nothing right now.  If the
                        // upstream is finished and the reader has
                        // itself drained, mark this stage done and
                        // see whether the whole chain is now empty.
                        const bool upstreamDone = (idx == 0)
                                ? false
                                : _chainDone[idx - 1];
                        if(upstreamDone && chainTailEmpty(idx)) {
                                _chainDone[idx] = true;
                                // Source / earliest stage is done;
                                // kick the next stage in case it was
                                // waiting on us.
                                if(!isLast) drainChain(idx + 1);
                                else if(_chainDone[_chain.size() - 1]) {
                                        finish(0);
                                }
                        }
                        return;
                }
                if(err == Error::EndOfFile) {
                        if(idx == 0) {
                                fprintf(stdout, "Source reached EOF.\n");
                        }
                        _chainDone[idx] = true;
                        // If there are downstream stages still holding
                        // frames, let them finish draining; otherwise
                        // finish now.
                        if(isLast) {
                                finish(0);
                        } else {
                                // Kick downstream so it sees the done
                                // flag on its next TryAgain.
                                drainChain(idx + 1);
                        }
                        return;
                }
                if(err.isError()) {
                        if(err != Error::Cancelled) {
                                fprintf(stderr,
                                        "Pipeline read error at stage %zu: %s\n",
                                        idx, err.name().cstr());
                        }
                        finish(1);
                        return;
                }

                if(isLast) {
                        fanOutToSinks(frame);
                } else {
                        writer->writeFrame(frame, false);
                        // The frame now lives in the next stage's
                        // write queue.  Kick that stage so any output
                        // it produces synchronously (1-in / 1-out
                        // codecs, converter, etc.) is picked up
                        // without waiting for a signal round-trip.
                        drainChain(idx + 1);
                }
        }
}

void Pipeline::finish(int rc) {
        if(_finished) return;
        _finished    = true;
        _cleanFinish = (rc == 0);
        Application::quit(rc);
}

void Pipeline::onWriteErrorPosted() {
        Error err = _writeError;
        // A backend may return TryAgain on a non-blocking write
        // (e.g. internal queue full).  Treat it as transient rather
        // than fatal — the next frameWanted will reopen the drain.
        if(err == Error::TryAgain) {
                _writeErrorPending.setValue(false);
                return;
        }
        if(err != Error::Cancelled) {
                fprintf(stderr, "Sink write error: %s\n", err.name().cstr());
        }
        finish(1);
}

} // namespace mediaplay
