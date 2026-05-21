/**
 * @file      srtsockettransport.cpp
 * @copyright Howard Logic. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/srtsockettransport.h>
#include <promeki/srtserver.h>
#include <promeki/logger.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SrtSocketTransport);

SrtSocketTransport::SrtSocketTransport(Mode mode) : _mode(mode) {}

SrtSocketTransport::~SrtSocketTransport() {
        close();
}

Error SrtSocketTransport::applySocketOptions(SrtSocket &sock) {
        Error e;
        e = sock.setLatency(_latencyMs);
        if (e.isError()) return e;
        if (_payloadSize > 0) {
                e = sock.setPayloadSize(_payloadSize);
                if (e.isError()) return e;
        }
        if (!_passphrase.isEmpty()) {
                e = sock.setPassphrase(_passphrase);
                if (e.isError()) return e;
                if (_pbKeyLen != 0) {
                        e = sock.setEncryptionKeyLength(_pbKeyLen);
                        if (e.isError()) return e;
                }
        }
        if (!_streamId.isEmpty()) {
                e = sock.setStreamId(_streamId);
                if (e.isError()) return e;
        }
        if (_maxBw != 0) {
                e = sock.setMaxBandwidth(_maxBw);
                if (e.isError()) return e;
        }
        return Error::Ok;
}

Error SrtSocketTransport::open() {
        if (isOpen()) return Error::AlreadyOpen;
        if (_mode == Caller || _mode == Rendezvous) {
                _socket = SrtSocket::UPtr::create();
                if (_mode == Rendezvous) {
                        // Must be set before open()'s applyPreConnect
                        // pass so SRTO_RENDEZVOUS is in place when
                        // the handshake fires.  Stashing it on the
                        // SrtSocket pre-open is enough; the option
                        // setter buffers until the socket exists.
                        _socket->setRendezvous(true);
                }
                Error e = _socket->open(IODevice::ReadWrite);
                if (e.isError()) {
                        _socket.reset();
                        return e;
                }
                e = applySocketOptions(*_socket);
                if (e.isError()) {
                        _socket.reset();
                        return e;
                }
                // Rendezvous requires both ends to bind to a known
                // local address before connect; caller mode treats
                // bind as optional.
                if (_mode == Rendezvous && _localAddress.isNull()) {
                        promekiWarn("SrtSocketTransport::open Rendezvous mode requires a bound local address");
                        _socket.reset();
                        return Error::Invalid;
                }
                if (!_localAddress.isNull()) {
                        e = _socket->bind(_localAddress);
                        if (e.isError()) {
                                _socket.reset();
                                return e;
                        }
                }
                if (_peerAddress.isNull()) {
                        promekiWarn("SrtSocketTransport::open Caller/Rendezvous mode requires a peer address");
                        _socket.reset();
                        return Error::Invalid;
                }
                e = _socket->connectToHost(_peerAddress);
                if (e.isError()) {
                        _socket.reset();
                        return e;
                }
                return Error::Ok;
        }

        // Listener mode: stand up a private SrtServer, accept one
        // connection, retain that socket as our transport's pipe.
        _listener = UniquePtr<SrtServer>::create();
        _listener->setLatency(_latencyMs);
        if (_payloadSize > 0) _listener->setPayloadSize(_payloadSize);
        if (!_passphrase.isEmpty()) {
                _listener->setPassphrase(_passphrase);
                if (_pbKeyLen != 0) _listener->setEncryptionKeyLength(_pbKeyLen);
        }
        if (_maxBw != 0) _listener->setMaxBandwidth(_maxBw);

        if (_localAddress.isNull()) {
                promekiWarn("SrtSocketTransport::open Listener mode requires a bound local address");
                _listener.reset();
                return Error::Invalid;
        }
        Error e = _listener->listen(_localAddress);
        if (e.isError()) {
                promekiWarn("SrtSocketTransport::open SrtServer::listen(%s) failed (%s)",
                            _localAddress.toString().cstr(), e.name().cstr());
                _listener.reset();
                return e;
        }
        _socket = _listener->accept(_acceptTimeoutMs);
        if (!_socket) {
                promekiWarn("SrtSocketTransport::open SrtServer::accept timed out after %u ms on %s",
                            _acceptTimeoutMs, _localAddress.toString().cstr());
                _listener.reset();
                return Error::Timeout;
        }
        _peerAddress = _socket->peerAddress();
        // The listener has done its job — close it so it doesn't keep
        // refusing further callers in the background.  Future
        // connection attempts on the same port from outside would now
        // see a quiet kernel UDP port; that's the right behaviour for
        // a single-peer transport.
        _listener->close();
        _listener.reset();
        return Error::Ok;
}

void SrtSocketTransport::close() {
        if (_socket) {
                _socket->close();
                _socket.reset();
        }
        if (_listener) {
                _listener->close();
                _listener.reset();
        }
}

bool SrtSocketTransport::isOpen() const {
        return _socket && _socket->isOpen();
}

ssize_t SrtSocketTransport::sendPacket(const void *data, size_t size, const SocketAddress & /*dest*/) {
        if (!isOpen()) return -1;
        // dest is intentionally ignored — SRT is single-peer.  The
        // PacketTransport API keeps the parameter so RTP / RTCP code
        // can reuse the same call signature regardless of backend.
        return static_cast<ssize_t>(_socket->write(data, static_cast<int64_t>(size)));
}

int SrtSocketTransport::sendPackets(const DatagramList &datagrams) {
        if (!isOpen()) return -1;
        int sent = 0;
        for (const Datagram &d : datagrams) {
                const ssize_t n = _socket->write(d.data, static_cast<int64_t>(d.size));
                if (n < 0) break;
                ++sent;
        }
        return sent;
}

ssize_t SrtSocketTransport::receivePacket(void *data, size_t maxSize, SocketAddress *sender) {
        if (!isOpen()) return -1;
        const int64_t n = _socket->read(data, static_cast<int64_t>(maxSize));
        if (n < 0) return -1;
        if (sender) *sender = _peerAddress;
        return static_cast<ssize_t>(n);
}

PROMEKI_NAMESPACE_END
