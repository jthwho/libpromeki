/**
 * @file      sdl/sdldisplaynode.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <mutex>
#include <promeki/core/namespace.h>
#include <promeki/core/string.h>
#include <promeki/core/variant.h>
#include <promeki/core/map.h>
#include <promeki/core/framerate.h>
#include <promeki/core/duration.h>
#include <promeki/core/timestamp.h>
#include <promeki/proav/medianode.h>
#include <promeki/proav/image.h>

PROMEKI_NAMESPACE_BEGIN

class SDLVideoWidget;
class SDLAudioOutput;

/**
 * @brief Terminal sink node that delivers video and audio to SDL outputs.
 * @ingroup sdl_core
 *
 * SDLDisplayNode is a terminal MediaNode with one input and no outputs.
 * It paces output at the configured frame rate and delivers video
 * frames to an SDLVideoWidget and audio data to an SDLAudioOutput
 * that the application provides.
 *
 * The node does not create or own any UI objects — the application
 * is responsible for constructing the SDLWindow, SDLVideoWidget,
 * SDLAudioOutput, and widget hierarchy.  The node is given pointers
 * to the video widget and audio output via setVideoWidget() and
 * setAudioOutput() before the pipeline starts.
 *
 * @par Config options
 * - `FrameRate` (FrameRate): Target frame rate for pacing.
 *   If invalid, frames are delivered as fast as they arrive.
 *
 * @par Example
 * @code
 * // Application sets up the UI
 * SDLWindow window("Player", 1280, 720);
 * SDLVideoWidget video(&window);
 * video.setGeometry(window.geometry());
 * SDLAudioOutput audio;
 * window.show();
 *
 * // Configure the node with the application's widgets
 * SDLDisplayNode *display = new SDLDisplayNode();
 * display->setVideoWidget(&video);
 * display->setAudioOutput(&audio);
 *
 * MediaNodeConfig cfg("SDLDisplayNode", "Display");
 * cfg.set("FrameRate", FrameRate(FrameRate::FPS_2997));
 * display->build(cfg);
 * pipeline.addNode(display);
 * @endcode
 */
class SDLDisplayNode : public MediaNode {
        PROMEKI_OBJECT(SDLDisplayNode, MediaNode)
        public:
                /**
                 * @brief Constructs an SDLDisplayNode.
                 * @param parent Optional parent object.
                 */
                SDLDisplayNode(ObjectBase *parent = nullptr);

                /** @brief Destructor. */
                ~SDLDisplayNode() override;

                MediaNodeConfig defaultConfig() const override;
                BuildResult build(const MediaNodeConfig &config) override;

                /**
                 * @brief Returns display statistics.
                 * @return A map containing FramesDisplayed.
                 */
                Map<String, Variant> extendedStats() const override;

                /**
                 * @brief Sets the video widget to render frames to.
                 *
                 * Must be called before the pipeline starts.  The node
                 * does not take ownership — the application manages the
                 * widget's lifetime.
                 *
                 * @param widget The video widget to render to.
                 */
                void setVideoWidget(SDLVideoWidget *widget) { _videoWidget = widget; }

                /** @brief Returns the video widget, or nullptr. */
                SDLVideoWidget *videoWidget() const { return _videoWidget; }

                /**
                 * @brief Sets the audio output to push audio to.
                 *
                 * Must be called before the pipeline starts.  The node
                 * does not take ownership — the application manages the
                 * audio output's lifetime.
                 *
                 * @param output The audio output device.
                 */
                void setAudioOutput(SDLAudioOutput *output) { _audioOutput = output; }

                /** @brief Returns the audio output, or nullptr. */
                SDLAudioOutput *audioOutput() const { return _audioOutput; }

                /**
                 * @brief Renders the most recent pending image.
                 *
                 * Must be called from the main thread.  Delivers the
                 * pending image to the video widget and triggers a
                 * repaint of the widget's parent window.
                 *
                 * @return true if a frame was rendered.
                 */
                bool renderPending();

        protected:
                void processFrame(Frame::Ptr &frame, int inputIndex, DeliveryList &deliveries) override;
                void cleanup() override;

        private:
                // Application-provided outputs (not owned)
                SDLVideoWidget *_videoWidget = nullptr;
                SDLAudioOutput *_audioOutput = nullptr;

                // Frame pacing (worker thread)
                FrameRate       _frameRate;
                Duration        _frameInterval;
                TimeStamp       _nextFrameTime;
                bool            _pacing = false;
                bool            _firstFrame = true;

                // Pending image (shared between worker and main thread)
                Image::Ptr      _pendingImage;
                mutable Mutex   _pendingMutex;

                // SDL user event for main-thread wakeup
                static uint32_t userEventType();

                // Stats
                uint64_t        _framesDisplayed = 0;
                mutable Mutex   _statsMutex;

                void wakeMainThread();
};

PROMEKI_NAMESPACE_END
