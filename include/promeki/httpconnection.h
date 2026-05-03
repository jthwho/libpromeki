/**
 * @file      httpconnection.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/buffer.h>
#include <promeki/string.h>
#include <promeki/uniqueptr.h>
#include <promeki/list.h>
#include <promeki/httprequest.h>
#include <promeki/httpresponse.h>

PROMEKI_NAMESPACE_BEGIN

class TcpSocket;
class EventLoop;

/**
 * @brief Per-connection HTTP/1.x request parser, response serializer,
 *        and socket pump.
 * @ingroup network
 *
 * One @ref HttpConnection wraps one accepted @ref TcpSocket.  The
 * connection is the bridge between byte-oriented I/O and the
 * higher-level @ref HttpRequest / @ref HttpResponse types: it feeds
 * received bytes into a vendored llhttp parser, builds a request
 * object as the parser fires its callbacks, hands the request to a
 * caller-supplied dispatcher when the message is complete, then
 * serializes the produced @ref HttpResponse back out.
 *
 * Lifecycle expectations:
 *  - Construct with the accepted TcpSocket (ownership is transferred).
 *  - Call @ref start to register the socket fd with the connection's
 *    @ref EventLoop and begin reading.
 *  - The connection emits @c %requestReceived / @c %responseSent /
 *    @c %closed signals on the loop's thread; the dispatcher
 *    callback installed via @ref setRequestHandler is invoked on the
 *    loop thread between request completion and response start.
 *
 * Keep-alive: HTTP/1.1 keeps the connection alive by default; HTTP/1.0
 * closes by default.  Either side's @c Connection: close header forces
 * close after the in-flight response is sent.
 *
 * Streamed bodies: when the response carries an
 * @ref HttpResponse::bodyStream, the connection drains the device
 * incrementally as the socket becomes writable, falling back to
 * chunked transfer-encoding when the device's length is unknown.
 *
 * @note @c HttpConnection is internal infrastructure.  Application
 *       code should use @ref HttpServer; the connection is exposed
 *       as a public type only so out-of-tree integrations and tests
 *       can drive it directly when needed.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase &mdash; thread-affine.  All connection state
 * mutates on the EventLoop thread that owns the underlying
 * @ref TcpSocket.  Cross-thread interaction goes through the
 * loop's @c postCallable.
 */
class HttpConnection : public ObjectBase {
                PROMEKI_OBJECT(HttpConnection, ObjectBase)
        public:
                /** @brief Convenience list type. */
                using List = ::promeki::List<HttpConnection *>;

                /** @brief Default per-connection idle timeout in milliseconds. */
                static constexpr unsigned int DefaultIdleTimeoutMs = 60'000;

                /** @brief Default upper limit on a single request body, in bytes. */
                static constexpr int64_t DefaultMaxBodyBytes = 64 * 1024 * 1024;

                /**
                 * @brief Caller-supplied dispatcher for completed requests.
                 *
                 * Invoked on the connection's EventLoop thread once a
                 * request has been fully parsed.  The handler must
                 * populate @p response synchronously; for asynchronous
                 * handlers, copy the request and call @ref postResponse
                 * later from the same loop.
                 */
                using RequestHandler = std::function<void(HttpRequest &request, HttpResponse &response)>;

                /**
                 * @brief Constructs a connection wrapping @p socket.
                 *
                 * @param socket Accepted TcpSocket (or @ref SslSocket for
                 *               TLS-terminated server mode); ownership
                 *               is taken.  Must already be open.  The
                 *               connection re-parents @p socket onto
                 *               itself so its lifetime matches the
                 *               connection.
                 * @param parent Parent ObjectBase (typically the @ref HttpServer).
                 */
                HttpConnection(TcpSocket *socket, ObjectBase *parent = nullptr);

                /**
                 * @brief Tells the connection to drive a server-side TLS
                 *        handshake before reading the first request.
                 *
                 * Called by @ref HttpServer immediately after construction
                 * when the wrapped socket is an @ref SslSocket configured
                 * with a server certificate.  The handshake runs on the
                 * loop's I/O readiness events; the parser only starts
                 * accepting bytes once the handshake completes
                 * successfully.
                 *
                 * Must be called before @ref start.  No-op when
                 * PROMEKI_ENABLE_TLS is off.
                 */
                void setNeedsServerHandshake();

                /** @brief Destructor.  Closes the socket if still open. */
                ~HttpConnection() override;

                /**
                 * @brief Installs the request dispatcher.
                 *
                 * The dispatcher is what hands off the parsed
                 * @ref HttpRequest to the application's routing logic
                 * (typically @ref HttpServer::router).  Without a
                 * dispatcher, completed requests immediately respond
                 * with @c HttpStatus::NotImplemented.
                 */
                void setRequestHandler(RequestHandler handler);

                /**
                 * @brief Begins reading from the socket.
                 *
                 * Registers the socket fd with the EventLoop captured
                 * at construction time.  Idempotent: subsequent calls
                 * are no-ops.
                 */
                Error start();

                /**
                 * @brief Closes the connection.
                 *
                 * Cancels any in-flight write, unregisters from the
                 * EventLoop, closes the socket, and emits @c %closed.
                 */
                void close();

                /** @brief True between @ref start and @ref close. */
                bool isOpen() const { return _state != State::Closed; }

