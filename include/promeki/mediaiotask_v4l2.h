/**
 * @file      mediaiotask_v4l2.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_V4L2

#include <atomic>
#include <thread>
#include <promeki/mediaiotask.h>
#include <promeki/imagedesc.h>
#include <promeki/audiodesc.h>
#include <promeki/audiobuffer.h>
#include <promeki/pixeldesc.h>
#include <promeki/framerate.h>
#include <promeki/image.h>
#include <promeki/queue.h>
#include <promeki/mutex.h>
#include <promeki/list.h>
#include <promeki/periodiccallback.h>

struct _snd_pcm;
typedef struct _snd_pcm snd_pcm_t;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIOTask backend for V4L2 video capture with optional ALSA audio.
 * @ingroup proav
 *
 * Output-only MediaIO source that captures video frames from a Linux
 * V4L2 device and optionally captures synchronized audio from an ALSA
 * PCM device.  Frames are captured via V4L2 MMAP buffers for zero-copy
 * kernel-to-userspace transfer, then copied into promeki Image objects.
 *
 * @par Threading model
 *
 * The backend launches its own capture threads on open:
 *
 * - A **video thread** ("v4l2-video") continuously dequeues V4L2
 *   buffers, copies them into Image::Ptr objects, and pushes them
 *   onto an internal Queue.  If the queue exceeds 2 entries, the
 *   oldest is dropped and counted via noteFrameDropped().
 * - An **audio thread** ("v4l2-audio", when enabled) continuously
 *   reads ALSA PCM samples into a mutex-protected AudioBuffer ring.
 *
 * executeCmd(Read) is therefore non-blocking: it pops the latest
 * captured image from the queue and drains the matching audio from
 * the ring buffer, keeping the MediaIO strand free.
 *
 * @par Device hot-unplug
 *
 * If the V4L2 device is removed (USB unplug), the capture thread
 * detects @c ENODEV and stores an atomic error.  The next
 * executeCmd(Read) returns @c Error::DeviceError, propagating the
 * failure to the pipeline.
 *
 * This is an infinite source: frameCount returns FrameCountInfinite
 * and canSeek returns false.
 *
 * @par Device enumeration
 *
 * The static @c enumerate() callback scans @c /dev/video* for devices
 * that report @c V4L2_CAP_VIDEO_CAPTURE, returning their device paths.
 *
 * @par Format negotiation
 *
 * On open, the backend queries the device for supported pixel formats
 * and attempts to set the requested @c VideoSize, @c VideoPixelFormat,
 * and @c FrameRate.  If the exact format is not available, V4L2 will
 * negotiate the closest match and the resulting MediaDesc reflects what
 * the driver actually selected.
 *
 * @par V4L2 pixel format mapping
 *
 * Common V4L2 pixel formats are mapped to PixelDesc IDs:
 * | V4L2                 | PixelDesc                          |
 * |----------------------|------------------------------------|
 * | V4L2_PIX_FMT_YUYV   | YUV8_422_Rec709                    |
 * | V4L2_PIX_FMT_UYVY   | YUV8_422_UYVY_Rec709               |
 * | V4L2_PIX_FMT_NV12   | YUV8_420_SemiPlanar_Rec709         |
 * | V4L2_PIX_FMT_RGB24  | RGB8_sRGB                          |
 * | V4L2_PIX_FMT_BGR24  | BGR8_sRGB                          |
 * | V4L2_PIX_FMT_MJPEG  | JPEG_YUV8_422_Rec709               |
 *
 * @par Config keys
 * | Key | Type | Default | Description |
 * |-----|------|---------|-------------|
 * | @ref MediaConfig::V4l2DevicePath   | String   | ""      | Device node (e.g. "/dev/video0"). Required. |
 * | @ref MediaConfig::V4l2BufferCount  | int      | 4       | MMAP buffer count (2-32). |
 * | @ref MediaConfig::V4l2AudioDevice  | String   | "auto"  | ALSA device for paired audio. "auto" = auto-detect, "none"/empty = disabled. |
 * | @ref MediaConfig::VideoSize        | Size2Du32| 1920x1080 | Requested capture resolution. |
 * | @ref MediaConfig::VideoPixelFormat | PixelDesc| YUV8_422_Rec709 | Requested pixel format. |
 * | @ref MediaConfig::FrameRate        | FrameRate| 30/1    | Requested capture frame rate. |
 * | @ref MediaConfig::AudioRate        | float    | 48000   | ALSA capture sample rate. |
 * | @ref MediaConfig::AudioChannels    | int      | 2       | ALSA capture channel count. |
 *
 * V4L2 camera controls (all default to -1 = device default):
 * V4l2Brightness, V4l2Contrast, V4l2Saturation, V4l2Hue, V4l2Gamma,
 * V4l2Sharpness, V4l2BacklightComp, V4l2WhiteBalanceTemp,
 * V4l2AutoWhiteBalance, V4l2ExposureAbsolute, V4l2AutoExposure,
 * V4l2Gain, V4l2PowerLineFreq, V4l2JpegQuality.
 *
 * @par Example
 * @code
 * MediaIO::Config cfg = MediaIO::defaultConfig("V4L2");
 * cfg.set(MediaConfig::V4l2DevicePath, String("/dev/video0"));
 * cfg.set(MediaConfig::V4l2AudioDevice, String("hw:1,0"));
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Output);
 * Frame::Ptr frame;
 * io->readFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 */
