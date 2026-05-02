/**
 * @file      ndimediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/config.h>

#if PROMEKI_ENABLE_NDI

#include <atomic>
#include <thread>
#include <promeki/audiobuffer.h>
#include <promeki/audiodesc.h>
#include <promeki/clock.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/imagedesc.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/mediaiostats.h>
#include <promeki/metadata.h>
#include <promeki/mutex.h>
#include <promeki/ndiclock.h>
#include <promeki/pcmaudiopayload.h>
#include <promeki/queue.h>
#include <promeki/string.h>
#include <promeki/uncompressedvideopayload.h>

// Forward-declare the NDI SDK's opaque instance types so we can hold
// instance handles in the class body without dragging the full SDK
// headers into the public include.  The .cpp pulls in
// <Processing.NDI.Lib.h> to actually drive the API.
struct NDIlib_send_instance_type;
struct NDIlib_recv_instance_type;

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief MediaIO backend that sends and receives NDI media streams.
 * @ingroup proav
 *
 * Wraps NewTek / Vizrt's NDI (Network Device Interface) protocol
 * behind libpromeki's generic MediaIO interface.  One MediaIO
 * instance carries one NDI source / sender, multiplexing video,
 * audio, and metadata onto a single named NDI stream.
 *
 * @par Mode support (v1)
 *
 * Sink mode (`MediaIO::Sink`) is fully supported.  Source mode
 * (receiving NDI streams from the network) lands in a follow-up
 * phase — opening as a Source today returns
 * @c Error::NotSupported.
 *
 * @par Threading
 *
 * Runs on a per-instance dedicated worker thread inherited from
 * @ref DedicatedThreadMediaIO.  The NDI SDK has its own internal
 * I/O thread that handles network traffic — `send_send_video_v2`
 * and friends queue work onto that thread and return promptly, so
 * the strand worker is the only thread libpromeki spins up for
 * sender mode.
 *
 * @par Format support (sink mode)
 *
 * Accepted promeki PixelFormats (no conversion done by the
 * backend):
 *
 * | PixelFormat                                 | NDI FourCC   |
 * |---------------------------------------------|--------------|
 * | @c YUV8_422_UYVY_Rec709                     | UYVY         |
 * | @c YUV8_420_SemiPlanar_Rec709               | NV12         |
 * | @c YUV8_420_Planar_Rec709                   | I420         |
 * | @c BGRA8_sRGB                               | BGRA         |
 * | @c RGBA8_sRGB                               | RGBA         |
 * | @c YUV10_422_SemiPlanar_LE_Rec709           | P216 (10)    |
 * | @c YUV12_422_SemiPlanar_LE_Rec709           | P216 (12)    |
 * | @c YUV16_422_SemiPlanar_LE_Rec709           | P216 (16)    |
 *
 * Other shapes (UYVY-style packed 10/12-bit, 4:2:0 16-bit, etc.)
 * require an upstream CSC stage — opening with one of those formats
 * returns a clear error naming a supported alternative.
 *
 * Audio is sent as 32-bit float planar (FLTP) at the descriptor's
 * sample rate and channel count.  PCM input formats are accepted
 * as-is; the backend converts to FLTP at send time.
 *
 * @par Discovery
 *
 * NDI source discovery (relevant for source mode in a follow-up
 * phase) runs in a process-wide background thread — see
 * @ref NdiDiscovery.  Sink mode does not start the discovery
 * thread; senders advertise their own presence via mDNS independent
 * of the discovery registry.
 *
 * @par Example (sink mode)
 *
 * @code
 * MediaDesc desc;
 * desc.setFrameRate(FrameRate(FrameRate::FPS_30));
 * desc.imageList().pushToBack(ImageDesc(Size2Du32(1920, 1080),
 *                                       PixelFormat(PixelFormat::YUV8_422_UYVY_Rec709)));
 * desc.audioList().pushToBack(AudioDesc(AudioFormat::PCMI_Float32LE, 48000.0f, 2));
 *
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Ndi");
 * cfg.set(MediaConfig::NdiSendName, "PromekiTest");
 *
 * MediaIO *io = MediaIO::create(cfg);
 * io->setExpectedDesc(desc);
 * io->open(MediaIO::Sink);
 * io->writeFrame(frame);
 * io->close();
 * delete io;
 * @endcode
 */
class NdiMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(NdiMediaIO, DedicatedThreadMediaIO)
        public:
                /** @brief int64_t — total NDI video frames transmitted. */
                static inline const MediaIOStats::ID StatsFramesSent{"NdiFramesSent"};
                /** @brief int64_t — total NDI audio frames transmitted. */
                static inline const MediaIOStats::ID StatsAudioFramesSent{"NdiAudioFramesSent"};
                /** @brief int64_t — total bytes pushed into the NDI sender (video + audio). */
                static inline const MediaIOStats::ID StatsBytesSent{"NdiBytesSent"};

                /** @brief int64_t — total NDI video frames received. */
                static inline const MediaIOStats::ID StatsFramesReceived{"NdiFramesReceived"};
                /** @brief int64_t — total NDI audio frames received. */
                static inline const MediaIOStats::ID StatsAudioFramesReceived{"NdiAudioFramesReceived"};
                /** @brief int64_t — total NDI metadata frames received. */
                static inline const MediaIOStats::ID StatsMetadataReceived{"NdiMetadataReceived"};
                /** @brief int64_t — receiver-side dropped frames (queue overflow). */
                static inline const MediaIOStats::ID StatsDroppedReceives{"NdiDroppedReceives"};

                /** @brief Constructs an NdiMediaIO. */
                NdiMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes any still-open sender / receiver. */
                ~NdiMediaIO() override;

                /**
                 * @brief Tells the planner which sink-side video shapes NDI accepts.
                 *
                 * NDI's wire formats are a small fixed set (UYVY, NV12,
                 * I420, BGRA, RGBA, P216) — anything else has to be
                 * converted upstream.  This override returns the offered
                 * desc unchanged when its pixel format already maps to
                 * an NDI FourCC, and otherwise asks for a same-color-
                 * family fallback so the planner splices a CSC bridge
                 * in front of us instead of the open() path failing
                 * with @c FormatMismatch.
                 */
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                void  cancelBlockingWork() override;

        private:
                Error openSink(const MediaIO::Config &cfg, const MediaDesc &mediaDesc);
                void  closeSink();

                Error openSource(const MediaIO::Config &cfg);
                void  closeSource();
                void  captureLoop();

                Error sendVideo(const UncompressedVideoPayload &vp);
                Error sendAudio(const PcmAudioPayload &ap);

                // NDI handles — opaque pointers to SDK-managed state.
                NDIlib_send_instance_type *_send = nullptr;
                NDIlib_recv_instance_type *_recv = nullptr;

                // Resolved at openSink() — used to pre-allocate the FLTP
                // conversion buffer once per stream rather than per frame.
                size_t _audioChannels   = 0;
                float  _audioSampleRate = 0.0f;

                // Configuration captured at open time.
                String _sendName;
                String _sendGroups;
                String _extraIps;
                bool   _sendClockVideo = false;
                bool   _sendClockAudio = false;
                bool   _sinkMode       = false;

                // Resolved video shape — populated at openSink time.
                ImageDesc _imageDesc;
                FrameRate _frameRate;

                // Telemetry — atomic so executeCmd(Stats) can read without
                // racing the strand worker that mutates these.
                std::atomic<int64_t> _framesSent{0};
                std::atomic<int64_t> _audioFramesSent{0};
                std::atomic<int64_t> _bytesSent{0};

                // ---- Source-mode state ----
                //
                // The capture thread loops on @c NDIlib_recv_capture_v3
                // with a short timeout, demultiplexes the returned
                // frames into the queues / ring below, and frees the
                // SDK buffers via the matching recv_free_* call.  All
                // queues are mutex-protected internally; the strand
                // drains them in @c executeCmd(MediaIOCommandRead).
                std::thread          _captureThread;
                std::atomic<bool>    _stopFlag{false};
                std::atomic<bool>    _readCancelled{false};
                // Reader-side video queue.  Capacity is small (matches
                // V4L2's "drop oldest, count drop") because NDI pushes
                // at the source's true frame rate and we want timing-
                // accurate delivery rather than a long backlog.
                static constexpr int     VideoQueueDepth = 2;
                Queue<UncompressedVideoPayload::Ptr> _videoQueue;
                AudioBuffer              _audioRing;
                Mutex                    _audioMutex;
                // Most-recent received audio NDI timestamp (100ns
                // ticks).  Captured by the capture thread, drained on
                // every executeCmd(Read).  Used to stamp the
                // dequeued audio payload with a sender-anchored
                // CaptureTime so downstream drift correction sees
                // wire arrival times.
                std::atomic<int64_t>     _lastAudioTimestampTicks{0};
                Mutex                    _metadataMutex;
                Metadata                 _pendingMetadata;
                bool                     _hasPendingMetadata = false;

                // Reader configuration captured at openSource time.
                // The bit-depth tag controls how P216 frames are tagged
                // when emitted (see @ref NdiReceiveBitDepth).
                int                      _captureTimeoutMs = 100;
                int                      _bitDepthHint     = 0; // 0 = Auto (16-bit).

                // Reader-side telemetry.
                std::atomic<int64_t> _framesReceived{0};
                std::atomic<int64_t> _audioFramesReceived{0};
                std::atomic<int64_t> _metadataReceived{0};
                std::atomic<int64_t> _droppedReceives{0};

                // Source-mode clock — driven by NDI per-frame
                // timestamps from the capture thread.  Owned via
                // Clock::Ptr so the bound port-group can keep it
                // alive past close if any consumer still holds a
                // reference.
                Clock::Ptr _sourceClock;
};

/**
 * @brief @ref MediaIOFactory for the NDI backend.
 * @ingroup proav
 */
class NdiFactory : public MediaIOFactory {
        public:
                NdiFactory() = default;

                String name() const override { return String("Ndi"); }
                String displayName() const override { return String("NDI Stream"); }
                String description() const override {
                        return String("NDI (Network Device Interface) media transport "
                                      "(uncompressed video + audio + metadata over IP)");
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                StringList schemes() const override { return {String("ndi")}; }
                bool       canHandlePath(const String &path) const override;
                StringList enumerate() const override;
                Error      urlToConfig(const Url &url, Config *outConfig) const override;

                Config::SpecMap configSpecs() const override;

                MediaIO *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NDI
