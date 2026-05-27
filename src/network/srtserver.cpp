/**
 * @file      srtserver.cpp
 * @copyright Jason Howard. All rights reserved.
 *
 * See LICENSE file in the project root folder for license information.
 */

#include <promeki/srtserver.h>
#include <promeki/srtsocket.h>
#include <promeki/logger.h>
#include <promeki/thread.h>

#include <chrono>

#include <sys/socket.h>
#include <netinet/in.h>

#include <srt/srt.h>

PROMEKI_NAMESPACE_BEGIN

PROMEKI_DEBUG(SrtServer);

SrtServer::SrtServer(ObjectBase *parent) : ObjectBase(parent) {}

SrtServer::~SrtServer() {
        close();
}

namespace {

        // C-side bridge for srt_listen_callback.  libsrt invokes this
        // with the user-supplied opaque pointer (the SrtServer) plus
        // the pre-accept context.  We materialize the streamid +
        // peer address and delegate to the std::function the caller
        // installed via SrtServer::setListenCallback.  Returning -1
        // tells libsrt to refuse the connection.
        static int listenCallbackBridge(void *opaque, SRTSOCKET /*ns*/, int /*hsversion*/,
                                        const struct sockaddr *peeraddr, const char *streamid) {
                if (!opaque) return 0;
                auto *server = static_cast<SrtServer *>(opaque);
                if (!server->lastError().isEmpty()) {
                        // Defensive: previous-error string should not gate
                        // accepts; only the user callback decides.
                }
                String        sid = streamid ? String(streamid) : String();
                SocketAddress peer;
                if (peeraddr) {
                        // Determine sockaddr length from family (AF_INET/AF_INET6).
                        size_t len = 0;
                        if (peeraddr->sa_family == AF_INET)       len = sizeof(struct sockaddr_in);
                        else if (peeraddr->sa_family == AF_INET6) len = sizeof(struct sockaddr_in6);
                        if (len) {
                                auto r = SocketAddress::fromSockAddr(peeraddr, len);
                                if (r.second().isOk()) peer = r.first();
                        }
                }
                // The user callback is held inside the SrtServer; use a
                // friend-only accessor.  We cheat slightly by re-invoking
                // setListenCallback with the same callback to read it back
                // — but a cleaner path is to expose a private friend
                // function.  For phase 1 we let the bridge call directly
                // through a tiny static helper installed alongside the
                // bridge below.
                return SrtServer::dispatchListenCallback(server, sid, peer) ? 0 : -1;
        }

} // anonymous namespace

bool SrtServer::dispatchListenCallback(SrtServer *server, const String &streamId, const SocketAddress &peer) {
        if (!server || !server->_listenCb) return true; // no filter installed → accept
        return server->_listenCb(streamId, peer);
}