                /** @brief Peer address as reported by the underlying socket. */
                String peerAddress() const;

                // ----------------------------------------------------
                // Limits / tuning
                // ----------------------------------------------------

                /** @brief Sets the per-connection idle timeout in milliseconds. */
                void setIdleTimeoutMs(unsigned int ms) { _idleTimeoutMs = ms; }

                /**
                 * @brief Sets the maximum request body size in bytes.
                 *
                 * Connections that exceed the limit yield a 413 reply
                 * and close.  Use @c -1 to disable the cap.
                 */
                void setMaxBodyBytes(int64_t bytes) { _maxBodyBytes = bytes; }

                /**
                 * @brief Posts a response back through this connection.
                 *
                 * Used by handlers that defer their reply onto a
                 * worker thread: copy the request, do the work, then
                 * call this method from the connection's EventLoop
                 * thread (typically via @c postCallable).
                 *
                 * Safe to call exactly once per @c %requestReceived
                 * emission; calling more than once raises an error.
                 */
                Error postResponse(HttpResponse response);

                /** @brief Emitted on the loop thread when a request finishes parsing. @signal */
                PROMEKI_SIGNAL(requestReceived, HttpRequest);

                /** @brief Emitted after the response has finished writing. @signal */
                PROMEKI_SIGNAL(responseSent, HttpRequest, HttpResponse);

                /** @brief Emitted as the connection enters the @c Closed state. @signal */
                PROMEKI_SIGNAL(closed);

                /** @brief Emitted on parser / I/O / timeout errors. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                // High-level state machine.  We never re-pipeline more
                // than one in-flight request; once parsed we wait for
                // the dispatcher to populate a response, drain it,
                // then either resume reading (keep-alive) or close.
                enum class State {
                        Handshaking,      ///< Driving the TLS server handshake.
                        Reading,          ///< Awaiting / parsing a request.
                        AwaitingResponse, ///< Request done, dispatcher running.
                        Writing,          ///< Streaming a response.
                        Closing,          ///< Final flush in progress.
                        Closed
                };

                struct Impl; ///< Pimpl: hides llhttp_t headers.
                UniquePtr<Impl> _impl;

                TcpSocket *_socket = nullptr; ///< Owned, parented onto this.
                EventLoop *_loop = nullptr;
                int        _ioHandle = -1;
                int        _timerId = -1;
                State      _state = State::Reading;

                RequestHandler _handler;
                Buffer         _readBuf;    ///< Scratch read landing zone.
                Buffer         _writeQueue; ///< Pending bytes to send.
                size_t         _writeOffset = 0;

                HttpRequest      _pendingRequest;
                HttpRequest      _lastRequest; ///< Captured for responseSent signal.
                IODevice::Shared _streamSource;
                int64_t          _streamRemaining = -1;
                bool             _streamChunked = false;
                bool             _keepAlive = true;

                // Async-read parking: when the body IODevice returns
                // read()==0 with atEnd()==false, the pump unsubscribes
                // from IoWrite and waits on the device's readyRead
                // signal.  _streamReadyReadSlotId is the slot id from
                // Signal::connect — non-negative while parked.
                size_t _streamReadyReadSlotId = 0;
                bool   _streamReadyReadConnected = false;
                bool   _streamParked = false;

                // Protocol upgrade.  When the response carries an
                // upgrade hook (HttpResponse::upgradeHook()) and is a
                // 101 Switching Protocols, _pendingUpgradeHook is set
                // here and fires once the response finishes writing.
                HttpResponse::UpgradeHook _pendingUpgradeHook;

                unsigned int _idleTimeoutMs = DefaultIdleTimeoutMs;
                int64_t      _maxBodyBytes = DefaultMaxBodyBytes;
                int64_t      _bodyBytesSoFar = 0;
                bool         _needsServerHandshake = false;

                // Header-collection scratch.  llhttp emits header
                // fields and values as separate callbacks (and may
                // fragment a single field across multiple callbacks)
                // so we accumulate into these strings until both
                // sides are complete, then push into _pendingRequest.
                String _hdrField;
                String _hdrValue;
                bool   _hdrFieldComplete = false;
                bool   _hdrValueComplete = false;
                String _urlBuf;

                void onIoReady(int fd, uint32_t events);
                void onIdleTimeout();
                void readSome();
                void pumpWrite();
                void deliverRequest();
                void enqueueResponse(HttpResponse response);
                void scheduleStreamPump();
                void completeProtocolUpgrade();

                // Async-read parking: hook the stream's readyRead so
                // pumpWrite resumes when the producer pushes more
                // bytes; detach when the stream finishes or the
                // connection closes.  Both are no-ops if the stream
                // isn't a sequential async device.
                void attachStreamReadyRead();
                void detachStreamReadyRead();
                void onStreamReadyRead();

                void flushPendingHeaderPair();
                void resetForNextRequest();

                // llhttp callbacks (defined in cpp; bridge via parser->data).
                static int cbMessageBegin(void *parser);
                static int cbUrl(void *parser, const char *at, size_t len);
                static int cbHeaderField(void *parser, const char *at, size_t len);
                static int cbHeaderValue(void *parser, const char *at, size_t len);
                static int cbHeadersComplete(void *parser);
                static int cbBody(void *parser, const char *at, size_t len);
                static int cbMessageComplete(void *parser);
};

PROMEKI_NAMESPACE_END
