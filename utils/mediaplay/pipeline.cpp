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
                   MediaIO *converter,
                   List<Sink> sinks,
                   int64_t frameCountLimit,
                   ObjectBase *parent)
        : ObjectBase(parent),
          _source(source),
          _converter(converter),
          _sinks(std::move(sinks)),
          _frameCountLimit(frameCountLimit),
          _mainLoop(EventLoop::current())
{
        ObjectBase::connect(&_source->frameReadySignal, &onSourceFrameReadySlot);
        if(_converter != nullptr) {
                ObjectBase::connect(&_converter->frameReadySignal,
                                    &onConverterFrameReadySlot);
                // A completed converter write means its output queue
                // now has a frame — kick drainConverter to pop it.
                _converter->frameWantedSignal.connect(
                        [this]() { reportConverterFrameWanted(); },
                        this);
                _converter->writeErrorSignal.connect(
                        [this](Error err) { reportWriteError(err); },
                        this);
        }

        for(size_t i = 0; i < _sinks.size(); ++i) {
                MediaIO *sinkIO = _sinks[i].io;
                sinkIO->writeErrorSignal.connect(
                        [this](Error err) { reportWriteError(err); },
                        this);
                // A completed sink write frees a slot — restart the
                // side that feeds it.
                sinkIO->frameWantedSignal.connect(
                        [this]() { reportSinkFrameWanted(); },
                        this);
        }
}

void Pipeline::start() {
        drainSource();
        if(_converter != nullptr) drainConverter();
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

void Pipeline::reportSinkFrameWanted() {
        if(_mainLoop != nullptr) {
                _mainLoop->postCallable([this]() {
                        onSinkFrameWantedPosted();
                });
        }
}

void Pipeline::reportConverterFrameWanted() {
        if(_mainLoop != nullptr) {
                _mainLoop->postCallable([this]() {
                        onConverterFrameWantedPosted();
                });
        }
}

void Pipeline::onSinkFrameWantedPosted() {
        // A sink finished a write — back-pressure eased.  Restart
        // whichever side feeds the sinks.
        if(_converter != nullptr) drainConverter();
        else                      drainSource();
}

void Pipeline::onConverterFrameWantedPosted() {
        // A converter write completed — its output queue now has a
        // frame.  Kick drainConverter to read it.
        drainConverter();
}

bool Pipeline::fanOutToSinks(const Frame::Ptr &frame) {
        for(size_t i = 0; i < _sinks.size(); ++i) {
                _sinks[i].io->writeFrame(frame, false);
        }
        _framesPumped++;
        return true;
}

// Returns true if every sink can accept at least one more frame.
bool Pipeline::sinksCanAccept() const {
        for(size_t i = 0; i < _sinks.size(); ++i) {
                if(_sinks[i].io->writesAccepted() <= 0) return false;
        }
        return true;
}

void Pipeline::drainSource() {
        if(_finished || _sourceAtEnd) return;
        while(true) {
                if(_frameCountLimit > 0 &&
                   _framesPumped >=
                       static_cast<uint64_t>(_frameCountLimit)) {
                        finish(0);
                        return;
                }

                // Back-pressure gate: ask the next downstream stage
                // whether it can accept another frame.
                if(_converter != nullptr) {
                        if(_converter->writesAccepted() <= 0) return;
                } else {
                        if(!sinksCanAccept()) return;
                }

                Frame::Ptr frame;
                Error err = _source->readFrame(frame, false);
                if(err == Error::TryAgain) return;
                if(err == Error::EndOfFile) {
                        fprintf(stdout, "Source reached EOF.\n");
                        if(_converter != nullptr &&
                           _converter->writesAccepted() < _converter->writeDepth()) {
                                // Frames are still in the converter
                                // pipeline — let drainConverter flush
                                // them to the sinks before finishing.
                                _sourceAtEnd = true;
                        } else {
                                finish(0);
                        }
                        return;
                }
                if(err.isError()) {
                        if(err != Error::Cancelled) {
                                fprintf(stderr,
                                        "Source read error: %s\n",
                                        err.name().cstr());
                        }
                        finish(1);
                        return;
                }

                if(_converter != nullptr) {
                        _converter->writeFrame(frame, false);
                } else {
                        fanOutToSinks(frame);
                }
        }
}

void Pipeline::drainConverter() {
        if(_finished || _converter == nullptr) return;
        while(true) {
                if(_frameCountLimit > 0 &&
                   _framesPumped >=
                       static_cast<uint64_t>(_frameCountLimit)) {
                        finish(0);
                        return;
                }

                // Sink back-pressure gate.
                if(!sinksCanAccept()) return;

                Frame::Ptr converted;
                Error err = _converter->readFrame(converted, false);
                if(err == Error::TryAgain) {
                        // Output FIFO is empty.  If the source has
                        // finished and nothing remains in the
                        // converter pipeline, the run is complete.
                        if(_sourceAtEnd &&
                           _converter->writesAccepted() >= _converter->writeDepth()) {
                                finish(0);
                        }
                        return;
                }
                if(err == Error::EndOfFile) {
                        finish(0);
                        return;
                }
                if(err.isError()) {
                        if(err != Error::Cancelled) {
                                fprintf(stderr,
                                        "Converter read error: %s\n",
                                        err.name().cstr());
                        }
                        finish(1);
                        return;
                }

                fanOutToSinks(converted);

                // A frame left the converter — its pending-write
                // slot has already been released by the strand, so
                // writesAccepted() has increased.  Let drainSource
                // push more work in.
                drainSource();
        }
}

void Pipeline::finish(int rc) {
        if(_finished) return;
        _finished    = true;
        _cleanFinish = (rc == 0);
        Application::quit(rc);
}

void Pipeline::onSourceFrameReady() { drainSource(); }

void Pipeline::onConverterFrameReady() { drainConverter(); }

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
