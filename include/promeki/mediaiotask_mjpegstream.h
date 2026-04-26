/**
 * @file      mediaiotask_mjpegstream.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/namespace.h>
#include <promeki/atomic.h>
#include <promeki/buffer.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/framerate.h>
#include <promeki/httphandler.h>
#include <promeki/list.h>
#include <promeki/map.h>
#include <promeki/mediadesc.h>
#include <promeki/mediaiotask.h>
#include <promeki/mutex.h>
#include <promeki/string.h>
#include <promeki/timestamp.h>
#include <promeki/uniqueptr.h>
#include <promeki/videoencoder.h>

PROMEKI_NAMESPACE_BEGIN

class HttpServer;

/**
 * @brief Subscriber interface for @ref MediaIOTask_MjpegStream.
 * @ingroup proav
 *
 * Attach a concrete subscriber to a live @ref MediaIOTask_MjpegStream
 * via @ref MediaIOTask_MjpegStream::attachSubscriber and the sink
 * delivers a callback for every newly-encoded JPEG frame plus a final
 * @ref onClosed callback when the sink closes (or when the subscriber
 * is detached explicitly).
 *
 * @par Threading and lifetime
 * Callbacks are dispatched on the sink's encoder strand.  The
 * subscriber must do its work quickly — copy the JPEG bytes, hand
 * them to a queue or a socket, and return.  Blocking inside the
 * callback stalls the encode pipeline and starves every other
 * attached subscriber.  Lifetime of the subscriber is owned by the
 * caller; subscribers must remain alive until either @ref onClosed
 * has been delivered or @ref MediaIOTask_MjpegStream::detachSubscriber
 * has returned.
 *
 * @par Example
 * @code
 * struct CountingSub : public MjpegStreamSubscriber {
 *         std::atomic<int> count{0};
 *         void onFrame(const Buffer &, const TimeStamp &) override { count++; }
 *         void onClosed() override {}
 * };
 *
 * CountingSub sub;
 * int id = sink->attachSubscriber(&sub);
 * // ... encode some frames ...
 * sink->detachSubscriber(id);
 * @endcode
 */
class MjpegStreamSubscriber {
        public:
                /** @brief Virtual destructor. */
                virtual ~MjpegStreamSubscriber() = default;

                /**
                 * @brief Called once per newly-encoded JPEG frame.
                 *
                 * Runs on the sink's encoder strand.  Implementations
                 * must not block — capture the @ref Buffer::Ptr (it
                 * shares ownership; no copy required) or hand it to a
                 * queue, and return.
                 *
                 * The same @ref Buffer::Ptr is dispatched to every
                 * attached subscriber, so the encoder allocates the
                 * JPEG bytes exactly once per encode.
                 *
                 * @param jpeg The JPEG-encoded bytes, shared by pointer.
                 * @param ts   Wall-clock timestamp at the moment the
                 *             frame finished encoding.
                 */
                virtual void onFrame(const Buffer::Ptr &jpeg, const TimeStamp &ts) = 0;

                /**
                 * @brief Called once when the subscription terminates.
                 *
                 * Fires either because the sink closed (every still-
                 * attached subscriber is notified) or because
                 * @ref MediaIOTask_MjpegStream::detachSubscriber was
                 * called for this subscriber.  No further @ref onFrame
                 * callbacks will arrive after @ref onClosed has run.
                 */
                virtual void onClosed() = 0;
};

/**
 * @brief Snapshot of @ref MediaIOTask_MjpegStream internal counters.
 * @ingroup proav
 *
 * Returned by @ref MediaIOTask_MjpegStream::snapshot.  Counters
 * accumulate from the most recent successful @c open until the next
 * @c close.  All counters are protected by an internal mutex so the
 * snapshot is safe to read from any thread regardless of strand
 * activity.
 */
struct MjpegStreamSnapshot {
        /// @brief JPEG payloads successfully encoded since open.
        int64_t framesEncoded     = 0;
        /// @brief Frames discarded by the rate gate before encoding.
        int64_t framesRateLimited = 0;
        /// @brief Frames the encoder rejected (zero on the happy path).
        int64_t framesEncodeError = 0;
        /// @brief Sum of per-frame encode times in microseconds.
        int64_t totalEncodeUs     = 0;
        /// @brief Largest single per-frame encode time in microseconds.
        int64_t peakEncodeUs      = 0;
        /// @brief Number of latency samples (== framesEncoded).
        int64_t encodeSamples     = 0;
        /// @brief Total encoded bytes since open.
        int64_t totalEncodedBytes = 0;
};

