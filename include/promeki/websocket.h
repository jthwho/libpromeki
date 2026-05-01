/**
 * @file      websocket.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <functional>
#include <promeki/config.h> // PROMEKI_ENABLE_TLS
#include <promeki/namespace.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/buffer.h>
#include <promeki/string.h>
#include <promeki/url.h>
#include <promeki/list.h>
#include <promeki/uniqueptr.h>
#include <promeki/sslcontext.h>

PROMEKI_NAMESPACE_BEGIN

class TcpSocket;
class EventLoop;
class HttpRequest;
class HttpResponse;

/**
 * @brief RFC 6455 WebSocket endpoint, client- or server-side.
 * @ingroup network
 *
 * @ref WebSocket implements the framing and connection-management
 * half of the WebSocket protocol on top of @ref TcpSocket
 * (or @ref SslSocket for @c wss://).  The HTTP @em upgrade half is
 * shared with the HTTP stack: client sockets perform the upgrade
 * inside @ref connectToUrl; server sockets are produced by
 * @ref HttpServer::routeWebSocket once the @c HTTP/1.1 101
 * @c Switching Protocols handshake has completed.
 *
 * @par Thread Safety
 * Inherits @ref ObjectBase: thread-affine.  Every WebSocket lives
 * on exactly one @ref EventLoop — the loop that's
 * @ref EventLoop::current at construction time.  All frame
 * parsing, signal emission, and write pumping happens on that
 * loop's thread.  Cross-thread callers should connect to the
 * @ref textMessageReceived / @ref binaryMessageReceived signals;
 * the standard signal-dispatch machinery moves the call to the
 * receiver's loop.
 *
 * @par Usage (client)
 * @code
 * WebSocket *ws = new WebSocket();
 * ws->connectedSignal.connect([ws]() {
 *     ws->sendTextMessage("hello");
 * });
 * ws->textMessageReceivedSignal.connect([](String msg) {
 *     promekiInfo("recv: %s", msg.cstr());
 * });
 * ws->connectToUrl("ws://localhost:9000/echo");
 * @endcode
 *
 * @par Usage (server)
 * @code
 * server.routeWebSocket("/echo",
 *     [](WebSocket *ws, const HttpRequest &) {
 *         ws->textMessageReceivedSignal.connect([ws](String msg) {
 *             ws->sendTextMessage(msg);   // echo
 *         });
 *     });
 * @endcode
 *
 * @par Limitations
 * v1 implements text + binary messages, ping / pong, and the
 * close handshake.  Per-message-deflate and other extensions are
 * not negotiated.  Subprotocols can be set on the client and are
 * echoed verbatim by the server route helper.
 */
class WebSocket : public ObjectBase {
                PROMEKI_OBJECT(WebSocket, ObjectBase)
        public:
                /** @brief Convenience list type. */
                using List = promeki::List<WebSocket *>;

                /** @brief High-level connection state. */
                enum State {
                        Disconnected, ///< No socket is attached.
                        Connecting,   ///< TCP / TLS / HTTP upgrade in flight (client side).
                        Connected,    ///< Frames may flow in either direction.
                        Closing       ///< Local or remote close in progress.
                };

                /** @brief Standard close codes (RFC 6455 §7.4). */
                enum CloseCode : uint16_t {
                        CloseNormal = 1000,
                        CloseGoingAway = 1001,
                        CloseProtocolError = 1002,
                        CloseUnsupportedData = 1003,
                        CloseNoStatus = 1005, ///< Reserved; never sent
                        CloseAbnormal = 1006, ///< Reserved; never sent
                        CloseInvalidPayload = 1007,
                        ClosePolicyViolation = 1008,
                        CloseMessageTooBig = 1009,
                        CloseMissingExtension = 1010,
                        CloseInternalError = 1011
                };

                /** @brief Default upper bound on a single message in bytes. */
                static constexpr int64_t DefaultMaxMessageBytes = 16 * 1024 * 1024;

                /** @brief Constructs a disconnected WebSocket. */
                explicit WebSocket(ObjectBase *parent = nullptr);

                /** @brief Destructor.  Closes the connection if it is still open. */
                ~WebSocket() override;

                // ----------------------------------------------------
                // Client-side connect
                // ----------------------------------------------------

                /**
                 * @brief Connects to a @c ws:// or @c wss:// URL.
                 *
                 * Resolves the host, opens an underlying socket
                 * (@ref TcpSocket for @c ws, @ref SslSocket for
                 * @c wss), and drives the HTTP upgrade.  The call
                 * itself returns immediately; @ref connectedSignal
                 * fires when the upgrade completes successfully and
                 * @ref errorOccurredSignal fires on failure.
                 *
                 * @param url Target URL.
                 * @return @ref Error::Ok if the connect was initiated
                 *         successfully (note: not "connected"),
                 *         @ref Error::Invalid for a malformed URL,
                 *         @ref Error::HostNotFound for DNS failure,
                 *         @ref Error::AlreadyOpen if a connection is
                 *         already in progress.
                 */
                Error connectToUrl(const String &url);

                /**
                 * @brief Sets a comma-separated list of subprotocols to
                 *        request via @c Sec-WebSocket-Protocol.
                 *
                 * The accepted subprotocol (the value the server echoes
                 * back) is exposed via @ref negotiatedSubprotocol after
                 * @ref connectedSignal fires.
                 */
                void setRequestedSubprotocols(const String &csv) { _requestedSubprotocols = csv; }

                /** @brief Returns the requested subprotocols list. */
                const String &requestedSubprotocols() const { return _requestedSubprotocols; }

                /**
                 * @brief Adds an extra HTTP header to the upgrade request.
                 *
                 * Useful for @c Authorization headers and similar.
                 */
                void setRequestHeader(const String &name, const String &value);