class MediaIOTask_V4L2 : public MediaIOTask {
        public:
                /// @brief Stats key: total frames captured by the video thread.
                static inline const MediaIOStats::ID StatsCaptured{"V4l2Captured"};
                /// @brief Stats key: total ALSA overruns recovered.
                static inline const MediaIOStats::ID StatsAlsaOverruns{"V4l2AlsaOverruns"};

                /**
                 * @brief Returns the format descriptor for this backend.
                 * @return A FormatDesc with no file extensions (capture source).
                 */
                static MediaIO::FormatDesc formatDesc();

                /** @brief Constructs a MediaIOTask_V4L2. */
                MediaIOTask_V4L2() = default;

                /** @brief Destructor. */
                ~MediaIOTask_V4L2() override;

                /** @brief Maps a V4L2 pixel format fourcc to a PixelDesc::ID. */
                static PixelDesc::ID v4l2ToPixelDesc(uint32_t v4l2fmt);

                /** @brief Maps a PixelDesc::ID to a V4L2 pixel format fourcc. */
                static uint32_t pixelDescToV4l2(PixelDesc::ID pd);

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                // -- V4L2 helpers --
                Error openVideo(const MediaIO::Config &cfg);
                Error startStreaming();
                void  stopStreaming();
                void  closeVideo();

                // -- ALSA helpers --
                Error openAudio(const MediaIO::Config &cfg);
                void  closeAudio();

                // -- Capture threads --
                void  stopThreads();
                void  videoCaptureLoop();
                void  audioCaptureLoop();

                // -- V4L2 state --
                struct MmapBuffer {
                        void   *start  = nullptr;
                        size_t  length = 0;
                };
                int                     _fd = -1;
                List<MmapBuffer>        _buffers;
                bool                    _streaming = false;
                ImageDesc               _imageDesc;

                // -- ALSA state --
                snd_pcm_t              *_pcm = nullptr;
                AudioDesc               _audioDesc;
                bool                    _audioEnabled = false;

                // -- Capture threads --
                std::atomic<bool>       _stopFlag{false};
                std::atomic<int>        _deviceError{0};  // errno from device failure (ENODEV etc.)
                std::thread             _videoThread;
                std::thread             _audioThread;

                // -- Video frame queue (video thread → read command) --
                static constexpr int    VideoQueueDepth = 2;
                Queue<Image::Ptr>       _videoQueue;

                // -- Audio ring buffer (audio thread → read command) --
                AudioBuffer             _audioRing;

                // -- Telemetry (updated atomically by capture threads) --
                std::atomic<int64_t>    _framesCaptured{0};
                std::atomic<int64_t>    _alsaOverruns{0};

                // -- Debug reporting --
                PeriodicCallback        _debugReport;
                int64_t                 _ringAccum = 0;         // sum of ring levels sampled each frame
                int64_t                 _ringAccumFrames = 0;   // frames sampled since last report
                double                  _ringAvgBaseline = 0.0; // first report's average
                TimeStamp               _ringBaselineTime;      // V4L2 timestamp at first report
                bool                    _ringBaselineSet = false;

                // -- Capture timestamp tracking (from V4L2 DQBUF) --
                TimeStamp               _lastCaptureTime;       // most recent frame's V4L2 timestamp
                TimeStamp               _firstCaptureTime;      // first frame's V4L2 timestamp
                int64_t                 _firstCaptureFrame = -1;// frame index of first timestamp
                double                  _frameDeltaSum = 0.0;   // sum of inter-frame intervals (sec) this period
                double                  _frameDeltaSqSum = 0.0; // sum of squared deltas for jitter
                int64_t                 _frameDeltaCount = 0;   // deltas accumulated this period
                double                  _prevPeriodFps = 0.0;   // previous period's measured fps

                // -- Audio drift overflow protection --
                bool                    _ringOverflowWarned = false;

                // -- General state --
                FrameRate               _frameRate;
                int64_t                 _frameCount = 0;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_V4L2