/**
 * @brief MediaIO sink that JPEG-encodes incoming frames at a
 *        rate-gated cadence and broadcasts each result to attached
 *        subscribers.
 * @ingroup proav
 *
 * @c MediaIOTask_MjpegStream is the transport-agnostic core of the
 * pipeline demo's preview feed.  It accepts whatever uncompressed
 * 8-bit interleaved 4:2:0 / 4:2:2 / 4:4:4 / RGB / RGBA video the
 * planner can deliver, drops frames that arrive faster than
 * @ref MediaConfig::MjpegMaxFps, JPEG-encodes the rest at
 * @ref MediaConfig::MjpegQuality, retains the most recent
 * @ref MediaConfig::MjpegMaxQueueFrames in an in-memory ring, and
 * broadcasts each newly-encoded frame to every
 * @ref MjpegStreamSubscriber attached at the time.
 *
 * @par No internal CSC
 * The sink declares its accepted descriptor via @c proposeInput so
 * the pipeline planner inserts a CSC bridge upstream when the source
 * doesn't already produce one of the JPEG-encoder-native pixel
 * formats.  The sink itself does no colour conversion.
 *
 * @par Subscriber semantics
 *  - Subscribers receive only the newest frame on each encode — the
 *    ring is for late-attaching consumers, not for back-pressure.
 *  - On attach, the subscriber receives the most recent frame in
 *    the ring (if any) before any further encodes — so a browser
 *    connecting mid-stream sees something immediately rather than
 *    waiting up to @c 1/MjpegMaxFps for the next encode.
 *  - On @c close, every still-attached subscriber receives one
 *    @ref MjpegStreamSubscriber::onClosed callback and the ring is
 *    cleared.
 *  - On @ref detachSubscriber the subscriber receives one
 *    @ref MjpegStreamSubscriber::onClosed callback and is removed
 *    from the broadcast list before @c detachSubscriber returns; no
 *    further callbacks ever fire for that subscriber.
 *
 * @par Reported stats
 * The standard @ref MediaIOStats::FramesPerSecond and
 * @ref MediaIOStats::BytesPerSecond keys are populated by the
 * @ref MediaIO base class from its happy-path rate tracker.  This
 * sink supplements with:
 *  - @ref MediaIOStats::FramesDropped — rate-gated frames (counted
 *    via the base class @c noteFrameDropped helper).
 *  - @ref MediaIOStats::AverageLatencyMs — running mean of the
 *    per-frame JPEG encode time in milliseconds.
 *  - @ref MediaIOStats::PeakLatencyMs — peak observed JPEG encode
 *    time in milliseconds.
 *
 * @par Example
 * @code
 * MediaIO::Config cfg = MediaIO::defaultConfig("MjpegStream");
 * cfg.set(MediaConfig::MjpegMaxFps, Rational<int>(30, 1));
 * cfg.set(MediaConfig::MjpegQuality, 70);
 *
 * MediaIO *sink = MediaIO::create(cfg);
 * sink->open(MediaIO::Sink);
 * // ... push frames in via writeFrame() ...
 * sink->close();
 * delete sink;
 * @endcode
 *
 * @par Thread Safety
 * Strand-affine — see @ref MediaIOTask.
 */
class MediaIOTask_MjpegStream : public MediaIOTask {
        public:
                /// @brief Boundary literal for the multipart-x-mixed-replace HTTP route.
                static const String HttpBoundary;

                /// @brief Default @c Content-Type emitted by the @c GET-latest helper.
                static const String LatestJpegContentType;

                /**
                 * @brief Returns the format descriptor used by the
                 *        MediaIO factory registry.
                 *
                 * @par Example
                 * @code
                 * const auto desc = MediaIOTask_MjpegStream::formatDesc();
                 * REQUIRE(desc.canBeSink);
                 * REQUIRE(desc.name == "MjpegStream");
                 * @endcode
                 */
                static MediaIO::FormatDesc formatDesc();

                /**
                 * @brief Constructs an idle MJPEG stream sink.
                 *
                 * @par Example
                 * @code
                 * MediaIOTask_MjpegStream *task = new MediaIOTask_MjpegStream();
                 * MediaIO io;
                 * io.adoptTask(task);
                 * @endcode
                 */
                MediaIOTask_MjpegStream();

