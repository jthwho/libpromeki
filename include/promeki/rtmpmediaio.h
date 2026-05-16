/**
 * @file      rtmpmediaio.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <promeki/atomic.h>
#include <promeki/audiodesc.h>
#include <promeki/dedicatedthreadmediaio.h>
#include <promeki/enums.h>
#include <promeki/frame.h>
#include <promeki/framenumber.h>
#include <promeki/framerate.h>
#include <promeki/histogram.h>
#include <promeki/imagedesc.h>
#include <promeki/mediaiofactory.h>
#include <promeki/namespace.h>
#include <promeki/pacinggate.h>
#include <promeki/queue.h>
#include <promeki/rtmpsession.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/url.h>

PROMEKI_NAMESPACE_BEGIN

class RtmpClient;
class Thread;

/**
 * @brief MediaIO backend that publishes / subscribes to an RTMP stream.
 * @ingroup proav
 *
 * @c RtmpMediaIO wraps the standalone @ref RtmpClient behind the
 * @ref MediaIO async-command interface.  Both @c MediaIO::Sink (publish)
 * and @c MediaIO::Source (play) modes are supported; @c ReadWrite is
 * rejected — an RTMP publish and an RTMP play are conceptually
 * different streams and should not share a MediaIO.
 *
 * @par Supported codecs (writer mode)
 *
 *  - **Video** — H.264 / AVC (legacy FLV CodecID = 7) or HEVC / H.265
 *    (Enhanced-RTMP, codec FourCC = @c "hvc1").  The instance expects
 *    a stream of @ref CompressedVideoPayload access units; the
 *    pipeline planner is responsible for inserting a
 *    @ref VideoEncoderMediaIO upstream when the source is
 *    uncompressed.  Access units may arrive in either Annex-B
 *    (start-code framed) or AVCC (length-prefixed) form — the
 *    packetizer converts Annex-B to AVCC inline before framing.
 *  - **Audio** — AAC raw (FLV SoundFormat = 10).  The instance
 *    expects a stream of @ref CompressedAudioPayload frames; the
 *    planner is responsible for inserting an
 *    @ref AudioEncoderMediaIO (and resampler if needed) upstream
 *    when the source is uncompressed.
 *
 * @par Sequence-header policy
 *
 * On the first IDR / IRAP access unit the writer extracts the
 * parameter sets, builds an @c avcC / @c hvcC blob, and emits a
 * @ref FlvVideoTag with @c packetType = @ref FlvVideoTag::SequenceHeader
 * before any subsequent @c Nalu access unit ships.  On the first AAC
 * frame the writer derives an @c AudioSpecificConfig from the
 * @ref AudioDesc and emits a @ref FlvAudioTag with @c aacPacketType =
 * @ref FlvAudioTag::AudioSpecificConfig before any @c Raw frame ships.
 * After the headers go out, every subsequent access unit ships as
 * @c Nalu / @c Raw with a 32-bit-millisecond presentation timestamp.
 *
 * @par First-IDR gating
 *
 * Per @ref MediaConfig::RtmpDropUntilKeyframe (default @c true),
 * non-keyframe video access units arriving before the first IDR are
 * silently dropped — most destinations reject a publish that begins
 * on an inter-frame.  Audio is not gated.
 *
 * @par Threading
 *
 * Runs on the per-instance dedicated worker thread inherited from
 * @ref DedicatedThreadMediaIO.  The strand never blocks on wire I/O —
 * it pushes incoming frames onto a bounded per-kind payload queue
 * and returns.  A single @ref PacketizerThread drains the queue,
 * builds @ref FlvVideoTag / @ref FlvAudioTag values, and dispatches
 * to the owned @ref RtmpClient via @c sendVideo / @c sendAudio.  The
 * RtmpClient owns the wire-side writer thread and the TCP / TLS
 * socket; from this class's perspective the socket is asynchronous.
 *
 * @par Mode support
 *
 * Source-mode (play) drains the @ref RtmpClient's per-kind receive
 * queues from a depacketizer thread, reassembles compressed
 * @ref Frame payloads, and pushes them onto the strand-side
 * @c _readerQueue that @c executeCmd(Read) drains.  The wire-side
 * @c MediaDesc is unknown until the first @c SequenceHeader arrives;
 * @ref proposeOutput is not yet implemented (the planner reconciles
 * the concrete codec when the first frame's descriptor is
 * populated, same model as RTP RX uses today).
 *
 * @par Example
 * @code
 * MediaIO::Config cfg;
 * cfg.set(MediaConfig::Type, "Rtmp");
 * cfg.set(MediaConfig::RtmpUrl, Url("rtmp://example.com/app/streamKey"));
 * cfg.set(MediaConfig::OpenMode, MediaIOOpenMode(MediaIOOpenMode::Write));
 *
 * MediaIO *io = MediaIO::create(cfg);
 * io->open(MediaIO::Sink);
 * io->writeFrame(frame);   // frame.videoPayloads()[0] = CompressedVideoPayload
 * io->close();
 * delete io;
 * @endcode
 */
