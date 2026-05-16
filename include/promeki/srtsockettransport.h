/**
 * @file      srtsockettransport.h
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#pragma once


#include <promeki/config.h>
#if PROMEKI_ENABLE_SRT
#include <promeki/namespace.h>
#include <promeki/packettransport.h>
#include <promeki/socketaddress.h>
#include <promeki/string.h>
#include <promeki/srtsocket.h>

PROMEKI_NAMESPACE_BEGIN

class SrtServer;

/**
 * @brief SRT-backed implementation of @ref PacketTransport.
 * @ingroup network
 *
 * Wraps a single connected @ref SrtSocket so that higher-level
 * packet protocols (notably @ref RtpSession) can ride over a
 * reliable, low-latency SRT connection without changing their own
 * code paths.  Each instance owns one connection — either dialed
 * out in @ref Caller mode or accepted from a listener in
 * @ref Listener mode.  Multi-peer fan-out is not in scope; create
 * one transport per connection.
 *
 * @par Mode summary
 *
 *  - @b Caller: @ref open creates an SrtSocket, applies cached
 *    options, dials the configured peer, and returns once the
 *    handshake completes.
 *  - @b Listener: @ref open creates a private @ref SrtServer,
 *    binds and listens on the configured local address, then
 *    blocks @ref open until exactly one peer connects.  Subsequent
 *    incoming connections are rejected for the lifetime of this
 *    transport — use a dedicated listener manager for multi-stream
 *    routing.
 *  - @b Rendezvous: @ref open creates a fresh SrtSocket, sets the
 *    rendezvous flag, binds to the local address and dials the
 *    peer.  Both endpoints run identical sequences and meet in the
 *    middle.  Useful when neither peer can serve as a public
 *    listener (NAT-traversal scenarios with port-prediction).
 *
 * @par Datagram semantics
 *
 * SRT in live mode is message-oriented; @ref sendPacket maps to
 * one @ref SrtSocket::write call (one SRT message), and
 * @ref receivePacket maps to one @ref SrtSocket::read.  The
 * @c dest argument on @ref sendPacket is ignored — SRT only knows
 * about the single connected peer — but is preserved on the API
 * surface so RTP / future RTCP code can pass through unchanged.
 *
 * @par Thread safety
 * Inherits @ref PacketTransport — a single instance must only be
 * used from one thread at a time.
 *
 * @par Example (caller)
 * @code
 * SrtSocketTransport tx(SrtSocketTransport::Caller);
 * tx.setPeerAddress(SocketAddress(Ipv4Address(192, 168, 1, 10), 4200));
 * tx.setLatency(120);
 * tx.setPassphrase(String("secret-key-12345"));
 * Error err = tx.open();    // dials and returns once handshaken
 * tx.sendPacket(payload, sz, SocketAddress());  // dest ignored
 * @endcode
 */
class SrtSocketTransport : public PacketTransport {
        public:
                /** @brief Unique-ownership pointer to a SrtSocketTransport. */
                using UPtr = UniquePtr<SrtSocketTransport>;

                /** @brief Connection role for the transport. */
                enum Mode {
                        Caller,    ///< Dial outwards to a remote SRT listener.
                        Listener,  ///< Listen and accept exactly one peer.
                        Rendezvous ///< Peer-to-peer mutual handshake (both ends).
                };

                /**
                 * @brief Constructs a fresh transport in the requested role.
                 * @param mode @ref Caller or @ref Listener.
                 */
                explicit SrtSocketTransport(Mode mode = Caller);

                /** @brief Destructor. Closes the transport if open. */
                ~SrtSocketTransport() override;

                /** @brief Returns the configured role. */
                Mode mode() const { return _mode; }

                /**
                 * @brief Sets the local bind address (caller + listener).
                 *
                 * In @ref Caller mode this is the source address used
                 * during the SRT handshake (port 0 = OS-assigned).  In
                 * @ref Listener mode this is where @ref open binds and
                 * listens.
                 *
                 * @param addr Local address.
                 */
                void setLocalAddress(const SocketAddress &addr) { _localAddress = addr; }

                /** @brief Returns the configured local address. */
                const SocketAddress &localAddress() const { return _localAddress; }

                /**
                 * @brief Sets the remote peer address (caller mode).
                 *
                 * In @ref Listener mode this is ignored at @ref open
                 * time and updated to reflect the actual peer once
                 * accept completes.
                 *
                 * @param addr Remote SRT listener address.
                 */
                void setPeerAddress(const SocketAddress &addr) { _peerAddress = addr; }

                /** @brief Returns the (configured or accepted) peer address. */
                const SocketAddress &peerAddress() const { return _peerAddress; }

