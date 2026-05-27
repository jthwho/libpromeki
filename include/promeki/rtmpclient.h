/**
 * @file      rtmpclient.h
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_NETWORK
#include <cstdint>

#include <promeki/atomic.h>
#include <promeki/duration.h>
#include <promeki/error.h>
#include <promeki/flvtag.h>
#include <promeki/list.h>
#include <promeki/metadata.h>
#include <promeki/mutex.h>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/queue.h>
#include <promeki/result.h>
#include <promeki/rtmpmessage.h>
#include <promeki/rtmpsession.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/url.h>

#if PROMEKI_ENABLE_TLS
#include <promeki/sslcontext.h>
#endif

PROMEKI_NAMESPACE_BEGIN

class TcpSocket;
class Thread;

/**
 * @brief Convenience wrapper around @ref TcpSocket / @ref SslSocket +
 *        @ref RtmpSession that owns a writer and a reader thread.
 * @ingroup network
 *
 * @c RtmpClient is the standalone-protocol entry point for callers
 * that want a fully-realized RTMP publisher or subscriber without
 * the MediaIO indirection.  Internally it:
 *
 *   - Parses an @c rtmp:// or @c rtmps:// URL into host / port /
 *     app / streamKey segments.
 *   - Opens a @ref TcpSocket (or @ref SslSocket for @c rtmps:),
 *     drives the handshake, and runs @c RtmpSession::connect.
 *   - Spins up a writer thread that drains a bounded
 *     @c Queue<RtmpMessage> onto the session.
 *   - Spins up a reader thread that pumps @c RtmpSession::readMessage
 *     and demuxes audio / video / metadata into per-kind queues for
 *     the caller to drain via @ref takeVideo / @ref takeAudio /
 *     @ref takeMetadata.
 *
 * The class is shaped so that an upstream @c RtmpMediaIO (Phase 5)
 * never has to touch the @ref RtmpSession directly — it instantiates
 * one @c RtmpClient and uses the high-level @ref sendVideo /
 * @ref sendAudio / @ref takeVideo / @ref takeAudio APIs.
 *
 * @par URL → app + streamKey split
 *
 * For a URL like @c rtmp://h/x/y/z/key, the conventional split is
 * "app = x/y/z, streamKey = key" — everything before the last @c /
 * is the app component carried in the @c connect AMF0 object; the
 * trailing segment is the stream identifier used by @c publish /
 * @c play.  Callers can override either side via @ref open's
 * @ref RtmpConnectOptions::app or by passing an explicit @p
 * streamKey argument to @ref publish / @ref play.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  The public API
 * is **thread-safe for the supported call topology**:
 *
 *   - @ref open / @ref close are called from the owning thread.
 *   - @ref publish / @ref play / @ref sendVideo / @ref sendAudio /
 *     @ref sendMetadata may be called from any thread; the bounded
 *     internal queues serialize the producer side.
 *   - @ref takeVideo / @ref takeAudio / @ref takeMetadata likewise.
 *
 * Concurrent @c open / @c close on the same instance is undefined.
 */
class RtmpClient : public ObjectBase {
                PROMEKI_OBJECT(RtmpClient, ObjectBase)
        public:
                /** @brief Default bound for the writer-side queue (matches the devplan's RtmpSendQueueDepth). */
                static constexpr int DefaultSendQueueDepth = 64;

                /** @brief Default bound for the receiver-side per-kind queues. */
                static constexpr int DefaultReadQueueDepth = 64;

                /** @brief Default TCP connect timeout. */
                static constexpr unsigned int DefaultConnectTimeoutMs = 10000;