class RtmpMediaIO : public DedicatedThreadMediaIO {
                PROMEKI_OBJECT(RtmpMediaIO, DedicatedThreadMediaIO)
        public:
                // ---- Telemetry keys ----

                /** @brief int64_t — total frames the strand accepted from upstream. */
                static inline const MediaIOStats::ID StatsFramesSent{"FramesSent"};
                /** @brief int64_t — total frames the depacketizer emitted to upstream. */
                static inline const MediaIOStats::ID StatsFramesReceived{"FramesReceived"};
                /** @brief int64_t — total video RTMP messages dispatched. */
                static inline const MediaIOStats::ID StatsVideoMessagesSent{"VideoMessagesSent"};
                /** @brief int64_t — total audio RTMP messages dispatched. */
                static inline const MediaIOStats::ID StatsAudioMessagesSent{"AudioMessagesSent"};
                /** @brief int64_t — total video RTMP messages received. */
                static inline const MediaIOStats::ID StatsVideoMessagesReceived{"VideoMessagesReceived"};
                /** @brief int64_t — total audio RTMP messages received. */
                static inline const MediaIOStats::ID StatsAudioMessagesReceived{"AudioMessagesReceived"};
                /** @brief int64_t — cumulative bytes the @ref RtmpClient handed to the chunk layer. */
                static inline const MediaIOStats::ID StatsBytesSent{"BytesSent"};
                /** @brief int64_t — cumulative bytes the @ref RtmpClient read off the chunk layer. */
                static inline const MediaIOStats::ID StatsBytesReceived{"BytesReceived"};
                /** @brief int64_t — current depth of the strand→packetizer payload queue. */
                static inline const MediaIOStats::ID StatsSendQueueDepth{"SendQueueDepth"};
                /** @brief int64_t — current depth of the depacketizer→strand reader queue. */
                static inline const MediaIOStats::ID StatsReadQueueDepth{"ReadQueueDepth"};
                /** @brief int64_t — count of writeFrame calls that hit Error::TryAgain on the bounded queue. */
                static inline const MediaIOStats::ID StatsSendQueueOverflows{"SendQueueOverflows"};
                /** @brief int64_t — wallclock ms from open entry to NetConnection.Connect.Success. */
                static inline const MediaIOStats::ID StatsConnectDurationMs{"ConnectDurationMs"};
                /** @brief int64_t — wallclock ms from open entry to handshake completion. */
                static inline const MediaIOStats::ID StatsHandshakeDurationMs{"HandshakeDurationMs"};
                /** @brief int64_t — count of pre-IDR access units dropped (sink-mode gating). */
                static inline const MediaIOStats::ID StatsVideoFramesDroppedPreIdr{"VideoFramesDroppedPreIdr"};

                /** @brief int64_t — PacingGate `OnTime` verdicts (video, sink). */
                static inline const MediaIOStats::ID StatsPacingTicksOnTime{"PacingTicksOnTime"};
                /** @brief int64_t — PacingGate `Late` verdicts (video, sink). */
                static inline const MediaIOStats::ID StatsPacingTicksLate{"PacingTicksLate"};
                /** @brief int64_t — PacingGate `Skip` verdicts (video, sink) — i.e. frames dropped by the gate. */
                static inline const MediaIOStats::ID StatsPacingTicksSkipped{"PacingTicksSkipped"};
                /** @brief int64_t — PacingGate timeline reanchors (video, sink). */
                static inline const MediaIOStats::ID StatsPacingReanchors{"PacingReanchors"};
                /** @brief String — live gate-clock binding tag (`"internal"` / `"external"` / `"none"`). */
                static inline const MediaIOStats::ID StatsPacingClockKind{"PacingClockKind"};