                /**
                 * @brief Releases internal resources.
                 *
                 * @par Example
                 * @code
                 * delete new MediaIOTask_MjpegStream();   // exits cleanly
                 * @endcode
                 */
                ~MediaIOTask_MjpegStream() override;

                /**
                 * @brief Attaches @p s to receive every newly-encoded JPEG
                 *        frame and a final @ref MjpegStreamSubscriber::onClosed
                 *        callback.
                 *
                 * The subscriber is owned by the caller; it must
                 * outlive the subscription (i.e. callers must call
                 * @ref detachSubscriber before destroying @p s, or
                 * wait for @ref MjpegStreamSubscriber::onClosed via
                 * sink @c close()).  When the sink already holds at
                 * least one encoded frame in its ring, @p s is primed
                 * with the most recent frame inside the same call so
                 * freshly-attaching consumers don't have to wait for
                 * the next encode.
                 *
                 * Returns a non-negative subscription id which is the
                 * argument to @ref detachSubscriber.  Returns -1 only
                 * if @p s is null.
                 *
                 * @par Example
                 * @code
                 * MyCollector sub;
                 * int id = sink->attachSubscriber(&sub);
                 * // ...
                 * sink->detachSubscriber(id);
                 * @endcode
                 */
                int  attachSubscriber(MjpegStreamSubscriber *s);

                /**
                 * @brief Detaches the subscriber registered under @p id.
                 *
                 * The subscriber receives a final
                 * @ref MjpegStreamSubscriber::onClosed callback before
                 * this method returns.  No further callbacks fire for
                 * the detached subscriber.  Detaching an unknown id is
                 * a no-op.
                 *
                 * @par Example
                 * @code
                 * sink->detachSubscriber(id);
                 * @endcode
                 */
                void detachSubscriber(int id);

                /**
                 * @brief Returns @c true between successful @c open
                 *        and the next @c close on the sink.
                 *
                 * Mutex-guarded so callers on the server's HTTP loop
                 * can safely read state set by the encoder strand.
                 */
                bool isStreaming() const;

                /**
                 * @brief Returns the most recently encoded JPEG buffer.
                 *
                 * Returns an empty (invalid) @ref Buffer::Ptr when the
                 * sink is closed or no frames have been encoded since
                 * the last @c open.  The returned pointer shares
                 * ownership with the ring entry it was sampled from —
                 * no copy is performed.
                 *
                 * @par Example
                 * @code
                 * Buffer::Ptr jpeg = sink->latestJpeg();
                 * if(jpeg.isValid()) writeOut(*jpeg);
                 * @endcode
                 */
                Buffer::Ptr latestJpeg() const;

                /**
                 * @brief Returns the wall-clock timestamp of the most
                 *        recently encoded frame.
                 *
                 * Returns a default-constructed @ref TimeStamp when no
                 * frame has been encoded since the last @c open.
                 *
                 * @par Example
                 * @code
                 * TimeStamp ts = sink->latestJpegTimestamp();
                 * @endcode
                 */
                TimeStamp latestJpegTimestamp() const;

                /**
                 * @brief Returns a thread-safe snapshot of the sink's
                 *        internal counters.
                 *
                 * @par Example
                 * @code
                 * MjpegStreamSnapshot s = sink->snapshot();
                 * CHECK(s.framesEncoded > 0);
                 * @endcode
                 */
                MjpegStreamSnapshot snapshot() const;

                /**
                 * @brief Registers a HTTP route on @p server at @p path
                 *        that serves a continuous MJPEG stream.
                 *
                 * The route responds with
                 * @c "Content-Type: multipart/x-mixed-replace;
                 * boundary=<HttpBoundary>".  Each newly-encoded frame
                 * is emitted as one @c image/jpeg part with its own
                 * @c Content-Length header.  Browsers consume this
                 * format natively via @c \<img @c src\>; no polling
                 * is required.
                 *
                 * Implementation: the route allocates a per-connection
                 * @ref AsyncBufferQueue and a per-connection
                 * @ref MjpegStreamSubscriber adapter that pushes
                 * boundary + headers + the encoded JPEG into the
                 * queue as @ref Buffer::Ptr segments.  The connection
                 * pumps the queue with @c Transfer-Encoding: chunked
                 * and parks on the queue's @c readyRead between
                 * frames — no busy-wait, no event-loop blocking.
                 *
                 * On client disconnect the connection's body stream
                 * is destroyed, which detaches the subscriber and
                 * closes the queue's writer side; on sink close, the
                 * subscriber receives @ref MjpegStreamSubscriber::onClosed
                 * and the queue's writer side is closed so the
                 * connection drains and finishes cleanly.
                 *
                 * If a primer frame is already in the ring when a
                 * client connects, that frame is pushed inside
                 * @c attachSubscriber so the browser shows something
                 * within milliseconds of connecting.
                 *
                 * Multiple servers / paths can be registered; each
                 * keeps its own per-route capture state.  When the
                 * sink is not open (or has been closed) at request
                 * time, the route returns @c 503 with a short
                 * plain-text body.
                 *
                 * @par Example
                 * @code
                 * HttpServer server;
                 * server.listen(SocketAddress::loopback(0));
                 * sink->registerHttpRoute(server, "/preview");
                 * @endcode
                 */
                void registerHttpRoute(HttpServer &server, const String &path);