Error SrtServer::openAndConfigure() {
        if (_sock != SrtSocket::InvalidHandle) return Error::Ok;
        _sock = srt_create_socket();
        if (_sock == SRT_INVALID_SOCK) {
                _sock = SrtSocket::InvalidHandle;
                captureLastError();
                return Error::LibraryFailure;
        }

        // Listener-side options: SRT requires these to be set on the
        // listener socket before listen(); accepted sockets inherit
        // them via the standard handshake.

        if (_reuseAddr) {
                const int v = 1;
                if (srt_setsockflag(_sock, SRTO_REUSEADDR, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        if (_latencyMs > 0) {
                const int v = _latencyMs;
                if (srt_setsockflag(_sock, SRTO_LATENCY, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        if (_payloadSize > 0) {
                const int v = _payloadSize;
                if (srt_setsockflag(_sock, SRTO_PAYLOADSIZE, &v, sizeof(v)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        if (!_passphrase.isEmpty()) {
                if (srt_setsockflag(_sock, SRTO_PASSPHRASE, _passphrase.cstr(),
                                    static_cast<int>(_passphrase.byteCount())) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
                if (_pbKeyLen != 0) {
                        const int v = _pbKeyLen;
                        if (srt_setsockflag(_sock, SRTO_PBKEYLEN, &v, sizeof(v)) == SRT_ERROR) {
                                captureLastError();
                                return Error::LibraryFailure;
                        }
                }
        }

        if (_maxBw != 0) {
                if (srt_setsockflag(_sock, SRTO_MAXBW, &_maxBw, sizeof(_maxBw)) == SRT_ERROR) {
                        captureLastError();
                        return Error::LibraryFailure;
                }
        }

        return Error::Ok;
}

Error SrtServer::listen(const SocketAddress &address, int backlog) {
        if (_listening) {
                promekiWarn("SrtServer::listen(%s) called while already listening on %s",
                            address.toString().cstr(), _address.toString().cstr());
                return Error::AlreadyOpen;
        }
        Error e = openAndConfigure();
        if (e.isError()) {
                promekiWarn("SrtServer::listen: openAndConfigure failed (%s): %s", e.name().cstr(),
                            _lastError.cstr());
                return e;
        }

        if (_listenCb) {
                if (srt_listen_callback(_sock, &listenCallbackBridge, this) == SRT_ERROR) {
                        captureLastError();
                        promekiWarn("SrtServer::listen: srt_listen_callback failed: %s", _lastError.cstr());
                        close();
                        return Error::LibraryFailure;
                }
        }

        struct sockaddr_storage storage;
        const size_t            len = address.toSockAddr(&storage);
        if (len == 0) {
                promekiWarn("SrtServer::listen: invalid address '%s'", address.toString().cstr());
                return Error::Invalid;
        }

        if (srt_bind(_sock, reinterpret_cast<struct sockaddr *>(&storage), static_cast<int>(len)) == SRT_ERROR) {
                captureLastError();
                promekiWarn("SrtServer::listen: srt_bind(%s) failed: %s", address.toString().cstr(),
                            _lastError.cstr());
                close();
                return Error::LibraryFailure;
        }
        if (srt_listen(_sock, backlog) == SRT_ERROR) {
                captureLastError();
                promekiWarn("SrtServer::listen: srt_listen(backlog=%d) failed on %s: %s", backlog,
                            address.toString().cstr(), _lastError.cstr());
                close();
                return Error::LibraryFailure;
        }
        _listening = true;
        _address = address;
        // Refresh address in case bind used port 0 and the kernel picked one.
        struct sockaddr_storage actual;
        int                     actualLen = sizeof(actual);
        if (srt_getsockname(_sock, reinterpret_cast<struct sockaddr *>(&actual), &actualLen) != SRT_ERROR) {
                auto r = SocketAddress::fromSockAddr(reinterpret_cast<struct sockaddr *>(&actual),
                                                    static_cast<size_t>(actualLen));
                if (r.second().isOk()) _address = r.first();
        }
        return Error::Ok;
}

void SrtServer::close() {
        if (_sock == SrtSocket::InvalidHandle) return;
        srt_close(_sock);
        _sock = SrtSocket::InvalidHandle;
        _listening = false;
}

SrtSocket::UPtr SrtServer::accept(unsigned int timeoutMs) {
        if (!_listening) return nullptr;

        // The listener's blocking SRTO_RCVSYN flag controls whether
        // srt_accept blocks indefinitely or returns immediately.  We
        // honour @p timeoutMs by polling: most callers want bounded
        // waits, even when the listener is in blocking mode.
        const auto deadline = (timeoutMs == 0)
                                      ? std::chrono::steady_clock::time_point::max()
                                      : std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

        // Force the listener to non-blocking accept while we own the
        // wait loop, then restore it after.  Saves a thread that
        // would otherwise be stuck inside srt_accept.
        int saveSync = 1;
        int len = sizeof(saveSync);
        srt_getsockflag(_sock, SRTO_RCVSYN, &saveSync, &len);
        const int nonBlock = 0;
        srt_setsockflag(_sock, SRTO_RCVSYN, &nonBlock, sizeof(nonBlock));

        SrtSocket::UPtr result;
        while (true) {
                struct sockaddr_storage peer;
                int                     peerLen = sizeof(peer);
                const int               s = srt_accept(_sock, reinterpret_cast<struct sockaddr *>(&peer), &peerLen);
                if (s != SRT_INVALID_SOCK) {
                        // Restore listener block-mode before constructing
                        // the SrtSocket so the user-visible state is intact.
                        srt_setsockflag(_sock, SRTO_RCVSYN, &saveSync, sizeof(saveSync));
                        // Adopt the new SRTSOCKET — SrtSocket adopts and
                        // refreshes peer/local addresses internally.
                        result = SrtSocket::UPtr::create(s);
                        // SrtSocket adoption walks getpeername; nothing to
                        // do here other than emit the signal and return.
                        newConnectionSignal.emit();
                        return result;
                }
                // -1 with non-blocking: spin until deadline.
                if (std::chrono::steady_clock::now() >= deadline) break;
                BasicThread::sleepMs(10);
        }
        captureLastError();
        srt_setsockflag(_sock, SRTO_RCVSYN, &saveSync, sizeof(saveSync));
        return nullptr;
}

Error SrtServer::setNonBlocking(bool enable) {
        if (_sock == SrtSocket::InvalidHandle) return Error::NotOpen;
        const int sync = enable ? 0 : 1;
        if (srt_setsockflag(_sock, SRTO_RCVSYN, &sync, sizeof(sync)) == SRT_ERROR) {
                captureLastError();
                return Error::LibraryFailure;
        }
        return Error::Ok;
}

Error SrtServer::setLatency(int ms) {
        if (ms < 0 || ms > 60000) return Error::Invalid;
        _latencyMs = ms;
        return Error::Ok;
}

Error SrtServer::setPassphrase(const String &passphrase) {
        if (!passphrase.isEmpty() && (passphrase.byteCount() < 10 || passphrase.byteCount() > 79)) return Error::Invalid;
        _passphrase = passphrase;
        return Error::Ok;
}

Error SrtServer::setEncryptionKeyLength(int bytes) {
        if (bytes != 0 && bytes != 16 && bytes != 24 && bytes != 32) return Error::Invalid;
        _pbKeyLen = bytes;
        return Error::Ok;
}

Error SrtServer::setMaxBandwidth(int64_t bytesPerSec) {
        _maxBw = bytesPerSec;
        return Error::Ok;
}

Error SrtServer::setPayloadSize(int bytes) {
        if (bytes < 0 || bytes > 1456) return Error::Invalid;
        _payloadSize = bytes;
        return Error::Ok;
}

Error SrtServer::setReuseAddress(bool enable) {
        _reuseAddr = enable;
        return Error::Ok;
}

Error SrtServer::setListenCallback(ListenCallback cb) {
        if (_listening) return Error::AlreadyOpen;
        _listenCb = std::move(cb);
        return Error::Ok;
}

void SrtServer::captureLastError() {
        const char *s = srt_getlasterror_str();
        _lastError = (s ? String(s) : String());
}

PROMEKI_NAMESPACE_END