                /**
                 * @brief Constructs an idle client.
                 *
                 * Call @ref open to parse a URL and bring the
                 * connection up.
                 */
                explicit RtmpClient(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Calls @ref close. */
                ~RtmpClient() override;

#if PROMEKI_ENABLE_TLS
                /**
                 * @brief Attaches a custom @ref SslContext used for
                 *        peer verification when the URL scheme is
                 *        @c rtmps:.
                 *
                 * Optional — a default-constructed @ref SslContext
                 * already auto-loads the system CA bundle, so
                 * @c rtmps:// against publicly-trusted servers
                 * works without any setup.  Configure a custom
                 * context when you need a private CA or to disable
                 * verification (@c setVerifyPeer(false)) for
                 * self-signed test servers.  Must be called before
                 * @ref open.
                 */
                void setSslContext(SslContext ctx) { _sslContext = std::move(ctx); }
#endif

                /**
                 * @brief Parses @p url, opens the right socket type,
                 *        and drives the handshake + connect-flow.
                 *
                 * Blocks until @ref RtmpSession::connect returns or
                 * @p timeoutMs elapses.  On success the writer and
                 * reader threads are running and subsequent
                 * @ref publish / @ref play / @ref sendVideo calls
                 * route through them.
                 */
                Error open(const Url &url,
                           const RtmpConnectOptions &opts = {},
                           unsigned int timeoutMs = DefaultConnectTimeoutMs);

                /**
                 * @brief Issues @c releaseStream + @c FCPublish +
                 *        @c createStream + @c publish in sequence,
                 *        blocking until @c NetStream.Publish.Start
                 *        arrives or @p timeoutMs elapses.
                 *
                 * @c streamKey defaults to whatever was parsed from
                 * the URL when @c open ran; pass a non-empty value
                 * here to override.
                 */
                Error publish(const String &streamKey = String(),
                              const String &mode = "live",
                              unsigned int timeoutMs = 5000);

                /**
                 * @brief Issues @c createStream + (optional
                 *        @c FCSubscribe) + @c play in sequence,
                 *        blocking until @c NetStream.Play.Start
                 *        arrives or @p timeoutMs elapses.
                 */
                Error play(const String &streamKey = String(),
                           unsigned int timeoutMs = 5000,
                           bool useFcSubscribe = false);

                /**
                 * @brief Packs @p tag as an FLV @c VIDEODATA payload
                 *        and pushes it onto the writer queue with
                 *        @p timestampMs.
                 *
                 * Returns @c Error::TryAgain when the queue is full
                 * (caller's responsibility: throttle upstream).
                 */
                Error sendVideo(const FlvVideoTag &tag, uint32_t timestampMs);

                /**
                 * @brief Packs @p tag as an FLV @c AUDIODATA payload
                 *        and pushes it onto the writer queue with
                 *        @p timestampMs.
                 */
                Error sendAudio(const FlvAudioTag &tag, uint32_t timestampMs);

                /**
                 * @brief Sends an @c onMetaData AMF0 script payload
                 *        derived from @p meta.
                 *
                 * v1 emits an empty AMF0 ecma-array as a placeholder
                 * — the metadata's parsed shape is wired through
                 * here once Phase 5's @c RtmpMediaIO consumer needs
                 * specific keys (`width`, `height`, `framerate`,
                 * etc.).  The transport mechanics are in place; the
                 * payload shape is parameterized.
                 */
                Error sendMetadata(const Metadata &meta, uint32_t timestampMs = 0);

                /**
                 * @brief Pops the next decoded FLV video tag from
                 *        the per-kind queue.
                 *
                 * @return @c Error::TryAgain when the queue is empty
                 *         within @p timeoutMs; the tag plus
                 *         @c Error::Ok on success.
                 */
                Result<FlvVideoTag> takeVideo(unsigned int timeoutMs);

                /**
                 * @brief Pops the next decoded FLV audio tag.
                 *        @c Error::TryAgain on empty within @p timeoutMs.
                 */
                Result<FlvAudioTag> takeAudio(unsigned int timeoutMs);

                /**
                 * @brief Pops the next received @c onMetaData payload.
                 *        @c Error::TryAgain on empty within @p timeoutMs.
                 */
                Result<Metadata>    takeMetadata(unsigned int timeoutMs);

                /**
                 * @brief Shuts the threads down + closes the socket.
                 *
                 * Safe to call multiple times.  Idempotent.
                 */
                Error close();

                /** @brief Returns the URL passed to @ref open. */
                const Url &url() const { return _url; }

                /** @brief Returns the app component parsed from the URL. */
                const String &app() const { return _app; }

                /** @brief Returns the stream-key component parsed from the URL. */
                const String &streamKey() const { return _streamKey; }

                /** @brief Returns the message-stream-id allocated by `createStream`. */
                uint32_t streamId() const { return _streamId.value(); }

                /** @brief Returns true once @ref open completed successfully and threads are running. */
                bool isOpen() const { return _open.value(); }

                /** @brief Returns the underlying session, or @c nullptr if not yet opened. */
                RtmpSession *session() { return _session.get(); }

                // ---- Stats ----

                /** @brief Cumulative bytes written by the writer thread. */
                int64_t bytesSent() const     { return _bytesSent.value(); }

                /** @brief Cumulative bytes read by the reader thread. */
                int64_t bytesReceived() const { return _bytesReceived.value(); }

                /** @brief Number of video messages handed off to the chunk layer. */
                int64_t videoMessagesSent() const { return _videoMessagesSent.value(); }

                /** @brief Number of audio messages handed off to the chunk layer. */
                int64_t audioMessagesSent() const { return _audioMessagesSent.value(); }

                /** @brief Number of video messages drained by the reader thread. */
                int64_t videoMessagesReceived() const { return _videoMessagesReceived.value(); }

                /** @brief Number of audio messages drained by the reader thread. */
                int64_t audioMessagesReceived() const { return _audioMessagesReceived.value(); }

                /**
                 * @brief Last RTT estimate from a paired @c PingRequest /
                 *        @c PingResponse, or zero before any sample.
                 *
                 * v1 returns a fixed zero — the RTT-sampling write
                 * path lands with Phase 5 (when the writer thread
                 * gains a periodic-keepalive emitter).  The accessor
                 * is in place so callers can program against a
                 * stable shape today.
                 */
                Duration rttEstimate() const { return Duration(); }

                // ---- Signals ----

                /** @brief Fires once @ref open succeeds. @signal */
                PROMEKI_SIGNAL(connected);

                /** @brief Fires when the connection terminates. @signal */
                PROMEKI_SIGNAL(disconnected, Error);

                /** @brief Fires once for every received @c onMetaData payload. @signal */
                PROMEKI_SIGNAL(metadataReceived, Metadata);

                /**
                 * @brief Splits @p url's path into @c app and
                 *        @c streamKey segments.
                 *
                 * When the path has two or more segments, everything
                 * before the last `/` is the app and the trailing
                 * segment is the stream key.  When the path has a
                 * single segment (e.g. @c rtmp://host/live2), the
                 * segment is the app and the stream key is empty —
                 * callers can supply the key out-of-band via
                 * @c MediaConfig::RtmpStreamKey or the
                 * @ref publish() / @ref play() arguments.  An empty
                 * path yields empty strings.
                 *
                 * Exposed as a public static so tests and callers can
                 * reason about how a URL maps to RTMP `app` /
                 * `streamKey` without instantiating a client.
                 */
                static void splitPath(const Url &url, String &app, String &streamKey);

        private:
                class WriterThread;
                class ReaderThread;

                void teardown(Error reason);
                void startMediaThreads();

                Url                          _url;
                String                       _app;
                String                       _streamKey;
                Atomic<uint32_t>             _streamId{0};
                Atomic<bool>                 _open{false};
                Atomic<bool>                 _stopping{false};

                UniquePtr<TcpSocket>         _socket;       ///< Owned TcpSocket / SslSocket (polymorphic via base ptr).
                UniquePtr<RtmpSession>       _session;
                UniquePtr<WriterThread>      _writer;
                UniquePtr<ReaderThread>      _reader;

#if PROMEKI_ENABLE_TLS
                SslContext              _sslContext;
                bool                         _isTls = false;
#endif

                Queue<RtmpMessage>           _writeQueue;
                Queue<FlvVideoTag>           _videoRxQueue;
                Queue<FlvAudioTag>           _audioRxQueue;
                Queue<Metadata>              _metadataRxQueue;

                Atomic<int64_t>              _bytesSent{0};
                Atomic<int64_t>              _bytesReceived{0};
                Atomic<int64_t>              _videoMessagesSent{0};
                Atomic<int64_t>              _audioMessagesSent{0};
                Atomic<int64_t>              _videoMessagesReceived{0};
                Atomic<int64_t>              _audioMessagesReceived{0};
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_NETWORK
