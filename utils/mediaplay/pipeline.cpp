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
        _inflight.resize(_sinks.size(), 0);

        // The source's frameReady drives the "source -> converter or
        // sinks" half of the drain.  If a converter is present, its
        // own frameReady drives the "converter -> sinks" half.  Both
        // halves stay non-blocking so the main thread's EventLoop is
        // always free to service timers (--duration) and signals.
        ObjectBase::connect(&_source->frameReadySignal, &onSourceFrameReadySlot);
        if(_converter != nullptr) {
                ObjectBase::connect(&_converter->frameReadySignal,
                                    &onConverterFrameReadySlot);
                // Converter write back-pressure: each completed write
                // posts frameWanted, which lets drainSource push
                // another frame.
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
                sinkIO->frameWantedSignal.connect(
                        [this, i]() { reportSinkFrameWanted(i); },
                        this);
        }
}

void Pipeline::start() {
        // Prime both halves: the source drain may fill the converter
        // with its first batch of work, and the converter drain will
        // no-op until there's something to pop.
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

void Pipeline::reportSinkFrameWanted(size_t sinkIndex) {
        if(_mainLoop != nullptr) {
                _mainLoop->postCallable([this, sinkIndex]() {
                        onSinkFrameWantedPosted(sinkIndex);
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

void Pipeline::onSinkFrameWantedPosted(size_t sinkIndex) {
        if(sinkIndex < _inflight.size() &&
           _inflight[sinkIndex] > 0) {
                _inflight[sinkIndex]--;
        }
        // Sink back-pressure has eased — restart whichever side was
        // feeding it.  If a converter is present, the converter
        // drain is the one pushing to sinks; otherwise it's
        // drainSource itself.
        if(_converter != nullptr) drainConverter();
        else                      drainSource();
}

void Pipeline::onConverterFrameWantedPosted() {
        if(_converterInflight > 0) _converterInflight--;
        // Converter accepted a frame — try to push another one in.
        drainSource();
}

bool Pipeline::fanOutToSinks(const Frame::Ptr &frame) {
        for(size_t i = 0; i < _sinks.size(); ++i) {
                _inflight[i]++;
                _sinks[i].io->writeFrame(frame, false);
        }
        _framesPumped++;
        return true;
}

void Pipeline::drainSource() {
        if(_finished) return;
        while(true) {
                if(_frameCountLimit > 0 &&
                   _framesPumped >=
                       static_cast<uint64_t>(_frameCountLimit)) {
                        finish(0);
                        return;
                }

                // Back-pressure gate: honour whichever downstream
                // stage is next in the chain.  With a converter the
                // gate is its own in-flight counter; without one the
                // sinks' counters matter here.
                if(_converter != nullptr) {
                        if(_converterInflight >= MaxInflightConverter) return;
                } else {
                        for(size_t i = 0; i < _sinks.size(); ++i) {
                                if(_inflight[i] >= MaxInflightPerSink) return;
                        }
                }

                Frame::Ptr frame;
                Error err = _source->readFrame(frame, false);
                if(err == Error::TryAgain) return;
                if(err == Error::EndOfFile) {
                        fprintf(stdout, "Source reached EOF.\n");
                        finish(0);
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
                        // Non-blocking write to the converter: the
                        // strand picks up the frame, runs the
                        // transform, and eventually emits its own
                        // frameReady which drives drainConverter.
                        // Track it as an in-flight unit of work so
                        // the source honours back-pressure.
                        _converterInflight++;
                        _converter->writeFrame(frame, false);
                        // Do NOT increment _framesPumped here; that
                        // counter tracks frames that reached the
                        // sinks, and the converter stage is one step
                        // upstream of that.  drainConverter bumps
                        // _framesPumped when fanning out.
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

                // Sink back-pressure gate — the converter is the
                // producer for the real sinks, so its drain respects
                // them.
                for(size_t i = 0; i < _sinks.size(); ++i) {
                        if(_inflight[i] >= MaxInflightPerSink) return;
                }

                Frame::Ptr converted;
                Error err = _converter->readFrame(converted, false);
                // Converter::executeCmd(Read) returns TryAgain when
                // its output FIFO is empty — totally normal, just
                // means there's nothing to fan out right now.
                if(err == Error::TryAgain) return;
                if(err == Error::EndOfFile) {
                        // Converter doesn't emit EOF today, but
                        // treat it the same way a source would if it
                        // ever does.
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
        // Converter back-pressure shows up as Error::TryAgain on the
        // writeError signal because the backend's executeCmd(Write)
        // returns it — treat it as a deferred write, not a fatal
        // pipeline error.  The next frameWanted callback will reopen
        // the drain.
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