                /**
                 * @brief Attaches an SslContext used for @c wss:// connects.
                 *
                 * Without one, a default-constructed @ref SslContext
                 * (no CA chain → effectively no peer verification) is
                 * created lazily.  Mirrors @ref HttpClient.
                 *
                 * Always available regardless of @c PROMEKI_ENABLE_TLS;
                 * in a TLS-disabled build the context is stored but
                 * its operations are inert (see @ref SslContext) and
                 * an actual @c wss connect is later rejected with
                 * @ref Error::NotImplemented.
                 */
                void setSslContext(SslContext::Ptr ctx) { _sslContext = std::move(ctx); }

                /** @brief Returns the attached SslContext, or null. */
                SslContext::Ptr sslContext() const { return _sslContext; }

                /**
                 * @brief Reports whether this build can speak TLS.
                 *
                 * Forwards to @ref SslContext::hasTlsSupport.
                 */
                static bool hasTlsSupport() { return SslContext::hasTlsSupport(); }

                // ----------------------------------------------------
                // Server-side adoption (used by HttpServer::routeWebSocket)
                // ----------------------------------------------------

                /**
                 * @brief Adopts an already-upgraded TCP socket.
                 *
                 * Used by @ref HttpServer::routeWebSocket after the
                 * HTTP layer has sent the @c 101 response.  Takes
                 * ownership of @p socket; the WebSocket re-parents it
                 * onto itself.
                 *
                 * @param socket Open, non-blocking socket whose first
                 *               byte (if any) will be the start of a
                 *               WebSocket frame.
                 */
                void adoptUpgradedSocket(TcpSocket *socket);

                // ----------------------------------------------------
                // Application API
                // ----------------------------------------------------

                /** @brief Sends a UTF-8 text message. */
                Error sendTextMessage(const String &message);

                /** @brief Sends a binary message. */
                Error sendBinaryMessage(const Buffer &message);

                /** @brief Sends an unsolicited ping with optional payload. */
                Error ping(const Buffer &payload = Buffer());

                /**
                 * @brief Initiates the WebSocket close handshake.
                 *
                 * Sends a close frame with the supplied code + reason,
                 * then waits for the peer's close frame before
                 * tearing the socket down.  No-op if the socket is
                 * already in @ref Closing or @ref Disconnected.
                 */
                void disconnect(uint16_t code = CloseNormal, const String &reason = String());

                /**
                 * @brief Force-closes the socket without a close handshake.
                 *
                 * Equivalent to @ref disconnect when the peer is
                 * already gone; otherwise prefer the polite path.
                 */
                void abort();

                /** @brief Returns the current connection state. */
                State state() const { return _state; }

                /** @brief Convenience: @ref state == @ref Connected. */
                bool isConnected() const { return _state == Connected; }

                /** @brief Returns the negotiated subprotocol, or empty. */
                const String &negotiatedSubprotocol() const { return _negotiatedSubprotocol; }

                /**
                 * @brief Sets the upper bound on a single message.
                 *
                 * Messages exceeding this size cause the connection
                 * to close with @ref CloseMessageTooBig.  Pass
                 * @c -1 to disable the cap.
                 */
                void setMaxMessageBytes(int64_t bytes) { _maxMessageBytes = bytes; }

                /** @brief Emitted on successful handshake. @signal */
                PROMEKI_SIGNAL(connected);

                /** @brief Emitted after the socket closes (clean or abrupt). @signal */
                PROMEKI_SIGNAL(disconnected);

                /** @brief Emitted on each incoming text message. @signal */
                PROMEKI_SIGNAL(textMessageReceived, String);

                /** @brief Emitted on each incoming binary message. @signal */
                PROMEKI_SIGNAL(binaryMessageReceived, Buffer);

                /** @brief Emitted on each incoming pong. @signal */
                PROMEKI_SIGNAL(pongReceived, Buffer);

                /** @brief Emitted on transport / framing / handshake errors. @signal */
                PROMEKI_SIGNAL(errorOccurred, Error);

        private:
                struct Impl;
                UniquePtr<Impl> _impl;

                EventLoop *_loop = nullptr;
                TcpSocket *_socket = nullptr; ///< Owned, parented onto this.
                int        _ioHandle = -1;
                State      _state = Disconnected;
                bool       _isClient = false; ///< True when we sent the upgrade

                Url     _url;
                String  _requestedSubprotocols;
                String  _negotiatedSubprotocol;
                String  _expectedAccept; ///< Sec-WebSocket-Accept we expect from server
                int64_t _maxMessageBytes = DefaultMaxMessageBytes;
                bool    _useTls = false;
                bool    _handshakeDone = false; ///< TLS handshake done (wss://)

                SslContext::Ptr _sslContext;

                void onIoReady(int fd, uint32_t events);
                void readSome();
                void pumpWrite();
                void registerIo(uint32_t mask);

                // Client-side handshake.
                void writeClientHandshake();
                void parseClientHandshakeResponse();

                // Frame helpers.
                Error sendFrame(uint8_t opcode, const void *data, size_t len, bool fin = true);
                void  processIncomingBytes();
                void  handleControlFrame(uint8_t opcode, Buffer payload);
                void  handleDataMessage(uint8_t opcode, Buffer payload);
                void  enqueueClose(uint16_t code, const String &reason);
                void  finalizeClose(Error err);

        public:
                // Internals exposed only so HttpServer::routeWebSocket
                // can build the proper Sec-WebSocket-Accept.  Not
                // intended for application use.
                static String computeAcceptValue(const String &clientKey);
};

PROMEKI_NAMESPACE_END