                /**
                 * @brief Returns a self-contained route handler that
                 *        streams from @p sink.
                 *
                 * Identical streaming shape to the route installed by
                 * @ref registerHttpRoute (continuous
                 * @c multipart/x-mixed-replace with one
                 * @c image/jpeg part per frame, and @c 503 when the
                 * sink is not currently open).  The seam exists so
                 * callers that want to mount a *dynamic* path (e.g.
                 * the pipeline demo's per-id @c /api/pipelines/{id}
                 * @c /preview/{stage} route) can register a single
                 * handler and dispatch to whichever sink they
                 * resolved at request time, without paying the cost
                 * of re-registering routes per pipeline.
                 *
                 * Lifetime: the returned handler captures @p sink by
                 * raw pointer; @p sink must outlive every request the
                 * handler may serve.  When @p sink is @c nullptr the
                 * handler responds with @c 503.  Multiple distinct
                 * paths can share one handler instance.
                 *
                 * @param sink The sink to stream from (non-owning).
                 * @return A handler suitable for @ref HttpServer::route.
                 */
                static HttpHandlerFunc buildMultipartHandler(MediaIOTask_MjpegStream *sink);

        private:
                Error executeCmd(MediaIOCommandOpen &cmd) override;
                Error executeCmd(MediaIOCommandClose &cmd) override;
                Error executeCmd(MediaIOCommandWrite &cmd) override;
                Error executeCmd(MediaIOCommandStats &cmd) override;
                Error proposeInput(const MediaDesc &offered,
                                   MediaDesc *preferred) const override;

                /// @brief Encodes @p frame's first uncompressed video payload.
                Error encodeFrame(const Frame &frame, Buffer::Ptr *out, TimeStamp *ts);

                /// @brief Snapshots the most recent ring entry without holding the lock.
                bool latestRingEntry(Buffer::Ptr *out, TimeStamp *outTs) const;

                /// @brief Pushes @p jpeg/@p ts into the ring + dispatches subscribers.
                void publishEncoded(const Buffer::Ptr &jpeg, const TimeStamp &ts);

                // ---- Resolved configuration (latched at open time) ----
                FrameRate _maxRate;
                Duration  _period;
                int       _quality      = 80;
                int       _ringDepth    = 1;
                bool      _isOpen       = false;

                // ---- Encoder strand state ----
                /// Lazily-constructed JPEG encoder.  Owned by this task.
                UniquePtr<VideoEncoder> _encoder;
                /// Wallclock anchor of the previously-encoded frame —
                /// frames arriving before @c _lastEncoded + @c _period
                /// are dropped by the rate gate.
                TimeStamp _lastEncoded;
                bool      _hasLastEncoded = false;
                int64_t   _frameSerial    = 0;

                // ---- Ring + subscribers (mutex-guarded) ----
                struct RingEntry {
                        Buffer::Ptr     jpeg;       ///< Shared, encode-once JPEG buffer.
                        TimeStamp       timestamp;
                };

                mutable Mutex                    _stateMutex;
                List<RingEntry>                  _ring;
                Map<int, MjpegStreamSubscriber*> _subscribers;
                int                              _nextSubscriberId = 1;
                MjpegStreamSnapshot              _stats;
                /// @brief Mirrors @c _isOpen but mutex-guarded so off-strand
                ///        callers (e.g. an HTTP route handler) can read it
                ///        safely without taking the strand lock.
                bool                             _isStreaming   = false;
};

PROMEKI_NAMESPACE_END