                /**
                 * @brief Sets the accept-wait timeout for listener mode.
                 * @param ms 0 = wait forever (default), otherwise milliseconds.
                 */
                void setAcceptTimeoutMs(unsigned int ms) { _acceptTimeoutMs = ms; }

                /** @brief Returns the accept-wait timeout. */
                unsigned int acceptTimeoutMs() const { return _acceptTimeoutMs; }

                /** @copydoc SrtSocket::setLatency */
                Error setLatency(int ms) {
                        if (ms < 0 || ms > 60000) return Error::Invalid;
                        _latencyMs = ms;
                        return Error::Ok;
                }
                int latency() const { return _latencyMs; }

                /** @copydoc SrtSocket::setPassphrase */
                Error setPassphrase(const String &passphrase) {
                        if (!passphrase.isEmpty() && (passphrase.byteCount() < 10 || passphrase.byteCount() > 79))
                                return Error::Invalid;
                        _passphrase = passphrase;
                        return Error::Ok;
                }

                /** @copydoc SrtSocket::setEncryptionKeyLength */
                Error setEncryptionKeyLength(int bytes) {
                        if (bytes != 0 && bytes != 16 && bytes != 24 && bytes != 32) return Error::Invalid;
                        _pbKeyLen = bytes;
                        return Error::Ok;
                }

                /** @copydoc SrtSocket::setStreamId */
                Error setStreamId(const String &id) {
                        if (id.byteCount() > 512) return Error::Invalid;
                        _streamId = id;
                        return Error::Ok;
                }
                const String &streamId() const { return _streamId; }

                /** @copydoc SrtSocket::setPayloadSize */
                Error setPayloadSize(int bytes) {
                        if (bytes < 0 || bytes > 1456) return Error::Invalid;
                        _payloadSize = bytes;
                        return Error::Ok;
                }

                /**
                 * @brief Buffers the max-bandwidth ceiling for the next @ref open.
                 *
                 * Forwarded to @ref SrtSocket::setMaxBandwidth on the
                 * underlying socket as part of the connect / accept
                 * sequence.  Cannot fail at the transport layer — the
                 * value is just stashed until the socket exists.
                 *
                 * @param bytesPerSec 0 = unlimited, -1 = relative-to-input.
                 */
                void setMaxBandwidth(int64_t bytesPerSec) { _maxBw = bytesPerSec; }

                /**
                 * @brief Returns the underlying SrtSocket (valid only when open).
                 *
                 * Exposed so callers that want to inspect SRT-specific
                 * state (statistics, key-state) without going through
                 * the PacketTransport surface can do so directly.
                 */
                SrtSocket *socket() const { return _socket.get(); }

                /**
                 * @brief Samples the underlying SrtSocket's stats.
                 *
                 * Convenience wrapper around @ref SrtSocket::stats —
                 * returns a zeroed struct when the transport is not
                 * open.
                 *
                 * @param resetWindowed Forwarded to libsrt.
                 */
                SrtSocket::Stats stats(bool resetWindowed = false) const {
                        if (!_socket) return SrtSocket::Stats{};
                        return _socket->stats(resetWindowed);
                }

                /** @copydoc PacketTransport::open() */
                Error open() override;

                /** @copydoc PacketTransport::close() */
                void close() override;

                /** @copydoc PacketTransport::isOpen() */
                bool isOpen() const override;

                /** @copydoc PacketTransport::sendPacket() */
                ssize_t sendPacket(const void *data, size_t size, const SocketAddress &dest) override;

                /** @copydoc PacketTransport::sendPackets() */
                int sendPackets(const DatagramList &datagrams) override;

                /** @copydoc PacketTransport::receivePacket() */
                ssize_t receivePacket(void *data, size_t maxSize, SocketAddress *sender = nullptr) override;

        private:
                Mode             _mode = Caller;
                SrtSocket::UPtr  _socket;
                UniquePtr<SrtServer> _listener;
                SocketAddress    _localAddress;
                SocketAddress    _peerAddress;
                String           _passphrase;
                String           _streamId;
                int              _latencyMs = 120;
                int              _payloadSize = 0;
                int              _pbKeyLen = 0;
                int64_t          _maxBw = 0;
                unsigned int     _acceptTimeoutMs = 0;

                /**
                 * @brief Plays cached config knobs back onto an SrtSocket.
                 *
                 * Used in caller mode (the freshly created socket needs
                 * options before connectToHost) and in the seam where
                 * the transport adopts a server-accepted SrtSocket.
                 */
                Error applySocketOptions(SrtSocket &sock);
};

PROMEKI_NAMESPACE_END

#endif // PROMEKI_ENABLE_SRT