                /** @brief Constructs an idle RtmpMediaIO. */
                explicit RtmpMediaIO(ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes any still-open stream. */
                ~RtmpMediaIO() override;

                /**
                 * @brief Per-process unique RtmpMediaIO instance ID.
                 *
                 * Assigned at construction from a process-local atomic
                 * counter that starts at 1 and never repeats.  Useful
                 * for log correlation when multiple sessions are alive.
                 */
                uint64_t objectId() const { return _objectId; }

                /**
                 * @brief Declares the compressed shape the RTMP sink wants.
                 *
                 * RTMP can only ship compressed essence on the wire
                 * (H.264 / HEVC video, AAC audio).  When the planner
                 * offers an uncompressed shape, this override rewrites
                 * the image PixelFormat to the @ref MediaConfig::RtmpVideoCodec
                 * codec's canonical compressed @ref PixelFormat and the
                 * audio AudioFormat to the @ref MediaConfig::RtmpAudioCodec
                 * codec's canonical compressed @ref AudioFormat.  That
                 * is the signal @ref VideoEncoderFactory::bridge /
                 * @ref AudioEncoderFactory::bridge need to splice their
                 * encoders into the pipeline.  When the offered shape
                 * is already compressed it is returned as-is.
                 *
                 * Reader mode falls back to passthrough — the planner
                 * doesn't currently introspect RTMP sources for
                 * downstream-format negotiation.
                 */
                Error proposeInput(const MediaDesc &offered, MediaDesc *preferred) const override;

        protected:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandRead &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;

                /**
                 * @brief Accepts an external pacing clock for sink mode.
                 *
                 * In sink mode the supplied @ref Clock paces the
                 * strand-side video frame cadence: the strand sleeps
                 * until the clock reports the next frame deadline
                 * before pushing the @c Frame onto the per-kind
                 * @c PayloadQueue.  Audio is not paced by this clock —
                 * AAC's natural cadence rides alongside through the
                 * bounded message queue.
                 *
                 * Source mode returns @c Error::NotSupported.  A null
                 * @c cmd.clock detaches the external clock; the gate
                 * then re-arms an @c Internal wall clock when the
                 * configured @ref MediaConfig::RtmpVideoPacing is
                 * @c Internal, or stays a no-op for @c External /
                 * @c None.
                 */
                Error executeCmd(MediaIOCommandSetClock &cmd) override;

                // Wakes the reader-side executeCmd(Read) loop so close()
                // can drain a strand parked on the reader queue pop.
                void cancelBlockingWork() override;

        private:
                class PacketizerThread;       ///< Sink-side: pops frames, builds FLV tags, dispatches via RtmpClient.
                class DepacketizerThread;     ///< Source-side: drains RtmpClient queues into the reader queue.

                /// @brief Tear down all owned threads, the client, and reset all state.
                void resetAll();

                /// @brief Builds an @ref RtmpConnectOptions from the current config.
                RtmpConnectOptions buildConnectOptions(const MediaIO::Config &cfg) const;

                /// @brief Returns true when @p frame carries at least one valid video payload.
                static bool hasVideoEssence(const Frame &frame);

                /// @brief Returns true when @p frame carries at least one valid audio payload.
                static bool hasAudioEssence(const Frame &frame);

                /**
                 * @brief Paces the next video frame against @ref _videoPaceGate.
                 *
                 * No-op when the gate has no clock bound (mode is
                 * @c External with no external clock yet, or mode is
                 * @c None).  Otherwise sleeps until the next deadline
                 * and returns @c true to send, @c false to drop the
                 * frame (lag past the skip threshold).  Reanchor is
                 * logged but the frame still ships.  Clock errors are
                 * logged and the frame ships unpaced.
                 */
                bool paceVideoFrame();

                /**
                 * @brief Arms (or re-arms) @ref _videoPaceGate based on
                 *        the configured pacing mode.
                 *
                 * Sets the gate's period and skip/reanchor thresholds
                 * from @ref _frameRate / @ref _paceSkipThresholdMs /
                 * @ref _paceReanchorThresholdMs.  When @ref _videoPacingMode
                 * is @c Internal, binds a fresh @ref WallClock; otherwise
                 * leaves the clock unbound (External waits for
                 * @c setClock; None never paces).
                 */
                void armVideoPaceGate();

                /// @brief String tag describing the live gate-clock binding.
                String paceClockKind() const;

                /**
                 * @brief Slot for @c RtmpClient::disconnectedSignal.
                 *
                 * Latches the disconnect reason so subsequent
                 * @c executeCmd(Read) / @c executeCmd(Write) calls
                 * surface it to the pipeline, and re-emits as
                 * @c errorOccurredSignal so the pipeline-level error
                 * cascade tears the stage down.
                 *
                 * Marshalled to the strand event loop via the
                 * @c connect(..., this) signal binding, so the body
                 * runs single-threaded on the strand.
                 */
                void onClientDisconnected(Error reason);

                // Owned RtmpClient — instantiated in executeCmd(Open),
                // destroyed in resetAll.  Lives across the open/close
                // cycle so the writer / reader threads it owns service
                // the wire.
                UniquePtr<RtmpClient> _client;

                // Worker threads.  Both are nullptr when idle.
                UniquePtr<PacketizerThread>   _packetizer;
                UniquePtr<DepacketizerThread> _depacketizer;

                // Strand-side bounded reader queue — populated by the
                // depacketizer thread, drained by executeCmd(Read).
                Queue<Frame> _readerQueue;

                // Cancellation latch for executeCmd(Read).  Set by
                // cancelBlockingWork(); cleared at every Open.
                Atomic<bool>      _readCancelled{false};

                // Latched when @c RtmpClient::disconnectedSignal fires
                // (peer went away, socket I/O error, etc.).  Polled by
                // the packetizer / depacketizer worker loops so they
                // exit cleanly instead of spewing per-frame send
                // failures into the log, and consulted by
                // executeCmd(Read) / executeCmd(Write) so the failure
                // surfaces to the pipeline as a write/read error.
                // @c _disconnectErrorCode stores the disconnect reason
                // as an @c Error::Code (int) to keep the atomic POD;
                // reconstruct an @c Error via @c Error(static_cast).
                Atomic<bool>      _clientDisconnected{false};
                Atomic<int>       _disconnectErrorCode{0};

                // Sink-side state set at Open from the MediaConfig +
                // pending descriptor, read by the packetizer thread.
                ImageDesc _imageDesc;
                AudioDesc _audioDesc;
                Url       _url;
                String    _streamKey;
                bool      _readerMode = false;
                bool      _dropUntilKeyframe = true;
                bool      _repeatParameterSets = true;
                bool      _enhancedRtmp = true;
                bool      _emitAnnexB = false;
                bool      _dataEnabled = true;
                int       _sendQueueDepth = 64;
                int       _readQueueDepth = 64;

                // Per-instance frame counters used to populate
                // currentFrame / frameCount on read / write commands.
                // Strand-owned; updated only inside executeCmd().
                FrameCount _frameCount{0};
                FrameCount _framesSent{0};

                // Cumulative stats counters bumped by the worker
                // threads, read by executeCmd(Stats).  Atomics for
                // lock-free cross-thread aggregation.
                Atomic<int64_t> _readerFramesReceived{0};
                Atomic<int64_t> _videoFramesDroppedPreIdr{0};
                Atomic<int64_t> _sendQueueOverflows{0};

                // Strand-side video pacing.  Mirrors RtpMediaIO's
                // _videoGate but defaults to an Internal wall-clock
                // binding since TCP has no kernel-pacing analog to fall
                // back on.  Touched only from the strand.
                RtmpVideoPacing _videoPacingMode{RtmpVideoPacing::Internal};
                PacingGate      _videoPaceGate;
                FrameRate       _frameRate;
                int             _paceSkipThresholdMs = 0;
                int             _paceReanchorThresholdMs = 0;
                bool            _paceClockIsExternal = false;

                // Open-time telemetry — captured around the
                // RtmpClient::open / publish calls.
                int64_t _connectDurationMs = 0;
                int64_t _handshakeDurationMs = 0;

                // Process-local monotonic counter that gives every
                // RtmpMediaIO a distinct id within one process.
                static Atomic<uint64_t> _nextObjectId;
                uint64_t                _objectId = 0;
};

/**
 * @brief @ref MediaIOFactory for the RTMP backend.
 * @ingroup proav
 */
class RtmpFactory : public MediaIOFactory {
        public:
                RtmpFactory() = default;

                String name() const override { return String("Rtmp"); }
                String displayName() const override { return String("RTMP Stream"); }
                String description() const override {
                        return String("RTMP / RTMPS publisher and subscriber "
                                      "(H.264 + HEVC video, AAC audio)");
                }
                StringList schemes() const override {
                        return StringList{String("rtmp"), String("rtmps")};
                }

                bool canBeSource() const override { return true; }
                bool canBeSink() const override { return true; }

                Config::SpecMap configSpecs() const override;
                Error           urlToConfig(const Url &url, Config *outConfig) const override;
                MediaIO        *create(const Config &config, ObjectBase *parent = nullptr) const override;
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK