/**
 * @file      sdldisplaynode.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/sdl/sdldisplaynode.h>
#include <promeki/sdl/sdlaudiooutput.h>
#include <promeki/sdl/sdlvideowidget.h>
#include <promeki/sdl/sdlwindow.h>
#include <promeki/sdl/sdlapplication.h>
#include <promeki/medianodeconfig.h>
#include <promeki/frame.h>
#include <promeki/logger.h>

#include <SDL3/SDL.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_REGISTER_NODE(SDLDisplayNode)

uint32_t SDLDisplayNode::userEventType() {
        static uint32_t type = SDL_RegisterEvents(1);
        return type;
}

SDLDisplayNode::SDLDisplayNode(ObjectBase *parent) : MediaNode(parent) {
        setName("SDLDisplayNode");
        addSink(MediaSink::Ptr::create("input", ContentVideo | ContentAudio));
        return;
}

SDLDisplayNode::~SDLDisplayNode() {
        return;
}

MediaNodeConfig SDLDisplayNode::defaultConfig() const {
        MediaNodeConfig cfg("SDLDisplayNode", "");
        cfg.set("FrameRate", FrameRate());
        return cfg;
}

BuildResult SDLDisplayNode::build(const MediaNodeConfig &config) {
        BuildResult result;

        if(state() != Idle) {
                result.addError("Node is not in Idle state");
                return result;
        }

        // Frame rate pacing
        _frameRate = config.get("FrameRate", FrameRate()).get<FrameRate>();
        if(_frameRate.isValid()) {
                int64_t intervalNs = static_cast<int64_t>(
                        static_cast<double>(_frameRate.denominator()) /
                        static_cast<double>(_frameRate.numerator()) * 1e9);
                _frameInterval = Duration::fromNanoseconds(intervalNs);
                _pacing = true;
        }

        _firstFrame = true;

        setState(Configured);
        return result;
}

Map<String, Variant> SDLDisplayNode::extendedStats() const {
        Map<String, Variant> ret;
        Mutex::Locker lock(_statsMutex);
        ret.insert("FramesDisplayed", Variant(_framesDisplayed));
        return ret;
}

void SDLDisplayNode::processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) {
        (void)inputIndex;
        (void)deliveries;

        if(!frame.isValid()) return;

        // Frame pacing: first frame sets the clock, subsequent frames sleep
        if(_pacing) {
                if(_firstFrame) {
                        _nextFrameTime = TimeStamp::now();
                        _firstFrame = false;
                } else {
                        _nextFrameTime += TimeStamp::secondsToDuration(
                                _frameInterval.toSecondsDouble());
                        _nextFrameTime.sleepUntil();
                }
        }

        // Push audio (thread-safe)
        if(_audioOutput != nullptr && !frame->audioList().isEmpty()) {
                for(const auto &audio : frame->audioList()) {
                        _audioOutput->pushAudio(*audio);
                }
        }

        // Stash the latest image for the main thread to render
        if(!frame->imageList().isEmpty()) {
                Mutex::Locker lock(_pendingMutex);
                _pendingImage = frame->imageList()[0];
        }

        // Wake the main thread to render
        wakeMainThread();

        // Consume the frame — this is a terminal sink
        frame = Frame::Ptr();
        return;
}

void SDLDisplayNode::wakeMainThread() {
        // Post renderPending() to the main thread's EventLoop
        SDLApplication *app = SDLApplication::instance();
        if(app != nullptr) {
                app->eventLoop().postCallable([this]() {
                        renderPending();
                });
        }

        // Also push an SDL user event to wake SDL_WaitEvent
        SDL_Event event = {};
        event.type = userEventType();
        SDL_PushEvent(&event);
        return;
}

bool SDLDisplayNode::renderPending() {
        if(_videoWidget == nullptr) return false;

        // Grab the pending image
        Image::Ptr img;
        {
                Mutex::Locker lock(_pendingMutex);
                if(!_pendingImage.isValid()) return false;
                img = _pendingImage;
                _pendingImage = Image::Ptr();
        }

        // Deliver to the video widget — it marks itself dirty
        _videoWidget->setImage(*img);

        // Find the parent window and repaint
        ObjectBase *p = _videoWidget->parent();
        while(p != nullptr) {
                SDLWindow *win = dynamic_cast<SDLWindow *>(p);
                if(win != nullptr) {
                        win->paintAll();
                        break;
                }
                p = p->parent();
        }

        {
                Mutex::Locker lock(_statsMutex);
                _framesDisplayed++;
        }

        return true;
}

void SDLDisplayNode::cleanup() {
        _firstFrame = true;
        return;
}

PROMEKI_NAMESPACE_END
