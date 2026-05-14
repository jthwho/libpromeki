/**
 * @file      srtserver.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once

#include <promeki/function.h>
#include <promeki/objectbase.h>
#include <promeki/error.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/srtsocket.h>

#include <functional>

PROMEKI_NAMESPACE_BEGIN

/**
 * @brief Listener-side SRT endpoint that accepts incoming connections.
 * @ingroup network
 *
 * SrtServer is the SRT analogue of @ref TcpServer.  It owns one
 * listener @c SRTSOCKET, binds it to a local address and waits for
 * caller-side handshakes.  Successful handshakes produce fresh
 * @ref SrtSocket instances via @ref accept.  Configuration that has
 * to be in place before the handshake (latency, passphrase,
 * encryption key length, max bandwidth) is set on the server and
 * inherited by accepted sockets — SRT itself does not propagate
 * post-listen option changes.
 *
 * @par Thread safety
 * Inherits @ref ObjectBase — thread-affine.  A single SrtServer may
 * only be used from the thread that created it.
 *
 * @par Example
 * @code
 * SrtServer server;
 * server.setLatency(120);
 * server.setPassphrase(String("very-secret-key"));
 * server.listen(SocketAddress::any(4200));
 * SrtSocket::UPtr client = server.accept(5000);  // 5 s wait
 * @endcode
 */
class SrtServer : public ObjectBase {
                PROMEKI_OBJECT(SrtServer, ObjectBase)
        public:
                /** @brief Default backlog if @ref listen is called without one. */
                static constexpr int DefaultBacklog = 50;

                /**
                 * @brief Constructs a server with no listener socket.
                 * @param parent Optional parent ObjectBase.
                 */
                SrtServer(ObjectBase *parent = nullptr);

                /** @brief Destructor. Closes the listener if open. */
                ~SrtServer() override;

                /**
                 * @brief Binds and starts listening on @p address.
                 *
                 * The listener socket is opened on demand.  Configuration
                 * (latency, passphrase, key length, max bandwidth) must
                 * be set *before* this call — SRT will refuse changes
                 * once the handshake is in flight.
                 *
                 * @param address Local address (use @c SocketAddress::any
                 *                for INADDR_ANY).
                 * @param backlog Pending-connection queue depth.
                 * @return @ref Error::Ok on success, or a system error.
                 */
                Error listen(const SocketAddress &address, int backlog = DefaultBacklog);

                /** @brief Stops listening and closes the listener socket. */
                void close();

                /** @brief Returns true while the listener is active. */
                bool isListening() const { return _listening; }

                /** @brief Returns the bound address (after @ref listen). */
                SocketAddress serverAddress() const { return _address; }

                /**
                 * @brief Accepts the next pending connection.
                 *
                 * Blocks until a handshake completes or @p timeoutMs
                 * elapses.  In non-blocking mode (@ref setNonBlocking)
                 * returns immediately — the caller drives readiness via
                 * @c srt_epoll on the listener handle.
                 *
                 * @param timeoutMs 0 = wait indefinitely.
                 * @return Owning pointer to the accepted SrtSocket on
                 *         success, or @c nullptr on timeout / error.
                 *         Inspect @ref lastError for diagnostics.
                 */
                SrtSocket::UPtr accept(unsigned int timeoutMs = 0);

                /**
                 * @brief Returns the listener handle (an @c SRTSOCKET).
                 *
                 * Exposed so callers driving @c srt_epoll directly can
                 * register the listener for accept-readiness.  Returns
                 * @ref SrtSocket::InvalidHandle when not listening.
                 */
                int handle() const { return _sock; }

                /**
                 * @brief Sets the listener socket to (non-)blocking mode.
                 *
                 * Required for @ref accept to integrate with an
                 * @ref EventLoop.
                 *
                 * @param enable True for non-blocking, false for blocking.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setNonBlocking(bool enable);

                // ---- Pre-listen configuration ----

                /** @copydoc SrtSocket::setLatency */
                Error setLatency(int ms);

                /** @copydoc SrtSocket::setPassphrase */
                Error setPassphrase(const String &passphrase);

                /** @copydoc SrtSocket::setEncryptionKeyLength */
                Error setEncryptionKeyLength(int bytes);

                /** @copydoc SrtSocket::setMaxBandwidth */
                Error setMaxBandwidth(int64_t bytesPerSec);

                /** @copydoc SrtSocket::setPayloadSize */
                Error setPayloadSize(int bytes);

                /**
                 * @brief Sets a kernel SO_REUSEADDR-style behaviour on the listener.
                 *
                 * Maps to @c SRTO_REUSEADDR.  Useful when restarting a
                 * listener on the same port without waiting for the
                 * kernel TIME_WAIT timeout to drain.
                 *
                 * @param enable True to enable reuse.
                 * @return @ref Error::Ok or a system error.
                 */
                Error setReuseAddress(bool enable);

                /**
                 * @brief Pre-accept decision callback signature.
                 *
                 * Called by the listener after the incoming peer's
                 * stream-id has been parsed but before the handshake
                 * completes.  Return @c true to allow the connection
                 * (it then surfaces from @ref accept), or @c false to
                 * reject it — SRT will tear it down with @c ECONNREJ
                 * before @ref accept ever sees it.
                 *
                 * @param streamId The peer's @c SRTO_STREAMID, possibly
                 *                 empty.
                 * @param peer     The peer's resolved address.
                 */
                using ListenCallback = Function<bool(const String &streamId, const SocketAddress &peer)>;

                /**
                 * @brief Installs a pre-accept decision callback.
                 *
                 * Wraps @c srt_listen_callback.  Must be called before
                 * @ref listen — installing it on an already-listening
                 * server is a programmer error.  The callback runs
                 * inside libsrt's accept thread, so it must be
                 * non-blocking and thread-safe.
                 *
                 * @param cb The callback (or empty to clear).
                 * @return @ref Error::Ok on success, or
                 *         @ref Error::AlreadyOpen if @ref listen has
                 *         already been called.
                 */
                Error setListenCallback(ListenCallback cb);

                /** @brief Returns the human-readable text of the last SRT error. */
                String lastError() const { return _lastError; }

                /** @brief Emitted after a successful @ref accept. @signal */
                PROMEKI_SIGNAL(newConnection);

                /**
                 * @internal
                 * @brief Invokes the registered listen callback.
                 *
                 * Public-but-unstable seam used by the C bridge that
                 * libsrt calls back into.  Not part of the SrtServer
                 * public API — callers should use
                 * @ref setListenCallback instead.
                 */
                static bool dispatchListenCallback(SrtServer *server, const String &streamId,
                                                   const SocketAddress &peer);

        private:
                int            _sock = SrtSocket::InvalidHandle;
                bool           _listening = false;
                SocketAddress  _address;
                String         _passphrase;
                String         _lastError;
                int            _latencyMs = 120;
                int            _payloadSize = 0;
                int            _pbKeyLen = 0;
                int64_t        _maxBw = 0;
                bool           _reuseAddr = true;
                ListenCallback _listenCb;

                /**
                 * @brief Lazily creates the listener handle and applies cached options.
                 *
                 * Called from @ref listen.  All option setters above
                 * just stash the value when the handle does not yet
                 * exist; this routine plays them back to libsrt.
                 */
                Error openAndConfigure();

                /**
                 * @brief Captures the most recent libsrt error string.
                 */
                void captureLastError();
};

PROMEKI_NAMESPACE_END
